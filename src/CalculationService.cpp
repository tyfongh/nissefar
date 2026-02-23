#include <CalculationService.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <poll.h>
#include <regex>
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

bool is_valid_expression(const std::string &expression) {
  static const std::regex allowed_re(
      R"(^[0-9a-zA-Z_+\-*/%^().,\s]+$)", std::regex::ECMAScript);
  return std::regex_match(expression, allowed_re);
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

CommandResult run_bc(const std::string &expression, int scale,
                     std::chrono::seconds timeout) {
  constexpr size_t output_cap = 20000;
  CommandResult result{};

  int stdin_pipe[2] = {-1, -1};
  int output_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(output_pipe) != 0) {
    if (stdin_pipe[0] >= 0) {
      close(stdin_pipe[0]);
      close(stdin_pipe[1]);
    }
    result.output = std::format("Tool error: failed to create process pipes: {}",
                                std::strerror(errno));
    return result;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(output_pipe[0]);
    close(output_pipe[1]);
    result.output = std::format("Tool error: failed to fork process: {}",
                                std::strerror(errno));
    return result;
  }

  if (pid == 0) {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(output_pipe[1], STDOUT_FILENO);
    dup2(output_pipe[1], STDERR_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(output_pipe[0]);
    close(output_pipe[1]);

    execlp("bc", "bc", "-l", static_cast<char *>(nullptr));
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(output_pipe[1]);

  std::string input = std::format("scale={}\n{}\n", scale, expression);
  size_t written = 0;
  while (written < input.size()) {
    const ssize_t n = write(stdin_pipe[1], input.data() + written,
                            input.size() - written);
    if (n > 0) {
      written += static_cast<size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    break;
  }
  close(stdin_pipe[1]);

  const int old_flags = fcntl(output_pipe[0], F_GETFL, 0);
  if (old_flags >= 0)
    fcntl(output_pipe[0], F_SETFL, old_flags | O_NONBLOCK);

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
    pfd.fd = output_pipe[0];
    pfd.events = POLLIN | POLLHUP;
    (void)poll(&pfd, 1, child_running ? 100 : 0);
    read_available_output(output_pipe[0], result.output, output_cap);

    if (!child_running) {
      char tmp[1];
      const ssize_t bytes = read(output_pipe[0], tmp, sizeof(tmp));
      if (bytes == 0 || (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
        break;
      }
      if (bytes > 0 && result.output.size() < output_cap) {
        result.output.push_back(tmp[0]);
      }
    }
  }

  close(output_pipe[0]);

  if (WIFEXITED(child_status)) {
    result.exit_code = WEXITSTATUS(child_status);
  } else if (WIFSIGNALED(child_status)) {
    result.exit_code = 128 + WTERMSIG(child_status);
  }

  return result;
}

} // namespace

CalculationService::CalculationService(dpp::cluster &bot) : bot(bot) {}

dpp::task<std::string>
CalculationService::calculate_with_bc(const std::string &expression,
                                      int scale) const {
  constexpr std::chrono::seconds timeout(2);

  const std::string trimmed_expression = trim_copy(expression);
  if (trimmed_expression.empty()) {
    co_return "Tool error: expression cannot be empty.";
  }

  if (trimmed_expression.size() > 500) {
    co_return "Tool error: expression too long (max 500 characters).";
  }

  if (!is_valid_expression(trimmed_expression)) {
    co_return "Tool error: expression contains unsupported characters.";
  }

  if (scale < 0 || scale > 100) {
    co_return "Tool error: scale must be between 0 and 100.";
  }

  CommandResult result = run_bc(trimmed_expression, scale, timeout);
  std::string output = trim_copy(result.output);

  if (result.timed_out) {
    co_return "Tool error: bc calculation timed out after 2 seconds.";
  }

  if (result.exit_code != 0) {
    if (output.empty()) {
      co_return std::format("Tool error: bc failed with exit code {}.",
                            result.exit_code);
    }
    co_return std::format("Tool error: bc failed with exit code {}. Output: {}",
                          result.exit_code, output);
  }

  if (output.empty()) {
    co_return "Tool error: bc produced empty output.";
  }

  bot.log(dpp::ll_info,
          std::format("bc calculation done: expr_len={} output_bytes={}",
                      trimmed_expression.size(), output.size()));

  co_return output;
}
