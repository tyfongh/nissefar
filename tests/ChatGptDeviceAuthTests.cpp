#include <ChatGptDeviceAuth.h>

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++failures;
  }
}

void test_parse_jwt_claims_and_extract_account_id() {
  const std::string token =
      "eyJhbGciOiJub25lIn0.eyJjaGF0Z3B0X2FjY291bnRfaWQiOiJhY2N0XzEyMyJ9.";
  const auto claims = chatgpt_device_auth::parse_jwt_claims(token);
  expect_true(claims.has_value(), "parse valid jwt payload");
  if (!claims.has_value()) {
    return;
  }

  const auto account_id =
      chatgpt_device_auth::extract_account_id_from_claims(*claims);
  expect_true(account_id == std::optional<std::string>{"acct_123"},
              "extract account id from top-level claim");
}

void test_extract_account_id_falls_back_to_organizations() {
  chatgpt_device_auth::OAuthTokenResponse tokens{
      .access_token =
          "eyJhbGciOiJub25lIn0.eyJvcmdhbml6YXRpb25zIjpbeyJpZCI6Im9yZ18xIn1dfQ.",
      .refresh_token = "refresh",
      .id_token = "",
      .expires_in = 3600};

  const auto account_id = chatgpt_device_auth::extract_account_id(tokens);
  expect_true(account_id == std::optional<std::string>{"org_1"},
              "extract account id from organizations fallback");
}

} // namespace

int main() {
  test_parse_jwt_claims_and_extract_account_id();
  test_extract_account_id_falls_back_to_organizations();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All ChatGptDeviceAuth tests passed\n";
  return 0;
}
