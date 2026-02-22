#include <WebPageService.h>

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cctype>
#include <format>
#include <netdb.h>
#include <optional>
#include <regex>
#include <sstream>

namespace {

struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::string port;
  std::string path;
};

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::optional<ParsedUrl> parse_url(const std::string &url) {
  static const std::regex url_re(
      R"(^\s*(https?)://([^/:\s?#]+)(?::([0-9]{1,5}))?([^\s#]*)?.*$)",
      std::regex::icase);
  std::smatch match;
  if (!std::regex_match(url, match, url_re)) {
    return std::nullopt;
  }

  ParsedUrl parsed{to_lower(match[1].str()), match[2].str(), match[3].str(),
                   match[4].str()};
  if (parsed.path.empty()) {
    parsed.path = "/";
  }

  if (parsed.port.empty()) {
    parsed.port = parsed.scheme == "https" ? "443" : "80";
  }

  return parsed;
}

bool is_blocked_host_name(const std::string &host) {
  const std::string host_lower = to_lower(host);
  if (host_lower == "localhost") {
    return true;
  }
  if (host_lower.size() >= 6 && host_lower.ends_with(".local")) {
    return true;
  }
  return false;
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
  if (IN6_IS_ADDR_LOOPBACK(&addr) || IN6_IS_ADDR_LINKLOCAL(&addr) ||
      IN6_IS_ADDR_MULTICAST(&addr)) {
    return true;
  }

  if ((addr.s6_addr[0] & 0xfe) == 0xfc) {
    return true;
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

std::string decode_entities(std::string text) {
  const std::array<std::pair<std::string, std::string>, 6> entities = {
      std::pair{"&amp;", "&"}, std::pair{"&lt;", "<"},
      std::pair{"&gt;", ">"},  std::pair{"&quot;", "\""},
      std::pair{"&#39;", "'"}, std::pair{"&nbsp;", " "}};

  for (const auto &[entity, value] : entities) {
    size_t pos = 0;
    while ((pos = text.find(entity, pos)) != std::string::npos) {
      text.replace(pos, entity.size(), value);
      pos += value.size();
    }
  }

  return text;
}

std::string strip_html(const std::string &html) {
  std::string text = html;
  text = std::regex_replace(text,
                            std::regex(R"(<script\b[^>]*>[\s\S]*?</script>)",
                                       std::regex::icase),
                            " ");
  text = std::regex_replace(text,
                            std::regex(R"(<style\b[^>]*>[\s\S]*?</style>)",
                                       std::regex::icase),
                            " ");
  text = std::regex_replace(text,
                            std::regex(
                                R"(<(nav|footer|header|form|svg)\b[^>]*>[\s\S]*?</\1>)",
                                std::regex::icase),
                            " ");
  text = std::regex_replace(text, std::regex(R"(<[^>]+>)"), " ");
  text = decode_entities(text);
  text = std::regex_replace(text, std::regex(R"(\s+)", std::regex::ECMAScript), " ");
  return text;
}

std::string extract_title(const std::string &html) {
  std::smatch match;
  if (std::regex_search(html, match,
                        std::regex(R"(<title[^>]*>([\s\S]*?)</title>)",
                                   std::regex::icase)) &&
      match.size() > 1) {
    std::string title = strip_html(match[1].str());
    if (title.size() > 300) {
      title.resize(300);
    }
    return title;
  }
  return "";
}

bool is_allowed_port(const std::string &port) {
  return port == "80" || port == "443";
}

std::string normalize_location_header(const std::string &location,
                                      const ParsedUrl &base_url) {
  if (location.starts_with("http://") || location.starts_with("https://")) {
    return location;
  }

  if (location.starts_with('/')) {
    return std::format("{}://{}:{}{}", base_url.scheme, base_url.host,
                       base_url.port, location);
  }

  return "";
}

} // namespace

WebPageService::WebPageService(dpp::cluster &bot) : bot(bot) {}

dpp::task<std::string>
WebPageService::fetch_webpage_text(const std::string &url) const {
  constexpr size_t max_response_bytes = 2 * 1024 * 1024;
  constexpr size_t max_output_chars = 12000;
  constexpr int max_redirects = 3;

  auto parsed = parse_url(url);
  if (!parsed.has_value()) {
    co_return "Tool error: invalid URL. Use an absolute http/https URL.";
  }

  if (!is_allowed_port(parsed->port)) {
    co_return "Tool error: blocked URL port. Only ports 80 and 443 are allowed.";
  }

  if (is_blocked_host_name(parsed->host) ||
      host_resolves_to_private_network(parsed->host)) {
    co_return "Tool error: blocked URL host (private or local network).";
  }

  std::string current_url = url;
  ParsedUrl current_parsed = *parsed;
  dpp::http_request_completion_t response{};

  for (int i = 0; i <= max_redirects; ++i) {
    response = co_await bot.co_request(current_url, dpp::m_get);

    if (response.status == 301 || response.status == 302 || response.status == 303 ||
        response.status == 307 || response.status == 308) {
      auto location_it = response.headers.find("location");
      if (location_it == response.headers.end()) {
        co_return "Tool error: redirect without location header.";
      }

      std::string redirected_url =
          normalize_location_header(location_it->second, current_parsed);
      if (redirected_url.empty()) {
        co_return "Tool error: unsupported redirect URL.";
      }

      auto redirected_parsed = parse_url(redirected_url);
      if (!redirected_parsed.has_value() ||
          !is_allowed_port(redirected_parsed->port) ||
          is_blocked_host_name(redirected_parsed->host) ||
          host_resolves_to_private_network(redirected_parsed->host)) {
        co_return "Tool error: blocked redirect target.";
      }

      current_url = redirected_url;
      current_parsed = *redirected_parsed;
      continue;
    }
    break;
  }

  if (response.status != 200) {
    co_return std::format("Tool error: webpage request failed with status {}.",
                          response.status);
  }

  std::string body = response.body;
  if (body.size() > max_response_bytes) {
    body.resize(max_response_bytes);
  }

  std::string content_type;
  auto content_type_it = response.headers.find("content-type");
  if (content_type_it != response.headers.end()) {
    content_type = to_lower(content_type_it->second);
  }

  std::string title;
  std::string extracted_text;
  if (content_type.find("text/html") != std::string::npos ||
      content_type.empty()) {
    title = extract_title(body);
    extracted_text = strip_html(body);
  } else {
    extracted_text = decode_entities(body);
    extracted_text = std::regex_replace(extracted_text, std::regex(R"(\s+)"), " ");
  }

  bool truncated = false;
  if (extracted_text.size() > max_output_chars) {
    extracted_text.resize(max_output_chars);
    truncated = true;
  }

  std::ostringstream out;
  out << "URL: " << current_url << "\n";
  if (!title.empty()) {
    out << "Title: " << title << "\n";
  }
  out << "Extracted text:\n" << extracted_text;
  if (truncated) {
    out << "\n[TRUNCATED]";
  }

  bot.log(dpp::ll_info,
          std::format("Fetched webpage text: url={} status={} output_bytes={}",
                      current_url, response.status, out.str().size()));

  co_return out.str();
}
