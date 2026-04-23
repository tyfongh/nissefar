#include <HttpClient.h>

#include <curl/curl.h>

#include <cctype>
#include <format>
#include <string_view>

namespace http_json {
namespace {

struct CurlBuffer {
  Headers headers;
  std::string body;
  std::string content_type;
  long status{0};
};

const bool kCurlInitialized = [] {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  return true;
}();

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *buffer = static_cast<CurlBuffer *>(userdata);
  const auto bytes = size * nmemb;
  buffer->body.append(ptr, bytes);
  return bytes;
}

size_t header_callback(char *buffer, size_t size, size_t nmemb, void *userdata) {
  auto *result = static_cast<CurlBuffer *>(userdata);
  std::string_view header_line(buffer, size * nmemb);

  const auto colon = header_line.find(':');
  if (colon == std::string_view::npos) {
    return size * nmemb;
  }

  std::string name(header_line.substr(0, colon));
  std::string value(header_line.substr(colon + 1));
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.erase(value.begin());
  }
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
    value.pop_back();
  }

  result->headers.emplace_back(name, value);

  std::string lowercase_name;
  lowercase_name.reserve(name.size());
  for (const char ch : name) {
    lowercase_name.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  if (lowercase_name == "content-type") {
    result->content_type = value;
  }

  return size * nmemb;
}

Result finalize_result(CURL *curl, struct curl_slist *curl_headers,
                       CurlBuffer buffer, bool try_parse_json) {
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &buffer.status);
  if (buffer.content_type.empty()) {
    char *content_type = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type) == CURLE_OK &&
        content_type) {
      buffer.content_type = content_type;
    }
  }

  curl_slist_free_all(curl_headers);
  curl_easy_cleanup(curl);

  Response response{.status = static_cast<int>(buffer.status),
                    .content_type = std::move(buffer.content_type),
                    .headers = std::move(buffer.headers),
                    .body = std::move(buffer.body)};
  if (try_parse_json && !response.body.empty()) {
    try {
      response.json = Json::parse(response.body);
    } catch (const std::exception &e) {
      return {std::nullopt,
              std::format("Failed to parse JSON response: {}", e.what())};
    }
  }

  return {std::move(response), {}};
}

Result post_request(const std::string &host, const std::string &user_agent,
                    int timeout_seconds, const std::string &path,
                    const std::string &body, const std::string &content_type,
                    const Headers &headers, bool try_parse_json) {
  (void)kCurlInitialized;

  CURL *curl = curl_easy_init();
  if (!curl) {
    return {std::nullopt, "Failed to initialize libcurl."};
  }

  CurlBuffer buffer;
  struct curl_slist *curl_headers = nullptr;
  const std::string url = std::format("https://{}{}", host, path);

  for (const auto &[name, value] : headers) {
    curl_headers =
        curl_slist_append(curl_headers, std::format("{}: {}", name, value).c_str());
  }
  curl_headers = curl_slist_append(
      curl_headers, std::format("Content-Type: {}", content_type).c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(body.size()));
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &buffer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  const CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    curl_slist_free_all(curl_headers);
    curl_easy_cleanup(curl);
    return {std::nullopt,
            std::format("HTTP request failed: {}", curl_easy_strerror(code))};
  }

  return finalize_result(curl, curl_headers, std::move(buffer), try_parse_json);
}

Result post_json_request(const std::string &host, const std::string &user_agent,
                         int timeout_seconds, const std::string &path,
                         const Json &body, const Headers &headers,
                         bool try_parse_json) {
  return post_request(host, user_agent, timeout_seconds, path, body.dump(),
                      "application/json", headers, try_parse_json);
}

Result post_form_request(const std::string &host, const std::string &user_agent,
                         int timeout_seconds, const std::string &path,
                         const std::string &body, const Headers &headers) {
  return post_request(host, user_agent, timeout_seconds, path, body,
                      "application/x-www-form-urlencoded", headers, true);
}

} // namespace

Client::Client(std::string host, std::string user_agent, int timeout_seconds)
    : host_(std::move(host)), user_agent_(std::move(user_agent)),
      timeout_seconds_(timeout_seconds) {}

Result Client::post_json(const std::string &path, const Json &body,
                         const Headers &headers) const {
  return post_json_request(host_, user_agent_, timeout_seconds_, path, body, headers,
                           true);
}

Result Client::post_json_stream(const std::string &path, const Json &body,
                                const Headers &headers) const {
  return post_json_request(host_, user_agent_, timeout_seconds_, path, body, headers,
                           false);
}

Result Client::post_form(const std::string &path, const std::string &body,
                         const Headers &headers) const {
  return post_form_request(host_, user_agent_, timeout_seconds_, path, body,
                           headers);
}

} // namespace http_json
