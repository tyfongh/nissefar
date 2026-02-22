#include <VideoSummaryService.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <optional>
#include <poll.h>
#include <regex>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct CommandResult {
  int exit_code = -1;
  bool timed_out = false;
  std::string output;
};

std::string trim_copy(std::string value) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

bool is_valid_http_url(const std::string &url) {
  static const std::regex url_re(R"(^https?://\S+$)", std::regex::icase);
  return std::regex_match(url, url_re);
}

std::optional<std::filesystem::path>
resolve_script_path(const Config &config) {
  auto make_absolute = [](std::filesystem::path path) {
    if (path.is_relative())
      path = std::filesystem::current_path() / path;
    return path;
  };

  if (!config.video_summary_script_path.empty()) {
    std::filesystem::path configured =
        make_absolute(std::filesystem::path(config.video_summary_script_path));
    if (std::filesystem::exists(configured)) {
      return configured;
    }
  }

  std::filesystem::path fallback =
      make_absolute(std::filesystem::path("../scripts/summarize_video.sh"));
  if (std::filesystem::exists(fallback)) {
    return fallback;
  }

  return std::nullopt;
}

bool is_executable_file(const std::filesystem::path &path) {
  return std::filesystem::is_regular_file(path) &&
         access(path.c_str(), X_OK) == 0;
}

void read_available_output(int fd, std::string &output, size_t output_cap) {
  char buffer[4096];
  for (;;) {
    const ssize_t bytes = read(fd, buffer, sizeof(buffer));
    if (bytes > 0) {
      if (output.size() < output_cap) {
        const size_t room = output_cap - output.size();
        const size_t to_append = static_cast<size_t>(bytes) > room
                                     ? room
                                     : static_cast<size_t>(bytes);
        output.append(buffer, to_append);
      }
      continue;
    }

    if (bytes == 0)
      return;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;

    return;
  }
}

CommandResult run_script(const std::filesystem::path &script_path,
                         const std::string &url,
                         std::chrono::seconds timeout) {
  constexpr size_t output_cap = 20000;
  CommandResult result{};

  int pipe_fds[2] = {-1, -1};
  if (pipe(pipe_fds) != 0) {
    result.output = std::format("Tool error: failed to create process pipe: {}",
                                std::strerror(errno));
    return result;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    result.output = std::format("Tool error: failed to fork process: {}",
                                std::strerror(errno));
    return result;
  }

  if (pid == 0) {
    dup2(pipe_fds[1], STDOUT_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    execl(script_path.c_str(), script_path.c_str(), url.c_str(),
          static_cast<char *>(nullptr));
    _exit(127);
  }

  close(pipe_fds[1]);
  const int old_flags = fcntl(pipe_fds[0], F_GETFL, 0);
  if (old_flags >= 0)
    fcntl(pipe_fds[0], F_SETFL, old_flags | O_NONBLOCK);

  int child_status = 0;
  bool child_running = true;
  const auto started = std::chrono::steady_clock::now();

  while (true) {
    if (child_running) {
      const auto now = std::chrono::steady_clock::now();
      if (now - started >= timeout) {
        result.timed_out = true;
        kill(pid, SIGKILL);
        waitpid(pid, &child_status, 0);
        child_running = false;
      }
    }

    if (child_running) {
      int wait_status = 0;
      const pid_t wait_result = waitpid(pid, &wait_status, WNOHANG);
      if (wait_result == pid) {
        child_status = wait_status;
        child_running = false;
      }
    }

    pollfd pfd{};
    pfd.fd = pipe_fds[0];
    pfd.events = POLLIN | POLLHUP;
    (void)poll(&pfd, 1, child_running ? 100 : 0);
    read_available_output(pipe_fds[0], result.output, output_cap);

    if (!child_running) {
      char tmp[1];
      const ssize_t bytes = read(pipe_fds[0], tmp, sizeof(tmp));
      if (bytes == 0 || (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
        break;
      }
      if (bytes > 0 && result.output.size() < output_cap) {
        result.output.push_back(tmp[0]);
      }
    }
  }

  close(pipe_fds[0]);

  if (WIFEXITED(child_status)) {
    result.exit_code = WEXITSTATUS(child_status);
  } else if (WIFSIGNALED(child_status)) {
    result.exit_code = 128 + WTERMSIG(child_status);
  }

  return result;
}

} // namespace

VideoSummaryService::VideoSummaryService(const Config &config, dpp::cluster &bot)
    : config(config), bot(bot) {}

dpp::task<std::string>
VideoSummaryService::summarize_video(const std::string &url) const {
  constexpr std::chrono::seconds timeout(300);

  if (!is_valid_http_url(url)) {
    co_return "Tool error: invalid URL. Use an absolute http/https URL.";
  }

  const auto script_path = resolve_script_path(config);
  if (!script_path.has_value()) {
    co_return "Tool error: summarize script not found (configured path or fallback ../scripts/summarize_video.sh).";
  }

  if (!is_executable_file(*script_path)) {
    co_return std::format("Tool error: summarize script is not executable: {}",
                          script_path->string());
  }

  bot.log(dpp::ll_info,
          std::format("Running video summary script: {}", script_path->string()));

  CommandResult result = run_script(*script_path, url, timeout);
  std::string output = trim_copy(result.output);

  if (result.timed_out) {
    co_return "Tool error: video summarization timed out after 300 seconds.";
  }

  if (result.exit_code != 0) {
    if (output.empty()) {
      co_return std::format(
          "Tool error: video summarization failed with exit code {}.",
          result.exit_code);
    }
    co_return std::format(
        "Tool error: video summarization failed with exit code {}. Output: {}",
        result.exit_code, output);
  }

  if (output.empty()) {
    co_return "Tool error: video summarization produced empty output.";
  }

  co_return output;
}
