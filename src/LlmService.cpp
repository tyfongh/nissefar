#include <LlmService.h>
#include <OllamaToolCalling.h>

#include <unordered_set>

namespace {

std::string response_to_text(const ollama::response &response) {
  const auto payload = response.as_json();

  if (payload.contains("message") && payload["message"].is_object()) {
    const auto &message = payload["message"];
    if (message.contains("content")) {
      if (message["content"].is_string()) {
        return message["content"].get<std::string>();
      }
      if (!message["content"].is_null()) {
        return message["content"].dump();
      }
    }
  }

  if (payload.contains("response") && payload["response"].is_string()) {
    return payload["response"].get<std::string>();
  }

  return payload.dump();
}

std::string response_shape(const ollama::response &response) {
  const auto payload = response.as_json();
  if (!payload.contains("message") || !payload["message"].is_object()) {
    return "missing message object";
  }

  const auto &message = payload["message"];
  if (!message.contains("content")) {
    return "message without content";
  }

  return std::format("message.content type={}", message["content"].type_name());
}

} // namespace

LlmService::LlmService(const Config &config, dpp::cluster &bot)
    : config(config), bot(bot), ollama_client(config.ollama_server_url) {
  ollama_client.setReadTimeout(360);
  ollama_client.setWriteTimeout(360);
}

dpp::task<ollama::images> LlmService::generate_images(
    std::vector<dpp::attachment> attachments) const {
  ollama::images imagelist;
  for (const auto &attachment : attachments) {
    if (attachment.content_type == "image/jpeg" ||
        attachment.content_type == "image/webp" ||
        attachment.content_type == "image/png") {
      dpp::http_request_completion_t attachment_data =
          co_await bot.co_request(attachment.url, dpp::m_get);
      bot.log(dpp::ll_info,
              std::format("Image size: {}", attachment_data.body.size()));
      imagelist.push_back(ollama::image(
          macaron::Base64::Encode(std::string(attachment_data.body))));
    }
  }
  co_return imagelist;
}

std::string LlmService::generate_text(const std::string &prompt,
                                      const ollama::images &imagelist,
                                      GenerationType gen_type) const {
  ollama::options opts;
  std::string model;
  std::string system_prompt;
  ollama::messages messages;

  opts["num_predict"] = 1000;
  opts["num_ctx"] = 40000;

  using enum GenerationType;
  switch (gen_type) {
  case TextReply:
    system_prompt = config.system_prompt;
    if (imagelist.size() > 0) {
      model = config.vision_model;
      ollama::message user_message("user", prompt);
      user_message["images"] = imagelist;
      messages.push_back(user_message);
    } else {
      model = config.text_model;
      messages.emplace_back("user", prompt);
    }
    break;
  case Diff:
    system_prompt = config.diff_system_prompt;
    model = config.comparison_model;
    messages.emplace_back("user", prompt);
    break;
  case ImageDescription:
    system_prompt = config.image_description_system_prompt;
    model = config.image_description_model;
    {
      ollama::message user_message("user", prompt);
      user_message["images"] = imagelist;
      messages.push_back(user_message);
    }
    break;
  }

  messages.insert(messages.begin(), ollama::message("system", system_prompt));

  std::string answer{};
  try {
    const bool use_generate_endpoint =
        (gen_type == GenerationType::ImageDescription) ||
        (gen_type == GenerationType::TextReply && !imagelist.empty());

    if (use_generate_endpoint) {
      ollama::request request(model, prompt, opts, false, imagelist);
      request["system"] = system_prompt;
      answer = ollama_client.generate(request);
    } else {
      answer = ollama_client.chat(model, messages, opts);
    }
  } catch (ollama::exception e) {
    answer = std::format("Exception running llm: {}", e.what());
  } catch (const std::exception &e) {
    answer = std::format("Exception running llm: {}", e.what());
  }

  if (gen_type == ImageDescription) {
    bot.log(dpp::ll_info, std::format("Got image description: {}", answer));
  }

  if (answer.length() > 1800)
    answer.resize(1800);

  return answer;
}

dpp::task<std::string> LlmService::generate_text_with_tools(
    const std::string &prompt, const ollama::images &imagelist,
    const std::vector<LlmService::ToolDefinition> &available_tools,
    const std::function<dpp::task<std::string>(const std::string &,
                                               const std::string &)>
        &tool_executor) const {
  if (!imagelist.empty()) {
    co_return generate_text(prompt, imagelist, GenerationType::TextReply);
  }

  ollama::options opts;
  opts["num_predict"] = 1000;
  opts["num_ctx"] = 40000;

  ollama_tools::tools json_tools;
  for (const auto &tool : available_tools) {
    ollama::json parameters =
        ollama::json{{"type", "object"}, {"properties", ollama::json::object()}};
    if (!tool.parameters_schema_json.empty()) {
      try {
        parameters = ollama::json::parse(tool.parameters_schema_json);
      } catch (...) {
      }
    }

    json_tools.push_back(ollama_tools::make_function_tool(
        tool.name, tool.description, parameters));
  }

  const std::string model = imagelist.empty() ? config.text_model : config.vision_model;
  ollama::messages messages;
  messages.emplace_back("system", config.system_prompt);

  if (!imagelist.empty()) {
    ollama::message user_message("user", prompt);
    user_message["images"] = imagelist;
    messages.push_back(user_message);
  } else {
    messages.emplace_back("user", prompt);
  }

  std::string answer{};
  bool tool_calling_failed = false;
  std::string failure_reason;
  int tool_calls_executed = 0;
  std::string last_tool_name;
  std::string last_tool_args;
  std::string last_tool_output_preview;
  std::size_t last_tool_output_size = 0;
  std::unordered_set<std::string> seen_tool_calls;

  try {
    bot.log(dpp::ll_info,
            std::format("Tool-calling enabled with {} tools", json_tools.size()));

    ollama::response response =
        ollama_tools::chat(ollama_client, model, messages, opts, json_tools);

    for (int iteration = 0; iteration < 4; ++iteration) {
      if (!ollama_tools::has_tool_calls(response)) {
        answer = response_to_text(response);
        if (answer.empty()) {
          bot.log(dpp::ll_warning,
                  std::format("Tool chat returned empty assistant content ({})",
                              response_shape(response)));
        }
        break;
      }

      messages.push_back(ollama_tools::assistant_message(response));

      for (const auto &tool_call : ollama_tools::tool_calls(response)) {
        std::string tool_name = "unknown_tool";
        std::string arguments_json = "{}";

        if (tool_call.contains("function") && tool_call["function"].is_object()) {
          const auto &fn = tool_call["function"];
          if (fn.contains("name") && fn["name"].is_string()) {
            tool_name = fn["name"].get<std::string>();
          }
          if (fn.contains("arguments") && fn["arguments"].is_object()) {
            arguments_json = fn["arguments"].dump();
          } else if (fn.contains("arguments") && fn["arguments"].is_string()) {
            arguments_json = fn["arguments"].get<std::string>();
          }
        }

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

        std::string tool_output;
        if (seen_tool_calls.contains(tool_key)) {
          tool_output =
              "Tool error: duplicate tool call blocked in same request. Use the prior result.";
          bot.log(dpp::ll_warning,
                  std::format("Blocked duplicate tool call: {} args={}", tool_name,
                              logged_args));
        } else {
          seen_tool_calls.insert(tool_key);
          tool_output = co_await tool_executor(tool_name, arguments_json);
          ++tool_calls_executed;
        }

        last_tool_output_size = tool_output.size();
        last_tool_output_preview = tool_output;
        if (last_tool_output_preview.size() > 300) {
          last_tool_output_preview.resize(300);
          last_tool_output_preview += "...";
        }
        bot.log(dpp::ll_info,
                std::format("Tool call result: {} output_bytes={}", tool_name,
                            tool_output.size()));
        messages.push_back(ollama_tools::tool_result_message(tool_name, tool_output));
      }

      response =
          ollama_tools::chat(ollama_client, model, messages, opts, json_tools);
    }

    if (answer.empty()) {
      tool_calling_failed = true;
      failure_reason = "Tool-calling did not finish within 4 iterations.";
    }
  } catch (ollama::exception e) {
    tool_calling_failed = true;
    failure_reason = std::format("Exception while running tool-calling: {}", e.what());
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
      const ollama::response fallback_response =
          ollama_tools::chat(ollama_client, model, messages, opts,
                             ollama_tools::tools{});
      answer = response_to_text(fallback_response);
    } catch (ollama::exception e) {
      bot.log(dpp::ll_error,
              std::format("Fallback chat after tool-calling failure also failed: {}",
                          e.what()));
      answer = "I had trouble finishing that request right now.";
    } catch (const std::exception &e) {
      bot.log(dpp::ll_error,
              std::format("Fallback chat after tool-calling failure threw: {}",
                          e.what()));
      answer = "I had trouble finishing that request right now.";
    }
  }

  if (answer.length() > 1800)
    answer.resize(1800);

  co_return answer;
}
