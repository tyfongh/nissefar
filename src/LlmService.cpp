#include <LlmService.h>

#include <array>
#include <unordered_set>

namespace {

std::string encode_base64(const std::string &input) {
  static constexpr std::array<char, 64> kAlphabet = {
      'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
      'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b',
      'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
      'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3',
      '4', '5', '6', '7', '8', '9', '+', '/'};

  std::string output;
  output.reserve(((input.size() + 2) / 3) * 4);

  for (std::size_t i = 0; i < input.size(); i += 3) {
    const unsigned char a = static_cast<unsigned char>(input[i]);
    const bool has_b = i + 1 < input.size();
    const bool has_c = i + 2 < input.size();
    const unsigned char b = has_b ? static_cast<unsigned char>(input[i + 1]) : 0;
    const unsigned char c = has_c ? static_cast<unsigned char>(input[i + 2]) : 0;

    output.push_back(kAlphabet[a >> 2]);
    output.push_back(kAlphabet[((a & 0x03) << 4) | (b >> 4)]);
    output.push_back(has_b ? kAlphabet[((b & 0x0F) << 2) | (c >> 6)] : '=');
    output.push_back(has_c ? kAlphabet[c & 0x3F] : '=');
  }

  return output;
}

std::string truncate_for_log(std::string value, std::size_t max_size) {
  if (value.size() <= max_size) {
    return value;
  }

  value.resize(max_size);
  value += "...";
  return value;
}

Json codex_tools_from_definitions(
    const std::vector<LlmService::ToolDefinition> &available_tools) {
  Json tools = Json::array();
  for (const auto &tool : available_tools) {
    Json parameters = Json{{"type", "object"}, {"properties", Json::object()}};
    if (!tool.parameters_schema_json.empty()) {
      try {
        parameters = Json::parse(tool.parameters_schema_json);
      } catch (...) {
      }
    }

    tools.push_back(Json{{"type", "function"},
                         {"name", tool.name},
                         {"description", tool.description},
                         {"parameters", std::move(parameters)}});
  }
  return tools;
}

std::string tool_schema_sizes_for_log(
    const std::vector<LlmService::ToolDefinition> &available_tools) {
  std::string result = "[";
  bool first = true;

  for (const auto &tool : available_tools) {
    if (!first) {
      result += ", ";
    }
    first = false;
    result += std::format("{}:{}", tool.name, tool.parameters_schema_json.size());
  }

  result += "]";
  return result;
}

std::string tool_call_names_for_log(const LlmResponse &response) {
  if (response.tool_calls.empty()) {
    return "[]";
  }

  std::string names = "[";
  bool first = true;

  for (const auto &tool_call : response.tool_calls) {
    if (!first) {
      names += ", ";
    }
    first = false;
    names += tool_call.name;
  }

  names += "]";
  return names;
}

std::string join_instructions(const std::string &base,
                              const std::string &suffix) {
  if (suffix.empty()) {
    return base;
  }
  return std::format("{}\n\n{}", base, suffix);
}

std::string system_prompt_for_type(const Config &config,
                                   LlmService::GenerationType gen_type) {
  using enum LlmService::GenerationType;

  switch (gen_type) {
  case TextReply:
    return config.system_prompt;
  case Diff:
    return config.diff_system_prompt;
  case ImageDescription:
    return config.image_description_system_prompt;
  }

  return config.system_prompt;
}

bool is_auth_error(const CodexResponseResult &result) { return result.status == 401; }

std::size_t request_payload_bytes(const CodexRequest &request) {
  return CodexClient::build_request_json(request).dump().size();
}

Json codex_image_generation_tool() {
  return Json::array({Json{{"type", "image_generation"}}});
}

} // namespace

LlmService::LlmService(const Config &config, dpp::cluster &bot,
                       std::shared_ptr<ChatGptAuthManager> auth_manager)
    : config(config), bot(bot), auth_manager(std::move(auth_manager)),
      codex_client("nissefar/0.1") {}

CodexResponseResult
LlmService::create_codex_response_with_auth_retry(const CodexRequest &request) const {
  const auto auth_result = auth_manager->ensure_valid();
  if (!auth_result.ok()) {
    return {std::nullopt, auth_result.error};
  }

  auto result = codex_client.create_response(*auth_result.auth, request);
  if (!is_auth_error(result)) {
    return result;
  }

  bot.log(dpp::ll_warning,
          std::format("Codex request returned HTTP 401; forcing auth refresh and retrying once"));

  const auto refresh_result = auth_manager->ensure_valid(true);
  if (!refresh_result.ok()) {
    return {std::nullopt,
            std::format("Auth retry after HTTP 401 failed: {}", refresh_result.error)};
  }

  return codex_client.create_response(*refresh_result.auth, request);
}

dpp::task<LlmImages> LlmService::generate_images(
    std::vector<dpp::attachment> attachments) const {
  LlmImages imagelist;
  for (const auto &attachment : attachments) {
    if (attachment.content_type == "image/jpeg" ||
        attachment.content_type == "image/webp" ||
        attachment.content_type == "image/png") {
      dpp::http_request_completion_t attachment_data =
          co_await bot.co_request(attachment.url, dpp::m_get);
      bot.log(dpp::ll_info,
              std::format("Image size: {}", attachment_data.body.size()));
      imagelist.push_back(LlmImage{.mime_type = attachment.content_type,
                                   .base64_data =
                                       encode_base64(std::string(attachment_data.body))});
    }
  }
  co_return imagelist;
}

std::string LlmService::generate_text(const std::string &prompt,
                                      const LlmImages &imagelist,
                                      GenerationType gen_type) const {
  if (!auth_manager) {
    bot.log(dpp::ll_error, "ChatGPT auth manager is not configured.");
    return "Unable to authenticate with ChatGPT right now.";
  }

  const auto auth_result = auth_manager->ensure_valid();
  if (!auth_result.ok()) {
    bot.log(dpp::ll_error,
            std::format("ChatGPT auth failure before generation: {}",
                        auth_result.error));
    return "Unable to authenticate with ChatGPT right now.";
  }

  const std::string system_prompt = system_prompt_for_type(config, gen_type);
  const CodexRequest request{.model = config.chatgpt_model,
                             .instructions = system_prompt,
                             .messages = {LlmMessage{.role = "user",
                                                     .content = prompt,
                                                     .images = imagelist}}};

  std::string answer{};
  try {
    bot.log(dpp::ll_info,
            std::format("Codex plain request mode={} prompt_bytes={} images={} payload_bytes={}",
                        static_cast<int>(gen_type), prompt.size(), imagelist.size(),
                        request_payload_bytes(request)));
    const auto result = create_codex_response_with_auth_retry(request);
    if (!result.ok()) {
      bot.log(dpp::ll_error,
              std::format("ChatGPT generation failed for mode={} prompt_bytes={} images={}: {}",
                          static_cast<int>(gen_type), prompt.size(), imagelist.size(),
                          result.error));
      return "I had trouble finishing that request right now.";
    }

    answer = result.response->text;
  } catch (const std::exception &e) {
    bot.log(dpp::ll_error,
            std::format("Exception running ChatGPT generation for mode={} prompt_bytes={} images={}: {}",
                        static_cast<int>(gen_type), prompt.size(), imagelist.size(),
                        e.what()));
    return "I had trouble finishing that request right now.";
  }

  if (gen_type == GenerationType::ImageDescription) {
    bot.log(dpp::ll_info, std::format("Got image description: {}", answer));
  }

  if (answer.length() > 1800)
    answer.resize(1800);

  return answer;
}

std::optional<SentimentEvaluation>
LlmService::evaluate_sentiment(const Message &message) const {
  std::string image_context;
  if (!message.image_descriptions.empty()) {
    image_context = "\n\nAttached image descriptions:";
    for (std::size_t i = 0; i < message.image_descriptions.size(); ++i) {
      image_context += std::format("\nImage {}: {}", i,
                                   message.image_descriptions[i]);
    }
  }

  const std::string prompt =
      "Evaluate the sentiment/tone of this Discord message.\n"
      "Return only valid JSON with this exact shape:\n"
      R"({"label":"positive|neutral|negative|mixed|unclear","score":0.0,"tone":["short lowercase tags"],"confidence":0.0})"
      "\nRules:\n"
      "- label must be one of positive, neutral, negative, mixed, unclear.\n"
      "- score must be from -1.0 negative to 1.0 positive.\n"
      "- confidence must be from 0.0 to 1.0.\n"
      "- tone must contain at most five short lowercase tags.\n"
      "- Consider both the message text and any attached image descriptions.\n"
      "- Do not include markdown or explanation.\n\n"
      "Message content:\n" +
      message.content + image_context;

  const std::string response =
      generate_text(prompt, LlmImages{}, GenerationType::Sentiment);
  return sentiment::parse_evaluation_json(response);
}

std::optional<LlmGeneratedImage>
LlmService::generate_image(const std::string &prompt,
                           const LlmImages &imagelist) const {
  if (!auth_manager) {
    bot.log(dpp::ll_error, "ChatGPT auth manager is not configured.");
    return std::nullopt;
  }

  const auto auth_result = auth_manager->ensure_valid();
  if (!auth_result.ok()) {
    bot.log(dpp::ll_error,
            std::format("ChatGPT auth failure before image generation: {}",
                        auth_result.error));
    return std::nullopt;
  }

  const CodexRequest request{.model = config.chatgpt_model,
                             .instructions =
                                 "Generate or edit an image from the provided standalone "
                                 "visual specification. Treat it as the complete image "
                                 "brief; do not request conversation context.",
                             .messages = {LlmMessage{.role = "user",
                                                     .content = prompt,
                                                     .images = imagelist}},
                             .tools = codex_image_generation_tool(),
                             .tool_choice = Json{{"type", "image_generation"}}};

  try {
    bot.log(dpp::ll_info,
            std::format("Codex image request prompt_bytes={} images={} payload_bytes={}",
                        prompt.size(), imagelist.size(), request_payload_bytes(request)));
    const auto result = create_codex_response_with_auth_retry(request);
    if (!result.ok()) {
      bot.log(dpp::ll_error,
              std::format("ChatGPT image generation failed for prompt_bytes={} images={}: {}",
                          prompt.size(), imagelist.size(), result.error));
      return std::nullopt;
    }

    if (result.response->generated_images.empty()) {
      bot.log(dpp::ll_warning,
              std::format("Codex image request returned no image output items payload={} ",
                          truncate_for_log(result.response->output_items.dump(), 600)));
      return std::nullopt;
    }

    return result.response->generated_images.front();
  } catch (const std::exception &e) {
    bot.log(dpp::ll_error,
            std::format("Exception running ChatGPT image generation for prompt_bytes={} images={}: {}",
                        prompt.size(), imagelist.size(), e.what()));
    return std::nullopt;
  }
}

dpp::task<LlmGenerationResult> LlmService::generate_text_with_tools(
    const std::string &prompt, const LlmImages &imagelist,
    const std::vector<LlmService::ToolDefinition> &available_tools,
    const std::function<dpp::task<LlmToolResult>(const std::string &,
                                                 const std::string &)>
        &tool_executor) const {
  if (!auth_manager) {
    bot.log(dpp::ll_error, "ChatGPT auth manager is not configured.");
    co_return LlmGenerationResult{
        .text = "Unable to authenticate with ChatGPT right now."};
  }

  const auto initial_auth = auth_manager->ensure_valid();
  if (!initial_auth.ok()) {
    bot.log(dpp::ll_error,
            std::format("ChatGPT auth failure before tool generation: {}",
                        initial_auth.error));
    co_return LlmGenerationResult{
        .text = "Unable to authenticate with ChatGPT right now."};
  }

  const Json json_tools = codex_tools_from_definitions(available_tools);
  const std::string model = config.chatgpt_model;
  const std::vector<LlmMessage> initial_messages = {
      LlmMessage{.role = "user", .content = prompt, .images = imagelist}};
  Json accumulated_items = Json::array();
  std::string instruction_suffix;

  std::string answer{};
  std::vector<LlmArtifact> artifacts;
  bool tool_calling_failed = false;
  std::string failure_reason;
  int tool_calls_executed = 0;
  std::string last_tool_name;
  std::string last_tool_args;
  std::string last_tool_output_preview;
  std::size_t last_tool_output_size = 0;
  std::unordered_set<std::string> seen_tool_calls;
  bool analytics_tool_used = false;
  bool tool_phase_complete = false;
  bool saw_empty_content_without_tool_calls = false;

  try {
    bot.log(dpp::ll_info,
            std::format("Tool-calling enabled with {} tools", json_tools.size()));
    bot.log(dpp::ll_info,
            std::format("Codex tool schema sizes: {}",
                        tool_schema_sizes_for_log(available_tools)));

    for (int iteration = 0; iteration < 4; ++iteration) {
      const CodexRequest request{.model = model,
                                 .instructions =
                                     join_instructions(config.system_prompt,
                                                       instruction_suffix),
                                 .messages = initial_messages,
                                 .input_items = accumulated_items,
                                 .tools = tool_phase_complete ? Json::array()
                                                              : json_tools};
      bot.log(dpp::ll_info,
              std::format("Codex tool request iteration={} prompt_bytes={} tools={} "
                          "input_items={} payload_bytes={} tool_phase_forced_final={}",
                           iteration + 1, prompt.size(), json_tools.size(),
                           accumulated_items.is_array() ? accumulated_items.size() : 0,
                           request_payload_bytes(request),
                           tool_phase_complete ? "yes" : "no"));
      const auto result = create_codex_response_with_auth_retry(request);
      if (!result.ok()) {
        tool_calling_failed = true;
        failure_reason = std::format("Codex tool request failed: {}", result.error);
        break;
      }

      const auto &response = *result.response;
      const bool has_tool_calls = !response.tool_calls.empty();
      const std::size_t tool_call_count = response.tool_calls.size();

      const std::size_t content_length = response.text.size();

      bot.log(dpp::ll_info,
              std::format("Tool loop iteration={} has_tool_calls={} tool_calls_count={} "
                          "content_length={} tool_names={}",
                          iteration + 1, has_tool_calls, tool_call_count,
                          content_length,
                          tool_call_names_for_log(response)));

      if (response.output_items.is_array()) {
        for (const auto &item : response.output_items) {
          accumulated_items.push_back(item);
        }
      }

      if (!has_tool_calls) {
        answer = response.text;
        if (answer.empty()) {
          saw_empty_content_without_tool_calls = true;
          const std::string payload_preview =
              truncate_for_log(response.output_items.dump(), 600);
          bot.log(dpp::ll_warning,
                  std::format("Tool chat returned empty assistant content payload={}",
                              payload_preview));
        }
        break;
      }

      for (const auto &tool_call : response.tool_calls) {
        const std::string &tool_name = tool_call.name;
        const std::string arguments_json = tool_call.arguments.dump();

        std::string logged_args = arguments_json;
        if (logged_args.size() > 300) {
          logged_args.resize(300);
          logged_args += "...";
        }

        bot.log(dpp::ll_info,
                std::format("Tool call requested: {} args={}", tool_name,
                            logged_args));

        last_tool_name = tool_name;
        last_tool_args = logged_args;

        const std::string tool_key = tool_name + "\n" + arguments_json;

        LlmToolResult execution_result;
        if (seen_tool_calls.contains(tool_key)) {
          execution_result.output =
              "Tool error: duplicate tool call blocked in same request. Use the prior result.";
          bot.log(dpp::ll_warning,
                  std::format("Blocked duplicate tool call: {} args={}", tool_name,
                              logged_args));
        } else {
          seen_tool_calls.insert(tool_key);
          execution_result = co_await tool_executor(tool_name, arguments_json);
          ++tool_calls_executed;
          if (tool_name == "query_channel_analytics") {
            analytics_tool_used = true;
          }
        }

        if (execution_result.stop_tool_loop) {
          tool_phase_complete = true;
        }
        for (auto &artifact : execution_result.artifacts) {
          artifacts.push_back(std::move(artifact));
        }

        const std::string &tool_output = execution_result.output;
        last_tool_output_size = tool_output.size();
        last_tool_output_preview = tool_output;
        if (last_tool_output_preview.size() > 300) {
          last_tool_output_preview.resize(300);
          last_tool_output_preview += "...";
        }
        bot.log(dpp::ll_info,
                std::format("Tool call result: {} output_bytes={}", tool_name,
                            tool_output.size()));
        if (tool_call.call_id.empty()) {
          tool_calling_failed = true;
          failure_reason = std::format("Tool call '{}' was missing a call id.",
                                       tool_name);
          break;
        }

        accumulated_items.push_back(Json{{"type", "function_call_output"},
                                         {"call_id", tool_call.call_id},
                                         {"output", tool_output}});
      }

      if (tool_calling_failed) {
        break;
      }

      if (analytics_tool_used) {
        tool_phase_complete = true;
        bot.log(dpp::ll_info,
                "Analytics tool result received; forcing final response without tools");
        instruction_suffix =
            "Tool phase is complete. Use the returned analytics result as the final "
            "source of truth. Do not ask to run another query. Provide the final "
            "answer now.";
      } else if (tool_phase_complete) {
        bot.log(dpp::ll_info,
                "Tool requested final response; continuing without tools");
        instruction_suffix = artifacts.empty()
            ? "The tool phase is complete. Explain the tool result accurately and "
              "provide the final answer now. Do not call another tool."
            : "The requested image has been generated and will be attached to your "
              "message. Provide a brief final response acknowledging it. Do not "
              "claim that you cannot attach or display the image, and do not call "
              "another tool.";
      }
    }

    if (answer.empty() && !tool_calling_failed) {
      tool_calling_failed = true;
      if (saw_empty_content_without_tool_calls) {
        failure_reason = "Empty assistant content with no tool_calls.";
      } else {
        failure_reason = "Tool-calling did not finish within 4 iterations.";
      }
    }
  } catch (const std::exception &e) {
    tool_calling_failed = true;
    failure_reason =
        std::format("Std exception while running tool-calling: {}", e.what());
  } catch (...) {
    tool_calling_failed = true;
    failure_reason = "Unknown exception while running tool-calling.";
  }

  if (tool_calling_failed) {
    bot.log(
        dpp::ll_warning,
        std::format(
            "Tool-calling failed, continuing without tools. reason='{}' "
            "tool_calls_executed={} last_tool='{}' last_args='{}' "
            "last_output_bytes={} last_output_preview='{}'",
            failure_reason, tool_calls_executed, last_tool_name, last_tool_args,
            last_tool_output_size, last_tool_output_preview));

    try {
      const CodexRequest fallback_request{.model = model,
                                          .instructions =
                                              join_instructions(config.system_prompt,
                                                                instruction_suffix),
                                          .messages = initial_messages,
                                          .input_items = accumulated_items,
                                          .tools = Json::array()};
      bot.log(dpp::ll_info,
              std::format("Codex fallback request prompt_bytes={} input_items={} payload_bytes={}",
                          prompt.size(),
                          accumulated_items.is_array() ? accumulated_items.size() : 0,
                          request_payload_bytes(fallback_request)));
      const auto fallback_response =
          create_codex_response_with_auth_retry(fallback_request);
      if (!fallback_response.ok()) {
        throw std::runtime_error(fallback_response.error);
      }
      answer = fallback_response.response->text;
    } catch (const std::exception &e) {
      bot.log(dpp::ll_error,
              std::format("Fallback chat after tool-calling failure also failed: {}",
                          e.what()));
      answer = artifacts.empty() ? "I had trouble finishing that request right now."
                                 : "Generated image:";
    }
  }

  if (answer.length() > 1800)
    answer.resize(1800);

  co_return LlmGenerationResult{.text = std::move(answer),
                                .artifacts = std::move(artifacts)};
}
