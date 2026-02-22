#include <LlmService.h>
#include <OllamaToolCalling.h>

LlmService::LlmService(const Config &config, dpp::cluster &bot)
    : config(config), bot(bot), ollama_client(config.ollama_server_url) {
  ollama_client.setReadTimeout(360);
  ollama_client.setWriteTimeout(360);
}

dpp::task<ollama::images> LlmService::generate_images(
    const std::vector<dpp::attachment> &attachments) const {
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
    answer = ollama_client.chat(model, messages, opts);
  } catch (ollama::exception e) {
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

  try {
    bot.log(dpp::ll_info,
            std::format("Tool-calling enabled with {} tools", json_tools.size()));

    ollama::response response =
        ollama_tools::chat(ollama_client, model, messages, opts, json_tools);

    for (int iteration = 0; iteration < 4; ++iteration) {
      if (!ollama_tools::has_tool_calls(response)) {
        answer = response;
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

        const std::string tool_output =
            co_await tool_executor(tool_name, arguments_json);
        bot.log(dpp::ll_info,
                std::format("Tool call result: {} output_bytes={}", tool_name,
                            tool_output.size()));
        messages.push_back(ollama_tools::tool_result_message(tool_name, tool_output));
      }

      response =
          ollama_tools::chat(ollama_client, model, messages, opts, json_tools);
    }

    if (answer.empty()) {
      answer = "I could not complete tool-calling for this request.";
    }
  } catch (ollama::exception e) {
    answer = std::format("Exception running llm: {}", e.what());
  }

  if (answer.length() > 1800)
    answer.resize(1800);

  co_return answer;
}
