#include <dpp/dpp.h>
#include <format>
#include <fstream>
#include <ollama.hpp>
#include <string>

const std::string CONFIG_PATH = "/home/tyfon/.config/nissefar/token.txt";
const std::string SYSTEM_PROMPT_PATH =
    "/home/tyfon/.config/nissefar/system_prompt.txt";

std::string read_token() {
  std::string token;
  std::ifstream f(CONFIG_PATH);
  std::getline(f, token);
  if (!token.empty() && token[token.length() - 1] == '\n')
    token.erase(token.length() - 1);
  return token;
}

std::string read_system_prompt() {
  std::string system_prompt;
  std::string line;
  std::ifstream f(SYSTEM_PROMPT_PATH);
  while (std::getline(f, line)) {
    system_prompt.append(line);
  }
  return system_prompt;
}

int main() {

  const auto token = read_token();
  const auto system_prompt = read_system_prompt();

  dpp::cluster bot(token, dpp::i_default_intents | dpp::i_message_content);
  bot.on_log(dpp::utility::cout_logger());

  bot.log(dpp::loglevel::ll_info,
          std::format("System prompt: {}", system_prompt));

  // Sett antall tokens ut til 100 for å begrense meldingslengden

  ollama::options opts;
  opts["num_predict"] = 100;

  // Vi ønsker kun å svare på melding til boten i spesifikke kanaler

  bot.on_message_create([&bot,
                         &system_prompt](const dpp::message_create_t &event)
                            -> dpp::task<void> {
    bool svar = false;

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

        ollama::images imagelist;

        // Sjekk om det er noen bilder i meldingen

        for (auto attachment : event.msg.attachments) {
          bot.log(dpp::loglevel::ll_info,
                  std::format("Attachment: {}", attachment.content_type));
          if (attachment.content_type == "image/jpeg") {

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

        // Bot funnet, lag svar via ollama.

        bot.log(dpp::loglevel::ll_info, std::format("Bot was mentioned by {}",
                                                    event.msg.author.id.str()));
        bot.log(dpp::loglevel::ll_info, std::format("Prompt: {}", prompt));

        // Velg modell basert på om det er bilder eller ikke

        ollama::request req;

        if (imagelist.size() > 0) {
          req["model"] = "llama3.2-vision:11b-instruct-q8_0";
          req["images"] = imagelist;
        } else
          req["model"] = "mistral-small:24b-instruct-2501-q4_K_M";

        req["system"] = system_prompt;
        req["prompt"] = prompt;

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
