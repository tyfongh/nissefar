#include <CodexClient.h>

#include <stdexcept>

namespace {

void expect_true(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_build_request_json_uses_expected_shape() {
  CodexRequest request{.model = "gpt-5.4",
                       .instructions = "Be brief.",
                       .messages = {{.role = "system", .content = "ignored-system", .images = {}},
                                    {.role = "user", .content = "hello", .images = {}}},
                       .tools = Json::array({Json{{"type", "function"},
                                                  {"name", "lookup"}}})};

  const Json payload = CodexClient::build_request_json(request);
  expect_true(payload["model"] == "gpt-5.4", "request includes model");
  expect_true(payload["instructions"] == "Be brief.",
              "request includes instructions");
  expect_true(payload["stream"] == true, "request enables streaming");
  expect_true(payload["store"] == false, "request disables server-side storage");
  expect_true(payload["input"].is_array() && payload["input"].size() == 2,
              "request includes all messages");
  expect_true(payload["input"][1]["content"] == "hello",
              "text message content stays scalar");
  expect_true(payload.contains("tools") && payload["tools"].is_array() &&
                  payload["tools"].size() == 1,
              "request includes tools array");

  CodexRequest followup{.model = "gpt-5.4",
                        .messages = {},
                        .input_items = Json::array({Json{{"type", "function_call_output"},
                                                         {"call_id", "call_123"},
                                                         {"output", "done"}}})};
  const Json followup_payload = CodexClient::build_request_json(followup);
  expect_true(followup_payload["input"].size() == 1,
              "raw input items are appended to request input");
  expect_true(followup_payload["input"][0]["call_id"] == "call_123",
              "raw input item shape is preserved");
}

void test_build_request_json_maps_images_as_data_urls() {
  CodexRequest request{
      .model = "gpt-5.4",
      .messages = {LlmMessage{.role = "user",
                              .content = "describe this",
                              .images = {LlmImage{.mime_type = "image/png",
                                                  .base64_data = "QUJD"},
                                         LlmImage{.mime_type = "image/jpeg",
                                                  .base64_data = "REVG"}}}}};

  const Json payload = CodexClient::build_request_json(request);
  expect_true(payload["input"][0]["content"].is_array(),
              "image request uses multimodal content array");
  expect_true(payload["input"][0]["content"].size() == 3,
              "text plus two images are included");
  expect_true(payload["input"][0]["content"][1]["image_url"] ==
                  "data:image/png;base64,QUJD",
              "png image is converted to data URL");
  expect_true(payload["input"][0]["content"][2]["image_url"] ==
                  "data:image/jpeg;base64,REVG",
              "jpeg image is converted to data URL");
}

void test_build_request_json_skips_malformed_images() {
  CodexRequest request{
      .model = "gpt-5.4",
      .messages = {LlmMessage{.role = "user",
                              .content = "describe this",
                              .images = {LlmImage{.mime_type = "text/plain",
                                                  .base64_data = "QUJD"},
                                         LlmImage{.mime_type = "image/png",
                                                  .base64_data = ""}}}}};

  const Json payload = CodexClient::build_request_json(request);
  expect_true(payload["input"][0]["content"] == "describe this",
              "invalid images fall back to plain text content");
}

void test_parse_text_response() {
  const Json payload = {
      {"output", Json::array({Json{{"type", "message"},
                                     {"content", Json::array({Json{{"type", "output_text"},
                                                                     {"text", "hello "}},
                                                               Json{{"type", "text"},
                                                                    {"text", "world"}}})}}})}};

  const auto result = CodexClient::parse_response_json(payload);
  expect_true(result.ok(), "text response parses successfully");
  expect_true(result.response->text == "hello world", "text parts are concatenated");
  expect_true(result.response->tool_calls.empty(), "text response has no tool calls");
}

void test_parse_tool_call_response() {
  const Json payload = {{"output",
                         Json::array({Json{{"type", "function_call"},
                                            {"name", "lookup_sheet"},
                                            {"arguments", R"({"tab":"A"})"}}})}};

  const auto result = CodexClient::parse_response_json(payload);
  expect_true(result.ok(), "tool call response parses successfully");
  expect_true(result.response->tool_calls.size() == 1,
              "one tool call is returned");
  expect_true(result.response->tool_calls[0].call_id.empty(),
              "missing tool call id stays empty");
  expect_true(result.response->tool_calls[0].name == "lookup_sheet",
              "tool call name is preserved");
  expect_true(result.response->tool_calls[0].arguments["tab"] == "A",
              "tool call arguments are parsed as JSON");
}

void test_parse_tool_call_response_with_call_id() {
  const Json payload = {{"output",
                         Json::array({Json{{"type", "function_call"},
                                            {"call_id", "call_456"},
                                            {"name", "lookup_sheet"},
                                            {"arguments", Json{{"tab", "B"}}}}})}};

  const auto result = CodexClient::parse_response_json(payload);
  expect_true(result.ok(), "tool call with call id parses successfully");
  expect_true(result.response->tool_calls[0].call_id == "call_456",
              "tool call id is preserved");
}

void test_parse_error_response() {
  const Json payload = {{"error", Json{{"message", "bad token"}}}};

  const auto result = CodexClient::parse_response_json(payload);
  expect_true(!result.ok(), "error payload should fail");
  expect_true(result.error.find("bad token") != std::string::npos,
              "error payload includes server message");
}

void test_parse_malformed_response() {
  const Json payload = {{"output", Json::array({Json{{"type", "message"},
                                                      {"content", Json::array()}}})}};

  const auto result = CodexClient::parse_response_json(payload);
  expect_true(!result.ok(), "malformed payload should fail");
  expect_true(result.error.find("text output") != std::string::npos,
              "malformed payload reports missing content");
}

void test_parse_stream_body_with_completed_response() {
  const std::string body =
      "event: response.output_text.delta\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello\"}\n\n"
      "event: response.completed\n"
      "data: {\"type\":\"response.completed\",\"response\":{\"output_text\":\"hello world\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"hello world\"}]}]}}\n\n";

  const auto result = CodexClient::parse_stream_body(body);
  expect_true(result.ok(), "completed stream response parses successfully");
  expect_true(result.response->text == "hello world",
              "completed stream uses final response payload actual='" +
                  result.response->text + "'");
}

void test_parse_stream_body_from_output_items() {
  const std::string body =
      "event: response.output_item.done\n"
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"type\":\"function_call\",\"call_id\":\"call_789\",\"name\":\"lookup\",\"arguments\":{\"q\":\"x\"}}}\n\n";

  const auto result = CodexClient::parse_stream_body(body);
  expect_true(result.ok(), "output-item stream parses successfully");
  expect_true(result.response->tool_calls.size() == 1,
              "stream output item yields tool call");
  expect_true(result.response->tool_calls[0].call_id == "call_789",
              "stream tool call id is preserved");
}

void test_parse_image_generation_response() {
  const Json payload = {{"output",
                         Json::array({Json{{"type", "image_generation_call"},
                                            {"id", "ig_123"},
                                            {"output_format", "png"},
                                            {"revised_prompt", "a red ball"},
                                            {"result", "QUJD"}}})}};

  const auto result = CodexClient::parse_response_json(payload);
  expect_true(result.ok(), "image generation response parses successfully");
  expect_true(result.response->generated_images.size() == 1,
              "image generation result is returned");
  expect_true(result.response->generated_images[0].id == "ig_123",
              "image generation id is preserved");
  expect_true(result.response->generated_images[0].mime_type == "image/png",
              "image generation mime type is inferred");
  expect_true(result.response->generated_images[0].base64_data == "QUJD",
              "image generation payload is preserved");
}

void test_parse_stream_body_with_image_generation_output() {
  const std::string body =
      "event: response.output_item.done\n"
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"type\":\"image_generation_call\",\"id\":\"ig_123\",\"output_format\":\"png\",\"result\":\"QUJD\"}}\n\n"
      "event: response.completed\n"
      "data: {\"type\":\"response.completed\",\"response\":{\"output\":[]}}\n\n";

  const auto result = CodexClient::parse_stream_body(body);
  expect_true(result.ok(), "image generation stream parses successfully");
  expect_true(result.response->generated_images.size() == 1,
              "stream image generation item is preserved");
}

} // namespace

int main() {
  test_build_request_json_uses_expected_shape();
  test_build_request_json_maps_images_as_data_urls();
  test_build_request_json_skips_malformed_images();
  test_parse_text_response();
  test_parse_tool_call_response();
  test_parse_tool_call_response_with_call_id();
  test_parse_error_response();
  test_parse_malformed_response();
  test_parse_stream_body_with_completed_response();
  test_parse_stream_body_from_output_items();
  test_parse_image_generation_response();
  test_parse_stream_body_with_image_generation_output();
  return 0;
}
