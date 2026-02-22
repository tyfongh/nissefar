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
  ollama::request req;
  ollama::options opts;

  opts["num_predict"] = 1000;
  opts["num_ctx"] = 40000;

  req["prompt"] = prompt;
  req["options"] = opts["options"];

  using enum GenerationType;
  switch (gen_type) {
  case TextReply:
    req["system"] = config.system_prompt;
    if (imagelist.size() > 0) {
      req["images"] = imagelist;
      req["model"] = config.vision_model;
    } else {
      req["model"] = config.text_model;
    }
    break;
  case Diff:
    req["system"] = config.diff_system_prompt;
    req["model"] = config.comparison_model;
    break;
  case ImageDescription:
    req["system"] = config.image_description_system_prompt;
    req["model"] = config.image_description_model;
    req["images"] = imagelist;
    break;
  }

  std::string answer{};
  try {
    answer = ollama::generate(req);
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
