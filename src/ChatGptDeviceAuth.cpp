#include <ChatGptDeviceAuth.h>

#include <httplib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace chatgpt_device_auth {
namespace {

constexpr int kHttpTimeoutSeconds = 30;

std::string url_encode(const std::string &value) {
  std::string encoded;
  encoded.reserve(value.size() * 3);

  for (unsigned char ch : value) {
    if (std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' ||
        ch == '~') {
      encoded.push_back(static_cast<char>(ch));
      continue;
    }

    encoded += std::format("%{:02X}", static_cast<int>(ch));
  }

  return encoded;
}

void configure_auth_client(httplib::SSLClient &client) {
  client.set_connection_timeout(kHttpTimeoutSeconds);
  client.set_read_timeout(kHttpTimeoutSeconds);
  client.set_write_timeout(kHttpTimeoutSeconds);
  client.enable_server_certificate_verification(true);
  client.set_follow_location(true);
  client.set_default_headers(
      {{"User-Agent", "nissefar-chatgpt-auth/0.1"}});
}

std::optional<Json> parse_json_body(const httplib::Result &response,
                                    std::string &error) {
  if (!response) {
    error = std::format("HTTP request failed: {}",
                        httplib::to_string(response.error()));
    return std::nullopt;
  }

  try {
    return Json::parse(response->body);
  } catch (const std::exception &e) {
    error = std::format("Failed to parse JSON response: {}", e.what());
    return std::nullopt;
  }
}

std::string decode_base64url(std::string value) {
  for (char &ch : value) {
    if (ch == '-')
      ch = '+';
    else if (ch == '_')
      ch = '/';
  }

  while (value.size() % 4 != 0) {
    value.push_back('=');
  }

  static constexpr std::array<int, 256> kLookup = [] {
    std::array<int, 256> table{};
    table.fill(-1);
    for (int i = 'A'; i <= 'Z'; ++i)
      table[static_cast<std::size_t>(i)] = i - 'A';
    for (int i = 'a'; i <= 'z'; ++i)
      table[static_cast<std::size_t>(i)] = i - 'a' + 26;
    for (int i = '0'; i <= '9'; ++i)
      table[static_cast<std::size_t>(i)] = i - '0' + 52;
    table[static_cast<std::size_t>('+')] = 62;
    table[static_cast<std::size_t>('/')] = 63;
    return table;
  }();

  std::string decoded;
  decoded.reserve(value.size() * 3 / 4);

  for (std::size_t i = 0; i < value.size(); i += 4) {
    const char c0 = value[i];
    const char c1 = value[i + 1];
    const char c2 = value[i + 2];
    const char c3 = value[i + 3];

    const int a = c0 == '=' ? 0 : kLookup[static_cast<unsigned char>(c0)];
    const int b = c1 == '=' ? 0 : kLookup[static_cast<unsigned char>(c1)];
    const int c = c2 == '=' ? 0 : kLookup[static_cast<unsigned char>(c2)];
    const int d = c3 == '=' ? 0 : kLookup[static_cast<unsigned char>(c3)];

    if (a < 0 || b < 0 || (c2 != '=' && c < 0) || (c3 != '=' && d < 0)) {
      return {};
    }

    decoded.push_back(static_cast<char>((a << 2) | (b >> 4)));
    if (c2 != '=') {
      decoded.push_back(static_cast<char>(((b & 0xF) << 4) | (c >> 2)));
    }
    if (c3 != '=') {
      decoded.push_back(static_cast<char>(((c & 0x3) << 6) | d));
    }
  }

  return decoded;
}

} // namespace

DeviceAuthorizationResult Client::start_device_authorization() const {
  httplib::SSLClient client("auth.openai.com");
  configure_auth_client(client);

  const auto response = client.Post(
      "/api/accounts/deviceauth/usercode",
      std::format(R"({{"client_id":"{}"}})", kClientId),
      "application/json");

  if (!response) {
    return {std::nullopt,
            std::format("Failed to initiate device authorization: {}",
                        httplib::to_string(response.error()))};
  }
  if (response->status < 200 || response->status >= 300) {
    return {std::nullopt,
            std::format("Failed to initiate device authorization: HTTP {}",
                        response->status)};
  }

  std::string error;
  auto payload = parse_json_body(response, error);
  if (!payload.has_value()) {
    return {std::nullopt, std::move(error)};
  }

  if (!payload->contains("device_auth_id") ||
      !(*payload)["device_auth_id"].is_string() ||
      !payload->contains("user_code") || !(*payload)["user_code"].is_string()) {
    return {std::nullopt,
            "Device authorization response is missing required fields."};
  }

  int interval_seconds = 5;
  if (payload->contains("interval")) {
    if ((*payload)["interval"].is_string()) {
      try {
        interval_seconds =
            std::max(1, std::stoi((*payload)["interval"].get<std::string>()));
      } catch (...) {
      }
    } else if ((*payload)["interval"].is_number_integer()) {
      interval_seconds = std::max(1, (*payload)["interval"].get<int>());
    }
  }

  return {DeviceAuthorization{.device_auth_id =
                                  (*payload)["device_auth_id"].get<std::string>(),
                              .user_code =
                                  (*payload)["user_code"].get<std::string>(),
                              .interval_seconds = interval_seconds},
          {}};
}

DevicePollResultWrapper
Client::poll_device_authorization(const DeviceAuthorization &authorization) const {
  httplib::SSLClient client("auth.openai.com");
  configure_auth_client(client);

  Json body = {{"device_auth_id", authorization.device_auth_id},
               {"user_code", authorization.user_code}};
  const auto response = client.Post("/api/accounts/deviceauth/token",
                                    body.dump(), "application/json");

  if (!response) {
    return {std::nullopt,
            std::format("Failed to poll device authorization: {}",
                        httplib::to_string(response.error()))};
  }

  if (response->status == 403 || response->status == 404) {
    return {DevicePollResult{.state = DevicePollResult::State::Pending}, {}};
  }

  if (response->status < 200 || response->status >= 300) {
    return {std::nullopt,
            std::format("Device authorization polling failed: HTTP {}",
                        response->status)};
  }

  std::string error;
  auto payload = parse_json_body(response, error);
  if (!payload.has_value()) {
    return {std::nullopt, std::move(error)};
  }

  if (!payload->contains("authorization_code") ||
      !(*payload)["authorization_code"].is_string() ||
      !payload->contains("code_verifier") ||
      !(*payload)["code_verifier"].is_string()) {
    return {std::nullopt,
            "Device authorization success response is missing required fields."};
  }

  return {DevicePollResult{.state = DevicePollResult::State::Authorized,
                           .authorization_code =
                               (*payload)["authorization_code"].get<std::string>(),
                           .code_verifier =
                               (*payload)["code_verifier"].get<std::string>()},
          {}};
}

OAuthTokenResult Client::exchange_authorization_code(
    const std::string &authorization_code,
    const std::string &code_verifier) const {
  httplib::SSLClient client("auth.openai.com");
  configure_auth_client(client);

  const std::string body =
      std::format("grant_type=authorization_code&code={}&redirect_uri={}&client_id={}&code_verifier={}",
                  url_encode(authorization_code), url_encode(kDeviceCallbackUrl),
                  url_encode(kClientId), url_encode(code_verifier));
  const auto response = client.Post("/oauth/token", body,
                                    "application/x-www-form-urlencoded");

  if (!response) {
    return {std::nullopt,
            std::format("Token exchange failed: {}",
                        httplib::to_string(response.error()))};
  }
  if (response->status < 200 || response->status >= 300) {
    return {std::nullopt,
            std::format("Token exchange failed: HTTP {}", response->status)};
  }

  std::string error;
  auto payload = parse_json_body(response, error);
  if (!payload.has_value()) {
    return {std::nullopt, std::move(error)};
  }

  if (!payload->contains("access_token") ||
      !(*payload)["access_token"].is_string() ||
      !payload->contains("refresh_token") ||
      !(*payload)["refresh_token"].is_string()) {
    return {std::nullopt, "Token exchange response is missing required fields."};
  }

  int expires_in = 3600;
  if (payload->contains("expires_in") && (*payload)["expires_in"].is_number()) {
    expires_in = (*payload)["expires_in"].get<int>();
  }

  OAuthTokenResponse tokens{.access_token =
                                (*payload)["access_token"].get<std::string>(),
                            .refresh_token =
                                (*payload)["refresh_token"].get<std::string>(),
                            .id_token = payload->contains("id_token") &&
                                                (*payload)["id_token"].is_string()
                                            ? (*payload)["id_token"].get<std::string>()
                                            : std::string(),
                            .expires_in = expires_in};
  return {tokens, {}};
}

std::optional<Json> parse_jwt_claims(const std::string &token) {
  const auto first_dot = token.find('.');
  if (first_dot == std::string::npos) {
    return std::nullopt;
  }
  const auto second_dot = token.find('.', first_dot + 1);
  if (second_dot == std::string::npos) {
    return std::nullopt;
  }

  const std::string payload =
      decode_base64url(token.substr(first_dot + 1, second_dot - first_dot - 1));
  if (payload.empty()) {
    return std::nullopt;
  }

  try {
    return Json::parse(payload);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> extract_account_id_from_claims(const Json &claims) {
  if (claims.contains("chatgpt_account_id") &&
      claims["chatgpt_account_id"].is_string()) {
    return claims["chatgpt_account_id"].get<std::string>();
  }

  if (claims.contains("https://api.openai.com/auth") &&
      claims["https://api.openai.com/auth"].is_object()) {
    const auto &nested = claims["https://api.openai.com/auth"];
    if (nested.contains("chatgpt_account_id") &&
        nested["chatgpt_account_id"].is_string()) {
      return nested["chatgpt_account_id"].get<std::string>();
    }
  }

  if (claims.contains("organizations") && claims["organizations"].is_array() &&
      !claims["organizations"].empty() && claims["organizations"][0].is_object() &&
      claims["organizations"][0].contains("id") &&
      claims["organizations"][0]["id"].is_string()) {
    return claims["organizations"][0]["id"].get<std::string>();
  }

  return std::nullopt;
}

std::optional<std::string> extract_account_id(const OAuthTokenResponse &tokens) {
  if (!tokens.id_token.empty()) {
    const auto claims = parse_jwt_claims(tokens.id_token);
    if (claims.has_value()) {
      const auto account_id = extract_account_id_from_claims(*claims);
      if (account_id.has_value()) {
        return account_id;
      }
    }
  }

  if (!tokens.access_token.empty()) {
    const auto claims = parse_jwt_claims(tokens.access_token);
    if (claims.has_value()) {
      return extract_account_id_from_claims(*claims);
    }
  }

  return std::nullopt;
}

} // namespace chatgpt_device_auth
