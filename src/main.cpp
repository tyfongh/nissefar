#include <dpp/dpp.h>
#include <format>
#include <inicpp.h>
#include <ollama.hpp>
#include <regex>
#include <string>

const std::string CONFIG_PATH = "/home/tyfon/.config/nissefar/config";

int main() {

  ini::IniFile config;
  config.setMultiLineValues(true);
  config.load(CONFIG_PATH);

  const std::string token = config["General"]["token"].as<std::string>();
  const std::string system_prompt =
      config["General"]["system_prompt"].as<std::string>();
  const std::string text_model =
      config["General"]["text_model"].as<std::string>();
  const std::string vision_model =
      config["General"]["vision_model"].as<std::string>();

  dpp::cluster bot(token, dpp::i_default_intents | dpp::i_message_content);
  bot.on_log(dpp::utility::cout_logger());

  bot.log(dpp::loglevel::ll_info,
          std::format("System prompt: {}", system_prompt));

  // Sett antall tokens ut til 100 for å begrense meldingslengden

  ollama::options opts;
  opts["num_predict"] = 100;

  // Vi ønsker kun å svare på melding til boten i spesifikke kanaler

  bot.on_message_create([&bot, &system_prompt, &text_model,
                         &vision_model](const dpp::message_create_t &event)
                            -> dpp::task<void> {
    bool svar = false;

    // Lag en variabel med botens id for fjerning i input-meldinger
    std::string bot_tag = std::format("<@{}>", bot.me.id.str());

    // Hent server og kanal, test mot aksepterte kombinasjoner

    dpp::guild *current_server = dpp::find_guild(event.msg.guild_id);
    dpp::channel *current_chan = dpp::find_channel(event.msg.channel_id);
    if (current_server && current_chan) {
      if ((current_server->name == "tyfon's server" &&
           current_chan->name == "general") ||
          (current_server->name == "Electric Vehicles Enthusiasts" &&
           current_chan->name == "botspam"))
        svar = true;
    }

    // Sjekke vektoren med mentions om botens ID er her og om vi skal svare

    for (const std::pair<dpp::user, dpp::guild_member> &mention :
         event.msg.mentions) {
      if (mention.second.user_id == bot.me.id && svar) {

        // Bot funnet, lag svar via ollama.

        // Sjekk om det er noen bilder i meldingen
        ollama::images imagelist;

        for (auto attachment : event.msg.attachments) {
          bot.log(dpp::loglevel::ll_info,
                  std::format("Attachment: {}", attachment.content_type));
          if (attachment.content_type == "image/jpeg" ||
              attachment.content_type == "image/png") {

            // Ved treff, last ned, encode til b64 og legg til vector
            dpp::http_request_completion_t attachment_data =
                co_await bot.co_request(attachment.url, dpp::m_get);
            bot.log(dpp::loglevel::ll_info,
                    std::format("Image size: {}", attachment_data.body.size()));
            imagelist.push_back(ollama::image(
                macaron::Base64::Encode(std::string(attachment_data.body))));
            // llama 3.2 støtter bare ett bilde, bryt ut av loop
            break;
          }
        }

        bot.log(dpp::loglevel::ll_info,
                std::format("Number of images: {}", imagelist.size()));

        bot.log(dpp::loglevel::ll_info,
                std::format("message from {}: {}",
                            event.msg.author.format_username(),
                            event.msg.content));

        // Lag prompt
        //
        std::string prompt =
            std::format("This is the message by <@{}> that you reply to: {}",
                        event.msg.author.id.str(), event.msg.content);

        // Legg til meldingen det ble svart på dersom denne eksisterer
        if (event.msg.message_reference.message_id) {

          dpp::confirmation_callback_t result = co_await bot.co_message_get(
              event.msg.message_reference.message_id, event.msg.channel_id);
          dpp::message op_message = result.get<dpp::message>();
          bot.log(dpp::loglevel::ll_info,
                  std::format("In reply to [{}]: {}",
                              event.msg.message_reference.message_id.str(),
                              op_message.content));
          prompt.append(std::format(
              "\nThis is a previous message from the conversation: {}",
              op_message.content));
        }

        // Bytt ut bot id med Nissefar
        prompt = std::regex_replace(prompt, std::regex(bot_tag), "Nissefar");

        bot.log(dpp::loglevel::ll_info, std::format("Bot was mentioned by {}",
                                                    event.msg.author.id.str()));
        bot.log(dpp::loglevel::ll_info, std::format("Prompt: {}", prompt));

        // Velg modell basert på om det er bilder eller ikke

        ollama::request req;

        req["system"] = system_prompt;
        req["prompt"] = prompt;

        if (imagelist.size() > 0) {
          req["model"] = vision_model;
          req["images"] = imagelist;
        } else
          req["model"] = text_model;

        std::string answer;
        try {
          answer = ollama::generate(req);
        } catch (ollama::exception e) {
          event.reply(std::format("Exception running llm: {}", e.what()), true);
        }
        event.reply(answer, true);
      }
    }
    co_return;
  });

  bot.start(dpp::st_wait);
}
