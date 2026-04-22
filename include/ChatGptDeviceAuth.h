#ifndef CHATGPTDEVICEAUTH_H
#define CHATGPTDEVICEAUTH_H

#include <Json.h>

#include <optional>
#include <string>

namespace chatgpt_device_auth {

inline constexpr const char *kClientId = "app_EMoamEEZ73f0CkXaXp7hrann";
inline constexpr const char *kIssuer = "https://auth.openai.com";
inline constexpr const char *kDeviceVerificationUrl =
    "https://auth.openai.com/codex/device";
inline constexpr const char *kDeviceCallbackUrl =
    "https://auth.openai.com/deviceauth/callback";

struct DeviceAuthorization {
  std::string device_auth_id;
  std::string user_code;
  int interval_seconds{5};
};

struct DevicePollResult {
  enum class State { Pending, Authorized, Failed };

  State state{State::Failed};
  std::string authorization_code;
  std::string code_verifier;
  std::string error;
};

struct OAuthTokenResponse {
  std::string access_token;
  std::string refresh_token;
  std::string id_token;
  int expires_in{3600};
};

struct DeviceAuthorizationResult {
  std::optional<DeviceAuthorization> authorization;
  std::string error;

  [[nodiscard]] bool ok() const { return authorization.has_value(); }
};

struct DevicePollResultWrapper {
  std::optional<DevicePollResult> result;
  std::string error;

  [[nodiscard]] bool ok() const { return result.has_value(); }
};

struct OAuthTokenResult {
  std::optional<OAuthTokenResponse> tokens;
  std::string error;

  [[nodiscard]] bool ok() const { return tokens.has_value(); }
};

class Client {
public:
  DeviceAuthorizationResult start_device_authorization() const;
  DevicePollResultWrapper
  poll_device_authorization(const DeviceAuthorization &authorization) const;
  OAuthTokenResult exchange_authorization_code(
      const std::string &authorization_code,
      const std::string &code_verifier) const;
  OAuthTokenResult refresh_access_token(const std::string &refresh_token) const;
};

std::optional<Json> parse_jwt_claims(const std::string &token);
std::optional<std::string> extract_account_id_from_claims(const Json &claims);
std::optional<std::string> extract_account_id(const OAuthTokenResponse &tokens);

} // namespace chatgpt_device_auth

#endif // CHATGPTDEVICEAUTH_H
