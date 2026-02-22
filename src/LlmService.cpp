#include <LlmService.h>

LlmService::LlmService(const Config &config, dpp::cluster &bot)
    : config(config), bot(bot) {}

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
    answer = ollama::chat(model, messages, opts);
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
