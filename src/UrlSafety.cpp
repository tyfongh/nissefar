#include <UrlSafety.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstring>
#include <format>
#include <netdb.h>
#include <regex>

namespace {

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string trim_trailing_dots(std::string value) {
  while (!value.empty() && value.back() == '.') {
    value.pop_back();
  }
  return value;
}

bool is_ascii(const std::string &value) {
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return c <= 0x7f;
  });
}

bool is_valid_dns_label(const std::string &label) {
  if (label.empty()) {
    return false;
  }

  if (!std::isalnum(static_cast<unsigned char>(label.front())) ||
      !std::isalnum(static_cast<unsigned char>(label.back()))) {
    return false;
  }

  return std::all_of(label.begin(), label.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-';
  });
}

bool is_valid_dns_host(const std::string &host) {
  if (host.empty()) {
    return false;
  }

  if (!is_ascii(host)) {
    return false;
  }

  if (host.find('%') != std::string::npos || host.find('[') != std::string::npos ||
      host.find(']') != std::string::npos) {
    return false;
  }

  if (host.starts_with('.') || host.ends_with('.') || host.find("..") != std::string::npos) {
    return false;
  }

  std::size_t start = 0;
  while (start < host.size()) {
    const std::size_t dot = host.find('.', start);
    const std::size_t end = (dot == std::string::npos) ? host.size() : dot;
    if (!is_valid_dns_label(host.substr(start, end - start))) {
      return false;
    }
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }

  return true;
}

bool is_private_ipv4(uint32_t host_order_ip) {
  if ((host_order_ip & 0xff000000U) == 0x0a000000U) {
    return true;
  }
  if ((host_order_ip & 0xfff00000U) == 0xac100000U) {
    return true;
  }
  if ((host_order_ip & 0xffff0000U) == 0xc0a80000U) {
    return true;
  }
  if ((host_order_ip & 0xff000000U) == 0x7f000000U) {
    return true;
  }
  if ((host_order_ip & 0xffff0000U) == 0xa9fe0000U) {
    return true;
  }
  if ((host_order_ip & 0xffc00000U) == 0x64400000U) {
    return true;
  }
  if ((host_order_ip & 0xf0000000U) == 0xe0000000U) {
    return true;
  }
  if ((host_order_ip & 0xf0000000U) == 0xf0000000U) {
    return true;
  }
  if ((host_order_ip & 0xff000000U) == 0x00000000U) {
    return true;
  }
  return false;
}

bool is_private_ipv6(const in6_addr &addr) {
  if (IN6_IS_ADDR_UNSPECIFIED(&addr) || IN6_IS_ADDR_LOOPBACK(&addr) ||
      IN6_IS_ADDR_LINKLOCAL(&addr) ||
      IN6_IS_ADDR_MULTICAST(&addr)) {
    return true;
  }

  if ((addr.s6_addr[0] & 0xfe) == 0xfc) {
    return true;
  }

  if (IN6_IS_ADDR_V4MAPPED(&addr)) {
    uint32_t mapped_ipv4 = 0;
    std::memcpy(&mapped_ipv4, &addr.s6_addr[12], sizeof(mapped_ipv4));
    if (is_private_ipv4(ntohl(mapped_ipv4))) {
      return true;
    }
  }

  return false;
}

bool is_valid_host_syntax(const std::string &host) {
  if (!is_ascii(host)) {
    return false;
  }

  in_addr ipv4_addr{};
  if (inet_pton(AF_INET, host.c_str(), &ipv4_addr) == 1) {
    return true;
  }

  in6_addr ipv6_addr{};
  if (inet_pton(AF_INET6, host.c_str(), &ipv6_addr) == 1) {
    return true;
  }

  return is_valid_dns_host(host);
}

} // namespace

namespace url_safety {

std::optional<ParsedUrl> parse_http_url(const std::string &url) {
  static const std::regex url_re(
      R"(^\s*(https?)://(\[[0-9A-Fa-f:%.]+\]|[^/:\s?#]+)(?::([0-9]{1,5}))?([^\s#]*)?.*$)",
      std::regex::icase);

  std::smatch match;
  if (!std::regex_match(url, match, url_re)) {
    return std::nullopt;
  }

  ParsedUrl parsed{};
  parsed.scheme = to_lower(match[1].str());
  parsed.host = match[2].str();
  parsed.port = match[3].str();
  parsed.path = match[4].str();

  if (parsed.host.size() >= 2 && parsed.host.front() == '[' &&
      parsed.host.back() == ']') {
    parsed.host = parsed.host.substr(1, parsed.host.size() - 2);
  }

  if (parsed.path.empty()) {
    parsed.path = "/";
  }

  if (parsed.port.empty()) {
    parsed.port = parsed.scheme == "https" ? "443" : "80";
  }

  return parsed;
}

bool is_allowed_port(const std::string &port) {
  return port == "80" || port == "443";
}

bool is_blocked_host_name(const std::string &host) {
  const std::string host_lower = to_lower(trim_trailing_dots(host));
  if (host_lower == "localhost") {
    return true;
  }
  if (host_lower.size() >= 6 && host_lower.ends_with(".local")) {
    return true;
  }
  return false;
}

bool is_literal_private_ip(const std::string &host) {
  in_addr ipv4_addr{};
  if (inet_pton(AF_INET, host.c_str(), &ipv4_addr) == 1) {
    return is_private_ipv4(ntohl(ipv4_addr.s_addr));
  }

  in6_addr ipv6_addr{};
  if (inet_pton(AF_INET6, host.c_str(), &ipv6_addr) == 1) {
    return is_private_ipv6(ipv6_addr);
  }

  return false;
}

bool host_resolves_to_private_network(const std::string &host) {
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  addrinfo *results = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &results) != 0) {
    return true;
  }

  bool blocked = false;
  for (addrinfo *entry = results; entry != nullptr; entry = entry->ai_next) {
    if (entry->ai_family == AF_INET) {
      auto *addr = reinterpret_cast<sockaddr_in *>(entry->ai_addr);
      uint32_t ip = ntohl(addr->sin_addr.s_addr);
      if (is_private_ipv4(ip)) {
        blocked = true;
        break;
      }
    } else if (entry->ai_family == AF_INET6) {
      auto *addr = reinterpret_cast<sockaddr_in6 *>(entry->ai_addr);
      if (is_private_ipv6(addr->sin6_addr)) {
        blocked = true;
        break;
      }
    }
  }

  freeaddrinfo(results);
  return blocked;
}

std::string host_for_url(const std::string &host) {
  if (host.find(':') != std::string::npos &&
      !(host.starts_with('[') && host.ends_with(']'))) {
    return std::format("[{}]", host);
  }
  return host;
}

std::optional<std::string> validate_public_http_url(const ParsedUrl &parsed_url) {
  if (!is_valid_host_syntax(parsed_url.host)) {
    return "Tool error: invalid URL host.";
  }

  if (!is_allowed_port(parsed_url.port)) {
    return "Tool error: blocked URL port. Only ports 80 and 443 are allowed.";
  }

  if (is_blocked_host_name(parsed_url.host) ||
      is_literal_private_ip(parsed_url.host) ||
      host_resolves_to_private_network(parsed_url.host)) {
    return "Tool error: blocked URL host (private or local network).";
  }

  return std::nullopt;
}

std::optional<std::string> validate_public_http_url(const std::string &url,
                                                    ParsedUrl *parsed_url) {
  const auto parsed = parse_http_url(url);
  if (!parsed.has_value()) {
    return "Tool error: invalid URL. Use an absolute http/https URL.";
  }

  if (parsed_url != nullptr) {
    *parsed_url = *parsed;
  }

  return validate_public_http_url(*parsed);
}

} // namespace url_safety
