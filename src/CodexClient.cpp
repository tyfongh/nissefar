#include <CodexClient.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <sstream>

namespace {

constexpr const char *kCodexHost = "chatgpt.com";
constexpr const char *kCodexResponsesPath = "/backend-api/codex/responses";

std::string make_session_id() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::format("nissefar-{}",
                     std::chrono::duration_cast<std::chrono::milliseconds>(now)
                         .count());
}

std::string extract_error_message(const Json &payload) {
  if (payload.contains("error")) {
    const auto &error = payload["error"];
    if (error.is_string()) {
      return error.get<std::string>();
    }
    if (error.is_object()) {
      if (error.contains("message") && error["message"].is_string()) {
        return error["message"].get<std::string>();
      }
      return error.dump();
    }
  }

  return payload.dump();
}

std::optional<std::string> get_header_value(const http_json::Response &response,
                                            const std::string &name) {
  for (const auto &[key, value] : response.headers) {
    if (key == name) {
      return value;
    }
  }
  return std::nullopt;
}

std::string usage_limit_summary(const http_json::Response &response) {
  const auto used_percent = get_header_value(response, "x-codex-primary-used-percent");
  const auto reset_after =
      get_header_value(response, "x-codex-primary-reset-after-seconds");
  const auto plan_type = get_header_value(response, "x-codex-plan-type");
  const auto active_limit = get_header_value(response, "x-codex-active-limit");

  if (!used_percent.has_value() && !reset_after.has_value() &&
      !plan_type.has_value() && !active_limit.has_value()) {
    return {};
  }

  return std::format(
      " usage_limit(plan={} active_limit={} used_percent={} reset_after_seconds={})",
      plan_type.value_or("unknown"), active_limit.value_or("unknown"),
      used_percent.value_or("unknown"), reset_after.value_or("unknown"));
}

CodexResponseResult with_http_metadata(CodexResponseResult result,
                                       const http_json::Response &response) {
  result.status = response.status;
  result.content_type = response.content_type;
  result.raw_body = response.body;
  return result;
}

std::vector<std::string> split_sse_events(const std::string &body) {
  std::vector<std::string> events;
  std::string current;
  std::istringstream stream(body);
  std::string line;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty()) {
      if (!current.empty()) {
        events.push_back(current);
        current.clear();
      }
      continue;
    }

    current += line;
    current.push_back('\n');
  }

  if (!current.empty()) {
    events.push_back(current);
  }

  return events;
}

std::optional<std::string> extract_sse_data(const std::string &event) {
  std::istringstream stream(event);
  std::string line;
  std::string data;
  bool found = false;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.starts_with("data:")) {
      continue;
    }

    std::string value = line.substr(5);
    if (!value.empty() && value.front() == ' ') {
      value.erase(value.begin());
    }
    if (found) {
      data.push_back('\n');
    }
    data += value;
    found = true;
  }

  if (!found) {
    return std::nullopt;
  }
  return data;
}

std::optional<Json> parse_event_json(const std::string &data) {
  if (data.empty() || data == "[DONE]") {
    return std::nullopt;
  }

  try {
    return Json::parse(data);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<Json> parse_tool_arguments(const Json &value) {
  if (value.is_object() || value.is_array()) {
    return value;
  }
  if (value.is_string()) {
    try {
      return Json::parse(value.get<std::string>());
    } catch (...) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

bool is_supported_image_mime_type(const std::string &mime_type) {
  return mime_type == "image/jpeg" || mime_type == "image/png" ||
         mime_type == "image/webp";
}

std::optional<std::string> image_to_data_url(const LlmImage &image) {
  if (!is_supported_image_mime_type(image.mime_type) || image.base64_data.empty()) {
    return std::nullopt;
  }

  return std::format("data:{};base64,{}", image.mime_type, image.base64_data);
}

void append_text_part(const Json &part, std::string &text, bool &found_text) {
  if (!part.is_object() || !part.contains("type") || !part["type"].is_string()) {
    return;
  }

  const auto &type = part["type"].get_ref<const std::string &>();
  if ((type == "output_text" || type == "text" || type == "refusal") &&
      part.contains("text") && part["text"].is_string()) {
    text += part["text"].get<std::string>();
    found_text = true;
  }
}

void append_message_text(const Json &item, std::string &text, bool &found_text) {
  if (!item.contains("content")) {
    return;
  }

  const auto &content = item["content"];
  if (content.is_string()) {
    text += content.get<std::string>();
    found_text = true;
    return;
  }

  if (!content.is_array()) {
    return;
  }

  for (const auto &part : content) {
    append_text_part(part, text, found_text);
  }
}

std::optional<LlmToolCall> parse_tool_call(const Json &item) {
  if (!item.is_object() || !item.contains("type") || !item["type"].is_string()) {
    return std::nullopt;
  }

  const auto &type = item["type"].get_ref<const std::string &>();
  if (type != "function_call" && type != "custom_tool_call") {
    return std::nullopt;
  }

  if (!item.contains("name") || !item["name"].is_string()) {
    return std::nullopt;
  }

  const Json *arguments_value = nullptr;
  if (item.contains("arguments")) {
    arguments_value = &item["arguments"];
  } else if (item.contains("input")) {
    arguments_value = &item["input"];
  }

  if (!arguments_value) {
    return std::nullopt;
  }

  const auto arguments = parse_tool_arguments(*arguments_value);
  if (!arguments.has_value()) {
    return std::nullopt;
  }

  std::string call_id;
  if (item.contains("call_id") && item["call_id"].is_string()) {
    call_id = item["call_id"].get<std::string>();
  } else if (item.contains("id") && item["id"].is_string()) {
    call_id = item["id"].get<std::string>();
  }

  return LlmToolCall{.call_id = std::move(call_id),
                     .name = item["name"].get<std::string>(),
                     .arguments = *arguments};
}

std::optional<LlmGeneratedImage> parse_generated_image(const Json &item) {
  if (!item.is_object() || !item.contains("type") || !item["type"].is_string() ||
      item["type"] != "image_generation_call") {
    return std::nullopt;
  }

  if (!item.contains("result") || !item["result"].is_string() ||
      item["result"].get<std::string>().empty()) {
    return std::nullopt;
  }

  std::string output_format = "png";
  if (item.contains("output_format") && item["output_format"].is_string() &&
      !item["output_format"].get<std::string>().empty()) {
    output_format = item["output_format"].get<std::string>();
  }

  std::string mime_type = "image/png";
  if (output_format == "jpeg" || output_format == "jpg") {
    mime_type = "image/jpeg";
  } else if (output_format == "webp") {
    mime_type = "image/webp";
  }

  LlmGeneratedImage image{.mime_type = std::move(mime_type),
                          .base64_data = item["result"].get<std::string>()};
  if (item.contains("id") && item["id"].is_string()) {
    image.id = item["id"].get<std::string>();
  }
  if (item.contains("revised_prompt") && item["revised_prompt"].is_string()) {
    image.revised_prompt = item["revised_prompt"].get<std::string>();
  }
  return image;
}

} // namespace

CodexClient::CodexClient(std::string user_agent)
    : session_id_(make_session_id()),
      http_client_(kCodexHost, std::move(user_agent), 360) {}

CodexResponseResult CodexClient::create_response(const ChatGptAuth &auth,
                                                 const CodexRequest &request) const {
  http_json::Headers headers{{"Authorization", std::format("Bearer {}", auth.access)},
                              {"originator", "nissefar"},
                              {"session_id", session_id_}};
  if (request.stream) {
    headers.emplace_back("Accept", "text/event-stream");
  } else {
    headers.emplace_back("Accept", "application/json");
  }
  if (auth.account_id.has_value() && !auth.account_id->empty()) {
    headers.emplace_back("ChatGPT-Account-Id", *auth.account_id);
  }

  const auto result = request.stream
                          ? http_client_.post_json_stream(kCodexResponsesPath,
                                                          build_request_json(request), headers)
                          : http_client_.post_json(kCodexResponsesPath,
                                                   build_request_json(request), headers);
  if (!result.ok()) {
    return {std::nullopt, result.error};
  }

  const auto &response = *result.response;
  if (response.status < 200 || response.status >= 300) {
    if (response.json.has_value()) {
      return with_http_metadata(
          {std::nullopt,
           std::format("Codex request failed: HTTP {}: {}{}", response.status,
                       extract_error_message(*response.json),
                       usage_limit_summary(response))},
          response);
    }
    return with_http_metadata(
        {std::nullopt,
         std::format("Codex request failed: HTTP {}{}", response.status,
                     usage_limit_summary(response))},
        response);
  }

  const bool looks_like_sse =
      response.content_type.find("text/event-stream") != std::string::npos ||
      response.body.starts_with("event:") || response.body.starts_with("data:");
  if (looks_like_sse) {
    return with_http_metadata(parse_stream_body(response.body), response);
  }

  if (!response.json.has_value()) {
    return with_http_metadata(
        {std::nullopt, "Codex response did not contain JSON."}, response);
  }

  return with_http_metadata(parse_response_json(*response.json), response);
}

Json CodexClient::build_request_json(const CodexRequest &request) {
  Json payload{{"model", request.model},
               {"input", Json::array()},
               {"stream", request.stream},
               {"store", false}};
  if (!request.instructions.empty()) {
    payload["instructions"] = request.instructions;
  }

  for (const auto &message : request.messages) {
    Json item{{"role", message.role}};
    if (message.images.empty()) {
      item["content"] = message.content;
    } else {
      Json content = Json::array();
      std::size_t valid_image_count = 0;
      if (!message.content.empty()) {
        content.push_back({{"type", "input_text"}, {"text", message.content}});
      }
      for (const auto &image : message.images) {
        const auto data_url = image_to_data_url(image);
        if (!data_url.has_value()) {
          continue;
        }
        ++valid_image_count;
        content.push_back({{"type", "input_image"}, {"image_url", *data_url}});
      }
      if (valid_image_count == 0) {
        item["content"] = message.content;
      } else {
        item["content"] = std::move(content);
      }
    }
    payload["input"].push_back(std::move(item));
  }

  if (request.input_items.is_array()) {
    for (const auto &item : request.input_items) {
      payload["input"].push_back(item);
    }
  }

  if (request.tools.is_array() && !request.tools.empty()) {
    payload["tools"] = request.tools;
  }

  if (!request.tool_choice.is_null() && !request.tool_choice.empty()) {
    payload["tool_choice"] = request.tool_choice;
  }

  return payload;
}

CodexResponseResult CodexClient::parse_response_json(const Json &payload) {
  if (payload.contains("error") && !payload["error"].is_null()) {
    return {std::nullopt,
            std::format("Codex API returned an error: {}",
                        extract_error_message(payload))};
  }

  LlmResponse parsed;
  bool found_text = false;
  const bool has_top_level_output_text =
      payload.contains("output_text") && payload["output_text"].is_string();

  if (has_top_level_output_text) {
    parsed.text = payload["output_text"].get<std::string>();
    found_text = true;
  }

  if (payload.contains("output") && payload["output"].is_array()) {
    parsed.output_items = payload["output"];
    for (const auto &item : payload["output"]) {
      if (!item.is_object()) {
        continue;
      }

      if (const auto tool_call = parse_tool_call(item); tool_call.has_value()) {
        parsed.tool_calls.push_back(*tool_call);
        continue;
      }

      if (const auto generated_image = parse_generated_image(item);
          generated_image.has_value()) {
        parsed.generated_images.push_back(*generated_image);
        continue;
      }

      if (!has_top_level_output_text && item.contains("type") && item["type"].is_string() &&
          item["type"] == "message") {
        append_message_text(item, parsed.text, found_text);
      }
    }
  }

  if (!found_text && parsed.tool_calls.empty() && parsed.generated_images.empty()) {
    return {std::nullopt,
            "Codex response did not contain text output, tool calls, or generated images."};
  }

  return {parsed, {}};
}

CodexResponseResult CodexClient::parse_stream_body(const std::string &body) {
  if (body.empty()) {
    return {std::nullopt, "Codex stream response was empty."};
  }

  Json response_payload;
  Json output_items = Json::array();
  std::string output_text;

  for (const auto &event_text : split_sse_events(body)) {
    const auto data = extract_sse_data(event_text);
    if (!data.has_value()) {
      continue;
    }

    const auto event = parse_event_json(*data);
    if (!event.has_value() || !event->is_object()) {
      continue;
    }

    if (event->contains("type") && (*event)["type"].is_string()) {
      const auto &type = (*event)["type"].get_ref<const std::string &>();

      if (type == "response.completed" && event->contains("response") &&
          (*event)["response"].is_object()) {
        response_payload = (*event)["response"];
        break;
      }

      if ((type == "response.failed" || type == "response.error") &&
          event->contains("response") && (*event)["response"].is_object()) {
        return parse_response_json((*event)["response"]);
      }

      if ((type == "response.failed" || type == "response.error") &&
          event->contains("error")) {
        return {std::nullopt,
                std::format("Codex API returned an error: {}",
                            extract_error_message(*event))};
      }

      if (type == "response.output_item.done" && event->contains("item") &&
          (*event)["item"].is_object()) {
        output_items.push_back((*event)["item"]);
      }

      if (type == "response.output_text.delta" && event->contains("delta") &&
          (*event)["delta"].is_string()) {
        output_text += (*event)["delta"].get<std::string>();
      }

      if (type == "response.output_text.done" && event->contains("text") &&
          (*event)["text"].is_string() && output_text.empty()) {
        output_text = (*event)["text"].get<std::string>();
      }
    }
  }

  if (!response_payload.is_object()) {
    response_payload = Json::object();
  }

  if (!output_items.empty() &&
      (!response_payload.contains("output") || !response_payload["output"].is_array() ||
       response_payload["output"].empty())) {
    response_payload["output"] = output_items;
  }

  if (!output_text.empty() &&
      (!response_payload.contains("output_text") || !response_payload["output_text"].is_string() ||
       response_payload["output_text"].get<std::string>().empty())) {
    response_payload["output_text"] = output_text;
  }

  if (!response_payload.is_object() || response_payload.empty()) {
    return {std::nullopt,
            std::format("Codex stream did not produce a final response. raw={}",
                        body.substr(0, std::min<std::size_t>(body.size(), 600)))};
  }

  return parse_response_json(response_payload);
}
