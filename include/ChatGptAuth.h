#ifndef CHATGPTAUTH_H
#define CHATGPTAUTH_H

#include <optional>
#include <string>
#include <cstdint>

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

#endif // CHATGPTAUTH_H
