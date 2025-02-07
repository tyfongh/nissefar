#include <dpp/cache.h>
#include <dpp/dispatcher.h>
#include <dpp/dpp.h>
#include <dpp/guild.h>
#include <dpp/intents.h>
#include <dpp/message.h>
#include <dpp/misc-enum.h>
#include <dpp/restresults.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <ollama.hpp>
#include <string>

const std::string CONFIG_PATH = "/home/tyfon/.config/nissefar/token.txt";

std::string read_token() {
  std::ifstream f(CONFIG_PATH, std::ios::in | std::ios::binary);
  const auto filesize = std::filesystem::file_size(CONFIG_PATH);
  std::string token(filesize, '\0');
  f.read(token.data(), filesize);
  if (!token.empty() && token[token.length() - 1] == '\n')
    token.erase(token.length() - 1);
  return token;
}

int main() {

  const auto token = read_token();
  dpp::cluster bot(token, dpp::i_default_intents | dpp::i_message_content);

  // Sett antall tokens ut til 100 for å begrense meldingslengden

  ollama::options opts;
  opts["num_predict"] = 100;

  bot.on_log(dpp::utility::cout_logger());

  // Vi ønsker kun å svare på melding til boten i spesifikke kanaler

  bot.on_message_create([&bot](const dpp::message_create_t &event)
                            -> dpp::task<void> {
    bool svar = false;

    // Hent server og kanal, test mot aksepterte kombinasjoner

    dpp::guild *current_server = dpp::find_guild(event.msg.guild_id);
    dpp::channel *current_chan = dpp::find_channel(event.msg.channel_id);
    if (current_server && current_chan) {
      /*
          bot.log(dpp::loglevel::ll_info,
                  std::format("Server: {}", current_server->name));
          bot.log(dpp::loglevel::ll_info,
                  std::format("Channel: {}", current_chan->name));
  */
      if ((current_server->name == "tyfon's server" &&
           current_chan->name == "general") ||
          (current_server->name == "Electric Vehicles Enthusiasts" &&
           current_chan->name == "botspam"))
        svar = true;
    }

    // Sjekke vektoren med mentions om botens ID er her og om vi skal svare

    for (const std::pair<dpp::user, dpp::guild_member> &mention :
         event.msg.mentions) {
      if (mention.second.user_id.str() == bot.me.id.str() && svar) {

        bot.log(dpp::loglevel::ll_info,
                std::format("message from {}: {}",
                            event.msg.author.format_username(),
                            event.msg.content));

        // Lag system prompt

        std::string system_prompt = std::format(
            "Your task is to generate a short witty reply to the discord "
            "message provided. The message is written by user id: <@{}>. The "
            "user id can be used to tag the author of the message.",
            event.msg.author.id.str());

        // Legg til meldingen det ble svart på dersom denne eksisterer
        if (event.msg.message_reference.message_id) {

          dpp::confirmation_callback_t result = co_await bot.co_message_get(
              event.msg.message_reference.message_id, event.msg.channel_id);
          dpp::message op_message = result.get<dpp::message>();
          bot.log(dpp::loglevel::ll_info,
                  std::format("In reply to [{}]: {}",
                              event.msg.message_reference.message_id.str(),
                              op_message.content));
          system_prompt.append(std::format(
              " The message is a reply to the following message: {}",
              op_message.content));
        }

        // Bot funnet, lag svar via ollama.

        bot.log(dpp::loglevel::ll_info, std::format("Bot was mentioned by {}",
                                                    event.msg.author.id.str()));
        ollama::request req;
        req["model"] = "mistral-small:24b-instruct-2501-q8_0";
        req["system"] = system_prompt;
        req["prompt"] = event.msg.content;
        event.reply(ollama::generate(req), true);
      }
    }
    co_return;
  });

  bot.start(dpp::st_wait);
}
