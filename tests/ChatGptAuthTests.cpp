#include <ChatGptAuth.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

void test_token_manager_skips_refresh_for_valid_token() {
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "nissefar-chatgpt-auth-manager-valid";
  std::filesystem::remove_all(temp_dir);
  const auto auth_path = temp_dir / "chatgpt.json";

  const ChatGptAuth auth{.type = "oauth",
                         .refresh = "refresh-token",
                         .access = "access-token",
                         .expires = 2000,
                         .account_id = std::string("acct_current")};
  expect_true(ChatGptAuthStore::write_to_path(auth, auth_path.string()).empty(),
              "write valid auth for token manager test");

  int refresh_calls = 0;
  ChatGptAuthManager manager(
      auth_path.string(),
      [&refresh_calls](const std::string &) {
        ++refresh_calls;
        return chatgpt_device_auth::OAuthTokenResult{std::nullopt,
                                                     "refresh should not run"};
      },
      [] { return 1000; });

  const auto result = manager.ensure_valid();
  expect_true(result.ok(), "token manager accepts unexpired auth");
  expect_false(result.refreshed, "token manager does not refresh valid token");
  expect_true(refresh_calls == 0, "refresh was not called");
  expect_true(result.auth->access == "access-token", "existing access token kept");

  std::filesystem::remove_all(temp_dir);
}

void test_token_manager_refreshes_and_preserves_account_id() {
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "nissefar-chatgpt-auth-manager-refresh";
  std::filesystem::remove_all(temp_dir);
  const auto auth_path = temp_dir / "chatgpt.json";

  const ChatGptAuth auth{.type = "oauth",
                         .refresh = "stale-refresh",
                         .access = "stale-access",
                         .expires = 900,
                         .account_id = std::string("acct_existing")};
  expect_true(ChatGptAuthStore::write_to_path(auth, auth_path.string()).empty(),
              "write expired auth for refresh test");

  ChatGptAuthManager manager(
      auth_path.string(),
      [](const std::string &refresh_token) {
        expect_true(refresh_token == "stale-refresh",
                    "refresh request uses stored refresh token");
        return chatgpt_device_auth::OAuthTokenResult{
            chatgpt_device_auth::OAuthTokenResponse{.access_token = "new-access",
                                                    .refresh_token = "new-refresh",
                                                    .id_token = "",
                                                    .expires_in = 600},
            {}};
      },
      [] { return 1000; });

  const auto result = manager.ensure_valid();
  expect_true(result.ok(), "token manager refresh succeeds");
  expect_true(result.refreshed, "token manager reports refresh");
  if (result.ok()) {
    expect_true(result.auth->access == "new-access", "access token updated");
    expect_true(result.auth->refresh == "new-refresh", "refresh token updated");
    expect_true(result.auth->expires == 1600, "expiry updated from now + expires_in");
    expect_true(result.auth->account_id == std::optional<std::string>{"acct_existing"},
                "existing account id preserved when refresh lacks one");
  }

  const auto persisted = ChatGptAuthStore::load_from_path(auth_path.string());
  expect_true(persisted.ok(), "refreshed auth persisted to disk");
  if (persisted.ok()) {
    expect_true(persisted.auth->access == "new-access",
                "persisted access token updated");
    expect_true(persisted.auth->account_id == std::optional<std::string>{"acct_existing"},
                "persisted account id preserved");
  }

  std::filesystem::remove_all(temp_dir);
}

void test_token_manager_refresh_updates_account_id_from_token_claims() {
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "nissefar-chatgpt-auth-manager-account";
  std::filesystem::remove_all(temp_dir);
  const auto auth_path = temp_dir / "chatgpt.json";

  const ChatGptAuth auth{.type = "oauth",
                         .refresh = "refresh-token",
                         .access = "access-token",
                         .expires = 900,
                         .account_id = std::string("acct_old")};
  expect_true(ChatGptAuthStore::write_to_path(auth, auth_path.string()).empty(),
              "write expired auth for account id refresh test");

  ChatGptAuthManager manager(
      auth_path.string(),
      [](const std::string &) {
        return chatgpt_device_auth::OAuthTokenResult{
            chatgpt_device_auth::OAuthTokenResponse{
                .access_token = "new-access",
                .refresh_token = "new-refresh",
                .id_token =
                    "eyJhbGciOiJub25lIn0.eyJjaGF0Z3B0X2FjY291bnRfaWQiOiJhY2N0X25ldyJ9.",
                .expires_in = 120},
            {}};
      },
      [] { return 1000; });

  const auto result = manager.ensure_valid();
  expect_true(result.ok(), "refresh with account claim succeeds");
  if (result.ok()) {
    expect_true(result.auth->account_id == std::optional<std::string>{"acct_new"},
                "account id updated from refreshed token claims");
  }

  std::filesystem::remove_all(temp_dir);
}

void test_token_manager_reports_refresh_failures() {
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "nissefar-chatgpt-auth-manager-failure";
  std::filesystem::remove_all(temp_dir);
  const auto auth_path = temp_dir / "chatgpt.json";

  const ChatGptAuth auth{.type = "oauth",
                         .refresh = "stale-refresh",
                         .access = "stale-access",
                         .expires = 900,
                         .account_id = std::nullopt};
  expect_true(ChatGptAuthStore::write_to_path(auth, auth_path.string()).empty(),
              "write expired auth for refresh failure test");

  ChatGptAuthManager manager(
      auth_path.string(),
      [](const std::string &) {
        return chatgpt_device_auth::OAuthTokenResult{std::nullopt,
                                                     "HTTP 401"};
      },
      [] { return 1000; });

  const auto result = manager.ensure_valid();
  expect_false(result.ok(), "refresh failure is surfaced");
  expect_true(result.error.find("Failed to refresh ChatGPT auth") != std::string::npos,
              "refresh failure has clear prefix");

  std::filesystem::remove_all(temp_dir);
}

void test_token_manager_force_refreshes_valid_token() {
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "nissefar-chatgpt-auth-manager-force-refresh";
  std::filesystem::remove_all(temp_dir);
  const auto auth_path = temp_dir / "chatgpt.json";

  const ChatGptAuth auth{.type = "oauth",
                         .refresh = "refresh-token",
                         .access = "access-token",
                         .expires = 2000,
                         .account_id = std::string("acct_current")};
  expect_true(ChatGptAuthStore::write_to_path(auth, auth_path.string()).empty(),
              "write valid auth for force refresh test");

  int refresh_calls = 0;
  ChatGptAuthManager manager(
      auth_path.string(),
      [&refresh_calls](const std::string &refresh_token) {
        ++refresh_calls;
        expect_true(refresh_token == "refresh-token",
                    "force refresh uses stored refresh token");
        return chatgpt_device_auth::OAuthTokenResult{
            chatgpt_device_auth::OAuthTokenResponse{.access_token = "new-access",
                                                    .refresh_token = "new-refresh",
                                                    .id_token = "",
                                                    .expires_in = 600},
            {}};
      },
      [] { return 1000; });

  const auto result = manager.ensure_valid(true);
  expect_true(result.ok(), "forced refresh succeeds");
  expect_true(result.refreshed, "forced refresh reports refresh");
  expect_true(refresh_calls == 1, "force refresh calls refresh even if token is valid");
  if (result.ok()) {
    expect_true(result.auth->access == "new-access",
                "force refresh updates access token");
  }

  std::filesystem::remove_all(temp_dir);
}

void test_expiry_check_uses_refresh_skew() {
  const ChatGptAuth auth{.type = "oauth",
                         .refresh = "refresh",
                         .access = "access",
                         .expires = 1059,
                         .account_id = std::nullopt};
  expect_true(ChatGptAuthManager::is_expired(auth, 1000),
              "token inside default refresh skew is treated as expired");
  expect_false(ChatGptAuthManager::is_expired(auth, 1000, 30),
               "smaller skew keeps token valid");
}

} // namespace

int main() {
  test_missing_auth_file_rejected();
  test_invalid_auth_payload_rejected();
  test_write_and_read_round_trip();
  test_token_manager_skips_refresh_for_valid_token();
  test_token_manager_refreshes_and_preserves_account_id();
  test_token_manager_refresh_updates_account_id_from_token_claims();
  test_token_manager_reports_refresh_failures();
  test_token_manager_force_refreshes_valid_token();
  test_expiry_check_uses_refresh_skew();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All ChatGptAuth tests passed\n";
  return 0;
}
