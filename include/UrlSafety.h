#ifndef URLSAFETY_H
#define URLSAFETY_H

#include <optional>
#include <string>

namespace url_safety {

struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::string port;
  std::string path;
};

std::optional<ParsedUrl> parse_http_url(const std::string &url);
bool is_allowed_port(const std::string &port);
bool is_blocked_host_name(const std::string &host);
bool is_literal_private_ip(const std::string &host);
bool host_resolves_to_private_network(const std::string &host);
std::string host_for_url(const std::string &host);

std::optional<std::string>
validate_public_http_url(const ParsedUrl &parsed_url);

std::optional<std::string> validate_public_http_url(const std::string &url,
                                                    ParsedUrl *parsed_url);

} // namespace url_safety

#endif // URLSAFETY_H
