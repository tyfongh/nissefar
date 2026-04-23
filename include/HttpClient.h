#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <Json.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace http_json {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Response {
  int status{0};
  std::string content_type;
  Headers headers;
  std::string body;
  std::optional<Json> json;
};

struct Result {
  std::optional<Response> response;
  std::string error;

  [[nodiscard]] bool ok() const { return response.has_value(); }
};

class Client {
public:
  explicit Client(std::string host, std::string user_agent = "nissefar/0.1",
                  int timeout_seconds = 30);

  Result post_json(const std::string &path, const Json &body,
                   const Headers &headers = {}) const;
  Result post_json_stream(const std::string &path, const Json &body,
                          const Headers &headers = {}) const;
  Result post_form(const std::string &path, const std::string &body,
                   const Headers &headers = {}) const;

private:
  std::string host_;
  std::string user_agent_;
  int timeout_seconds_;
};

} // namespace http_json

#endif // HTTPCLIENT_H
