#include <HtmlTextExtract.h>
#include <WebPageService.h>
#include <UrlSafety.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <sstream>

namespace {

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string normalize_location_header(const std::string &location,
                                      const url_safety::ParsedUrl &base_url) {
  if (location.starts_with("http://") || location.starts_with("https://")) {
    return location;
  }

  if (location.starts_with('/')) {
    return std::format("{}://{}:{}{}", base_url.scheme,
                       url_safety::host_for_url(base_url.host), base_url.port,
                       location);
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

  url_safety::ParsedUrl parsed;
  if (const auto validation_error =
          url_safety::validate_public_http_url(url, &parsed);
      validation_error.has_value()) {
    co_return *validation_error;
  }

  std::string current_url = url;
  url_safety::ParsedUrl current_parsed = parsed;
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

      auto redirected_parsed = url_safety::parse_http_url(redirected_url);
      if (!redirected_parsed.has_value()) {
        co_return "Tool error: blocked redirect target.";
      }

      if (const auto validation_error =
              url_safety::validate_public_http_url(*redirected_parsed);
          validation_error.has_value()) {
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
    title = html_text_extract::extract_title_from_html(body);
    if (title.size() > 300) {
      title.resize(300);
    }
    extracted_text = html_text_extract::extract_text_from_html(body);
  } else {
    extracted_text = html_text_extract::normalize_plain_text(body);
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
