#include <ChatGptAuth.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <sys/stat.h>

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++failures;
  }
}

void expect_false(bool condition, const std::string &message) {
  expect_true(!condition, message);
}

void test_missing_auth_file_rejected() {
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "nissefar-chatgpt-auth-missing";
  std::filesystem::remove_all(temp_dir);

  const auto result =
      ChatGptAuthStore::load_from_path((temp_dir / "chatgpt.json").string());
  expect_false(result.ok(), "missing auth file should fail");
  expect_true(result.error.find("not found") != std::string::npos ||
                  result.error.find("does not exist") != std::string::npos,
              "missing auth error mentions missing path");
}

void test_invalid_auth_payload_rejected() {
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "nissefar-chatgpt-auth-invalid";
  std::filesystem::create_directories(temp_dir);
  const auto auth_path = temp_dir / "chatgpt.json";

  std::ofstream output(auth_path);
  output << R"({"type":"oauth","refresh":"r","access":"a"})";
  output.close();

  const auto result = ChatGptAuthStore::load_from_path(auth_path.string());
  expect_false(result.ok(), "auth file missing expires should fail");
  expect_true(result.error.find("expires") != std::string::npos,
              "invalid auth error mentions expires");

  std::filesystem::remove_all(temp_dir);
}

void test_write_and_read_round_trip() {
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "nissefar-chatgpt-auth-roundtrip";
  std::filesystem::remove_all(temp_dir);
  const auto auth_path = temp_dir / "chatgpt.json";

  const ChatGptAuth auth{.type = "oauth",
                         .refresh = "refresh-token",
                         .access = "access-token",
                         .expires = 123456789,
                         .account_id = std::string("acct_123")};

  const std::string write_error =
      ChatGptAuthStore::write_to_path(auth, auth_path.string());
  expect_true(write_error.empty(), "write auth file succeeds");

  struct stat st {};
  expect_true(::stat(auth_path.c_str(), &st) == 0, "auth file exists after write");
  expect_true((st.st_mode & 0777) == 0600,
              "auth file permissions are 0600");

  const auto result = ChatGptAuthStore::load_from_path(auth_path.string());
  expect_true(result.ok(), "written auth file can be loaded");
  if (result.ok()) {
    expect_true(result.auth->type == "oauth", "type round-trips");
    expect_true(result.auth->refresh == "refresh-token", "refresh round-trips");
    expect_true(result.auth->access == "access-token", "access round-trips");
    expect_true(result.auth->expires == 123456789, "expires round-trips");
    expect_true(result.auth->account_id == std::optional<std::string>{"acct_123"},
                "accountId round-trips");
  }

  std::filesystem::remove_all(temp_dir);
}

} // namespace

int main() {
  test_missing_auth_file_rejected();
  test_invalid_auth_payload_rejected();
  test_write_and_read_round_trip();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All ChatGptAuth tests passed\n";
  return 0;
}
