#ifndef CHATGPTAUTH_H
#define CHATGPTAUTH_H

#include <optional>
#include <string>
#include <cstdint>
#include <functional>
#include <mutex>

#include <ChatGptDeviceAuth.h>

struct ChatGptAuth {
  std::string type;
  std::string refresh;
  std::string access;
  std::int64_t expires{0};
  std::optional<std::string> account_id;
};

struct ChatGptAuthResult {
  std::optional<ChatGptAuth> auth;
  std::string path;
  std::string error;

  [[nodiscard]] bool ok() const { return auth.has_value(); }
};

class ChatGptAuthStore {
public:
  static std::string default_path();
  static ChatGptAuthResult load();
  static ChatGptAuthResult load_from_path(const std::string &path);
  static std::string write(const ChatGptAuth &auth);
  static std::string write_to_path(const ChatGptAuth &auth, const std::string &path);
};

struct ChatGptAuthRefreshResult {
  std::optional<ChatGptAuth> auth;
  std::string path;
  std::string error;
  bool refreshed{false};

  [[nodiscard]] bool ok() const { return auth.has_value(); }
};

class ChatGptAuthManager {
public:
  using RefreshAccessTokenFn = std::function<chatgpt_device_auth::OAuthTokenResult(
      const std::string &)>;
  using NowFn = std::function<std::int64_t()>;

  explicit ChatGptAuthManager(
      std::string path = ChatGptAuthStore::default_path(),
      RefreshAccessTokenFn refresh_access_token = {}, NowFn now = {});

  [[nodiscard]] const std::string &path() const;
  [[nodiscard]] ChatGptAuthResult load() const;
  [[nodiscard]] ChatGptAuthRefreshResult ensure_valid(
      bool force_refresh = false);

  static bool is_expired(const ChatGptAuth &auth, std::int64_t now,
                         std::int64_t refresh_skew_seconds = 60);

private:
  std::string path_;
  RefreshAccessTokenFn refresh_access_token_;
  NowFn now_;
  mutable std::mutex mutex_;
};

#endif // CHATGPTAUTH_H
