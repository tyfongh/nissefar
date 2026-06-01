#include <HormuzStatus.h>

#include <Json.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <optional>
#include <regex>
#include <string>

namespace {

std::optional<std::string> extract_js_string_constant(const std::string &html,
                                                      const std::string &name) {
  const std::regex pattern("const\\s+" + name +
                           R"(\s*=\s*(["'])(.*?)\1\s*;)",
                           std::regex::ECMAScript);
  std::smatch match;
  if (!std::regex_search(html, match, pattern) || match.size() < 3) {
    return std::nullopt;
  }

  return match[2].str();
}

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string current_utc_iso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&now_time, &tm);

  char buffer[sizeof("YYYY-MM-DDTHH:MM:SSZ")];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

} // namespace

namespace hormuz_status {

std::string parse_status_html(const std::string &html,
                              const std::string &source_url) {
  const auto status = extract_js_string_constant(html, "STRAIT_STATUS");
  const auto answer = extract_js_string_constant(html, "STATUS_ANSWER");
  const auto detail = extract_js_string_constant(html, "STATUS_DETAIL");
  const auto label = extract_js_string_constant(html, "STATUS_LABEL");

  if (!status.has_value() || !answer.has_value()) {
    return "Tool error: could not find Hormuz status constants in page.";
  }

  const std::string normalized_status = to_lower(*status);
  const std::string normalized_answer = to_lower(*answer);
  const bool is_open = normalized_status == "open" || normalized_answer == "yes";

  Json payload = Json::object();
  payload["source"] = source_url;
  payload["status"] = *status;
  payload["answer"] = *answer;
  payload["is_open"] = is_open;
  payload["fetched_at"] = current_utc_iso8601();
  if (label.has_value()) {
    payload["label"] = *label;
  }
  if (detail.has_value()) {
    payload["detail"] = *detail;
  }

  return payload.dump();
}

} // namespace hormuz_status
