#ifndef CODEXCLIENT_H
#define CODEXCLIENT_H

#include <ChatGptAuth.h>
#include <HttpClient.h>
#include <LlmTypes.h>

#include <optional>
#include <string>

struct CodexRequest {
  std::string model;
  std::string instructions;
  std::vector<LlmMessage> messages;
  Json input_items = Json::array();
  Json tools = Json::array();
  Json tool_choice = Json();
  bool stream{true};
};

struct CodexResponseResult {
  std::optional<LlmResponse> response;
  std::string error;
  int status{0};
  std::string content_type;
  std::string raw_body;

  [[nodiscard]] bool ok() const { return response.has_value(); }
};

class CodexClient {
public:
  explicit CodexClient(std::string user_agent = "nissefar/0.1");

  CodexResponseResult create_response(const ChatGptAuth &auth,
                                      const CodexRequest &request) const;

  static Json build_request_json(const CodexRequest &request);
  static CodexResponseResult parse_response_json(const Json &payload);
  static CodexResponseResult parse_stream_body(const std::string &body);

private:
  std::string session_id_;
  http_json::Client http_client_;
};

#endif // CODEXCLIENT_H
