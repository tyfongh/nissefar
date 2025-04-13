#include <nissefar.h>
#include <ollama.hpp>
#include <stdexcept>

Nissefar::Nissefar() {
  if (!config.is_valid)
    throw std::runtime_error("Configuration is invalid");

  bot = std::make_unique<dpp::cluster>(
      config.discord_token, dpp::i_default_intents | dpp::i_message_content);

  bot->on_log(dpp::utility::cout_logger());

  ollama::setReadTimeout(360);
  ollama::setWriteTimeout(360);

  bot->log(dpp::ll_info, "Bot initialized");
}

void Nissefar::add_channel_message(dpp::snowflake channel_id,
                                   const Message &msg) {
  auto &buffer = channel_history[channel_id];
  if (buffer.size() == config.max_history)
    buffer.pop_front();
  buffer.push_back(msg);
}

const std::deque<Message> &
Nissefar::get_channel_history(dpp::snowflake channel_id) const {
  static const std::deque<Message> empty;
  auto history = channel_history.find(channel_id);
  if (history == channel_history.end())
    return empty;
  return history->second;
}

std::string Nissefar::format_message_history(dpp::snowflake channel_id) {
  std::string message_history{};
  auto msgs = get_channel_history(channel_id);
  if (msgs.size() > 0)
    message_history = std::string("Channel message history:");
  for (auto msg : get_channel_history(channel_id)) {
    message_history += std::format("\n----------------------\n"
                                   "Message id: {}\n"
                                   "Reply to message id: {}\n"
                                   "Author: {}\n"
                                   "Message content:{}",
                                   msg.msg_id.str(), msg.msg_replied_to.str(),
                                   msg.author.str(), msg.content);
  }
  if (!message_history.empty())
    message_history += "\n----------------------";
  return message_history;
}

dpp::task<ollama::images>
Nissefar::generate_images(std::vector<dpp::attachment> attachments) {
  ollama::images imagelist;
  for (auto attachment : attachments) {
    if (attachment.content_type == "image/jpeg" ||
        attachment.content_type == "image/png") {
      dpp::http_request_completion_t attachment_data =
          co_await bot->co_request(attachment.url, dpp::m_get);
      bot->log(dpp::ll_info,
               std::format("Image size: {}", attachment_data.body.size()));
      imagelist.push_back(ollama::image(
          macaron::Base64::Encode(std::string(attachment_data.body))));
    }
  }
  co_return imagelist;
}

std::string Nissefar::format_replyto_message(const Message &msg) {
  std::string message_text = std::format(
      "\nThe message you reply to:\n"
      "----------------------\nMessage id: {}\nReply to message "
      "id: {}\nAuthor: {}\nMessage content:{}\n----------------------\n",
      msg.msg_id.str(), msg.msg_replied_to.str(), msg.author.str(),
      msg.content);

  return message_text;
}

std::string Nissefar::generate_reply(const std::string &prompt,
                                     const ollama::images &imagelist) {
  ollama::request req;
  ollama::options opts;

  opts["num_predict"] = 1000;
  opts["temperature"] = 1;
  opts["top_k"] = 64;
  opts["top_p"] = 0.95;
  opts["min_p"] = 0;

  req["system"] = config.system_prompt;
  req["prompt"] = prompt;
  req["options"] = opts["options"];

  if (imagelist.size() > 0) {
    req["model"] = config.vision_model;
    req["images"] = imagelist;
  } else
    req["model"] = config.text_model;

  std::string answer{};
  try {
    answer = ollama::generate(req);
  } catch (ollama::exception e) {
    answer = std::format("Exception running llm: {}", e.what());
  }

  return answer;
}

dpp::task<void> Nissefar::handle_message(const dpp::message_create_t &event) {

  bool answer = false;

  dpp::guild *current_server = dpp::find_guild(event.msg.guild_id);
  dpp::channel *current_chan = dpp::find_channel(event.msg.channel_id);

  bot->log(
      dpp::ll_info,
      std::format("Message on server \"{}\", channel \"{}\", user: \"{}\": {}",
                  current_server->name, current_chan->name,
                  event.msg.author.id.str(), event.msg.content));

  for (auto mention : event.msg.mentions) {
    if (mention.second.user_id == bot->me.id &&
        event.msg.author.id != bot->me.id)
      answer = true;
  }

  Message last_message{event.msg.id, event.msg.message_reference.message_id,
                       event.msg.content, event.msg.author.id};

  if (answer) {
    std::string prompt = std::format("\nBot user id: {}\n", bot->me.id.str()) +
                         format_message_history(event.msg.channel_id) +
                         format_replyto_message(last_message);

    auto imagelist = co_await generate_images(event.msg.attachments);

    bot->log(dpp::ll_info, prompt);
    bot->log(dpp::ll_info,
             std::format("Number of images: {}", imagelist.size()));

    event.reply(generate_reply(prompt, imagelist), true);
  }

  add_channel_message(event.msg.channel_id, last_message);

  co_return;
}

void Nissefar::run() {

  bot->on_message_create(
      [this](const dpp::message_create_t &event) -> dpp::task<void> {
        co_return co_await handle_message(event);
      });

  bot->log(dpp::ll_info, "Starting bot..");
  bot->start(dpp::st_wait);
}
