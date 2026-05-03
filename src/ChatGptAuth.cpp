#include <ChatGptAuth.h>

#include <Json.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include <sys/stat.h>
#include <chrono>

namespace {

std::int64_t current_unix_time() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string auth_dir_from_home(const char *home) {
  return std::format("{}/.config/nissefar", home);
}

std::string validate_json_auth(const Json &payload, ChatGptAuth &auth) {
  if (!payload.is_object()) {
    return "ChatGPT auth file must contain a JSON object.";
  }

  if (!payload.contains("type") || !payload["type"].is_string()) {
    return "ChatGPT auth file is missing required string field 'type'.";
  }
  if (payload["type"].get<std::string>() != "oauth") {
    return "ChatGPT auth file field 'type' must be 'oauth'.";
  }
  if (!payload.contains("refresh") || !payload["refresh"].is_string() ||
      payload["refresh"].get<std::string>().empty()) {
    return "ChatGPT auth file is missing required string field 'refresh'.";
  }
  if (!payload.contains("access") || !payload["access"].is_string() ||
      payload["access"].get<std::string>().empty()) {
    return "ChatGPT auth file is missing required string field 'access'.";
  }
  if (!payload.contains("expires") || !payload["expires"].is_number_integer()) {
    return "ChatGPT auth file is missing required integer field 'expires'.";
  }
  if (payload.contains("accountId") && !payload["accountId"].is_null() &&
      !payload["accountId"].is_string()) {
    return "ChatGPT auth file field 'accountId' must be a string when present.";
  }

  auth.type = payload["type"].get<std::string>();
  auth.refresh = payload["refresh"].get<std::string>();
  auth.access = payload["access"].get<std::string>();
  auth.expires = payload["expires"].get<std::int64_t>();
  if (payload.contains("accountId") && payload["accountId"].is_string()) {
    auth.account_id = payload["accountId"].get<std::string>();
  }

  return {};
}

Json to_json(const ChatGptAuth &auth) {
  Json payload = Json::object();
  payload["type"] = auth.type;
  payload["refresh"] = auth.refresh;
  payload["access"] = auth.access;
  payload["expires"] = auth.expires;
  if (auth.account_id.has_value()) {
    payload["accountId"] = *auth.account_id;
  }
  return payload;
}

std::string chmod_0600(const std::string &path) {
  if (::chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
    return std::format("Failed to set auth file permissions on {}: {}", path,
                       std::strerror(errno));
  }
  return {};
}

} // namespace

ChatGptAuthManager::ChatGptAuthManager(std::string path,
                                       RefreshAccessTokenFn refresh_access_token,
                                       NowFn now)
    : path_(std::move(path)),
      refresh_access_token_(std::move(refresh_access_token)),
      now_(std::move(now)) {
  if (!refresh_access_token_) {
    refresh_access_token_ = [](const std::string &refresh_token) {
      return chatgpt_device_auth::Client().refresh_access_token(refresh_token);
    };
  }
  if (!now_) {
    now_ = [] { return current_unix_time(); };
  }
}

const std::string &ChatGptAuthManager::path() const { return path_; }

ChatGptAuthResult ChatGptAuthManager::load() const {
  if (path_.empty()) {
    return {std::nullopt, {},
            "HOME is not set; cannot resolve ~/.config/nissefar/chatgpt.json"};
  }
  return ChatGptAuthStore::load_from_path(path_);
}

bool ChatGptAuthManager::is_expired(const ChatGptAuth &auth, std::int64_t now,
                                    std::int64_t refresh_skew_seconds) {
  return auth.expires <= (now + refresh_skew_seconds);
}

ChatGptAuthRefreshResult ChatGptAuthManager::ensure_valid(bool force_refresh) {
  std::lock_guard lock(mutex_);

  const auto current = load();
  if (!current.ok()) {
    return {std::nullopt, current.path, current.error, false};
  }

  const std::int64_t now = now_();
  if (!force_refresh && !is_expired(*current.auth, now)) {
    return {*current.auth, current.path, {}, false};
  }

  const auto refresh_result = refresh_access_token_(current.auth->refresh);
  if (!refresh_result.ok()) {
    return {std::nullopt, current.path,
            std::format("Failed to refresh ChatGPT auth: {}",
                        refresh_result.error),
            false};
  }

  const auto &tokens = *refresh_result.tokens;
  ChatGptAuth refreshed{.type = "oauth",
                        .refresh = tokens.refresh_token,
                        .access = tokens.access_token,
                        .expires = now + std::max(tokens.expires_in, 1),
                        .account_id = chatgpt_device_auth::extract_account_id(
                            tokens)};

  if (!refreshed.account_id.has_value()) {
    refreshed.account_id = current.auth->account_id;
  }

  const std::string write_error =
      ChatGptAuthStore::write_to_path(refreshed, current.path);
  if (!write_error.empty()) {
    return {std::nullopt, current.path,
            std::format("Failed to persist refreshed ChatGPT auth: {}",
                        write_error),
            false};
  }

  return {refreshed, current.path, {}, true};
}

std::string ChatGptAuthStore::default_path() {
  const char *home = std::getenv("HOME");
  if (!home) {
    return {};
  }
  return std::format("{}/chatgpt.json", auth_dir_from_home(home));
}

ChatGptAuthResult ChatGptAuthStore::load() {
  const char *home = std::getenv("HOME");
  if (!home) {
    return {std::nullopt, {},
            "HOME is not set; cannot resolve ~/.config/nissefar/chatgpt.json"};
  }

  return load_from_path(std::format("{}/chatgpt.json", auth_dir_from_home(home)));
}

ChatGptAuthResult ChatGptAuthStore::load_from_path(const std::string &path) {
  namespace fs = std::filesystem;

  const fs::path auth_path(path);
  const fs::path auth_dir = auth_path.parent_path();

  if (auth_dir.empty() || !fs::exists(auth_dir)) {
    return {std::nullopt, path,
            std::format("ChatGPT auth directory does not exist: {}",
                        auth_dir.empty() ? path : auth_dir.string())};
  }
  if (!fs::exists(auth_path)) {
    return {std::nullopt, path,
            std::format("ChatGPT auth file not found: {}", path)};
  }

  std::ifstream input(path);
  if (!input) {
    return {std::nullopt, path,
            std::format("Failed to open ChatGPT auth file: {}", path)};
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();

  Json payload;
  try {
    payload = Json::parse(buffer.str());
  } catch (const std::exception &e) {
    return {std::nullopt, path,
            std::format("Malformed ChatGPT auth file {}: {}", path, e.what())};
  }

  ChatGptAuth auth;
  const std::string validation_error = validate_json_auth(payload, auth);
  if (!validation_error.empty()) {
    return {std::nullopt, path,
            std::format("Invalid ChatGPT auth file {}: {}", path,
                        validation_error)};
  }

  return {auth, path, {}};
}

std::string ChatGptAuthStore::write(const ChatGptAuth &auth) {
  const char *home = std::getenv("HOME");
  if (!home) {
    return "HOME is not set; cannot resolve ~/.config/nissefar/chatgpt.json";
  }

  return write_to_path(auth,
                       std::format("{}/chatgpt.json", auth_dir_from_home(home)));
}

std::string ChatGptAuthStore::write_to_path(const ChatGptAuth &auth,
                                            const std::string &path) {
  namespace fs = std::filesystem;

  if (auth.type != "oauth") {
    return "ChatGPT auth writes require type='oauth'.";
  }
  if (auth.refresh.empty() || auth.access.empty() || auth.expires == 0) {
    return "ChatGPT auth writes require refresh, access, and expires.";
  }

  const fs::path auth_path(path);
  const fs::path auth_dir = auth_path.parent_path();
  if (auth_dir.empty()) {
    return std::format("ChatGPT auth path has no parent directory: {}", path);
  }

  std::error_code ec;
  fs::create_directories(auth_dir, ec);
  if (ec) {
    return std::format("Failed to create ChatGPT auth directory {}: {}",
                       auth_dir.string(), ec.message());
  }

  const fs::path tmp_path =
      auth_dir / std::format(".chatgpt.json.tmp.{}", std::rand());

  std::ofstream output(tmp_path);
  if (!output) {
    return std::format("Failed to open temporary auth file: {}",
                       tmp_path.string());
  }
  output << to_json(auth).dump(2) << '\n';
  output.close();
  if (!output) {
    std::filesystem::remove(tmp_path, ec);
    return std::format("Failed to write temporary auth file: {}",
                       tmp_path.string());
  }

  const std::string chmod_error = chmod_0600(tmp_path.string());
  if (!chmod_error.empty()) {
    std::filesystem::remove(tmp_path, ec);
    return chmod_error;
  }

  fs::rename(tmp_path, auth_path, ec);
  if (ec) {
    std::filesystem::remove(tmp_path, ec);
    return std::format("Failed to replace ChatGPT auth file {}: {}", path,
                       ec.message());
  }

  return chmod_0600(path);
}
