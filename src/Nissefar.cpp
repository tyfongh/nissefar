#include <dpp/channel.h>
#include <dpp/misc-enum.h>
#include <nissefar.h>
#include <stdexcept>

Nissefar::Nissefar() {
  if (!config.is_valid)
    throw std::runtime_error("Configuration is invalid");

  bot = std::make_unique<dpp::cluster>(
      config.discord_token, dpp::i_default_intents | dpp::i_message_content);

  bot->on_log(dpp::utility::cout_logger());
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

dpp::task<void> Nissefar::handle_message(const dpp::message_create_t &event) {

  dpp::guild *current_server = dpp::find_guild(event.msg.guild_id);
  dpp::channel *current_chan = dpp::find_channel(event.msg.channel_id);

  bot->log(
      dpp::ll_info,
      std::format("Message on server \"{}\", channel \"{}\", user: \"{}\": {}",
                  current_server->name, current_chan->name,
                  event.msg.author.id.str(), event.msg.content));

  Message last_message{event.msg.content, event.msg.author.id};
  add_channel_message(event.msg.channel_id, last_message);

  for (auto msg : get_channel_history(event.msg.channel_id)) {
    bot->log(dpp::ll_info, std::format("Message from: {}, content: {}",
                                       msg.author.str(), msg.content));
  }

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
