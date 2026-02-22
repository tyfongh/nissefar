#ifndef OLLAMA_TOOL_CALLING_H
#define OLLAMA_TOOL_CALLING_H

#include <ollama.hpp>
#include <string>
#include <utility>
#include <vector>

namespace ollama_tools {

using tools = std::vector<ollama::json>;

inline ollama::json make_function_tool(const std::string &name,
                                       const std::string &description,
                                       const ollama::json &parameters) {
  ollama::json function = ollama::json::object();
  function["name"] = name;
  function["description"] = description;
  function["parameters"] = parameters;

  ollama::json tool = ollama::json::object();
  tool["type"] = "function";
  tool["function"] = std::move(function);
  return tool;
}

inline ollama::request
make_chat_request(const std::string &model, const ollama::messages &messages,
                  const ollama::options &options, const tools &available_tools,
                  bool stream = false,
                  const std::string &keep_alive_duration = "5m") {
  ollama::request request(model, messages, options, stream, "json",
                          keep_alive_duration);
  if (!available_tools.empty())
    request["tools"] = available_tools;
  return request;
}

inline ollama::response chat(Ollama &client, const std::string &model,
                             const ollama::messages &messages,
                             const ollama::options &options,
                             const tools &available_tools,
                             const std::string &keep_alive_duration = "5m") {
  ollama::request request =
      make_chat_request(model, messages, options, available_tools, false,
                        keep_alive_duration);
  return client.chat(request);
}

inline bool chat(Ollama &client, const std::string &model,
                 const ollama::messages &messages,
                 std::function<bool(const ollama::response &)> on_receive_token,
                 const ollama::options &options, const tools &available_tools,
                 const std::string &keep_alive_duration = "5m") {
  ollama::request request =
      make_chat_request(model, messages, options, available_tools, true,
                        keep_alive_duration);
  return client.chat(request, std::move(on_receive_token));
}

inline bool has_tool_calls(const ollama::response &response) {
  const auto &payload = response.as_json();
  return payload.contains("message") && payload["message"].is_object() &&
         payload["message"].contains("tool_calls") &&
         payload["message"]["tool_calls"].is_array() &&
         !payload["message"]["tool_calls"].empty();
}

inline ollama::json tool_calls(const ollama::response &response) {
  if (!has_tool_calls(response))
    return ollama::json::array();
  return response.as_json()["message"]["tool_calls"];
}

inline ollama::message assistant_message(const ollama::response &response) {
  ollama::message message;
  const auto &payload = response.as_json();
  if (!payload.contains("message") || !payload["message"].is_object())
    return message;

  const auto &server_message = payload["message"];
  for (auto it = server_message.begin(); it != server_message.end(); ++it)
    message[it.key()] = it.value();

  return message;
}

inline ollama::message tool_result_message(const std::string &tool_name,
                                           const std::string &content) {
  ollama::message message("tool", content);
  message["tool_name"] = tool_name;
  return message;
}

} // namespace ollama_tools

#endif // OLLAMA_TOOL_CALLING_H
