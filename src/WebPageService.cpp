#include <HtmlTextExtract.h>
#include <HormuzStatus.h>
#include <WebPageService.h>
#include <UrlSafety.h>

#include <format>
#include <map>
#include <sstream>

namespace {

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
  const std::multimap<std::string, std::string> request_headers = {
      {"Accept",
       "text/markdown, text/html;q=0.9, text/plain;q=0.8, */*;q=0.1"}};

  for (int i = 0; i <= max_redirects; ++i) {
    response = co_await bot.co_request(current_url, dpp::m_get, "", "text/plain",
                                       request_headers);

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
    content_type = content_type_it->second;
  }

  const auto content_format =
      html_text_extract::classify_content_type(content_type);
  std::string title;
  if (content_format == html_text_extract::ContentFormat::Html) {
    title = html_text_extract::extract_title_from_html(body);
    if (title.size() > 300) {
      title.resize(300);
    }
  }
  std::string extracted_text =
      html_text_extract::prepare_webpage_text(body, content_format);

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
  switch (content_format) {
  case html_text_extract::ContentFormat::Html:
    out << "Content format: HTML-derived text\n";
    break;
  case html_text_extract::ContentFormat::Markdown:
    out << "Content format: Markdown\n";
    break;
  case html_text_extract::ContentFormat::PlainText:
    out << "Content format: Plain text\n";
    break;
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

dpp::task<std::string> WebPageService::fetch_hormuz_strait_status() const {
  constexpr const char *url = "https://hormuzstatus.com/";
  auto response = co_await bot.co_request(url, dpp::m_get);

  if (response.status != 200) {
    co_return std::format("Tool error: Hormuz status request failed with status {}.",
                          response.status);
  }

  co_return hormuz_status::parse_status_html(response.body, url);
}
