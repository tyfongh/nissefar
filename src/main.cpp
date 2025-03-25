#include <chrono>
#include <dpp/dpp.h>
#include <format>
#include <inicpp.h>
#include <ollama.hpp>
#include <stdlib.h>
#include <string>

int main() {

  const char *home = getenv("HOME");
  if (home == NULL) {
    std::cout << "Can't get home directory" << std::endl;
    return -1;
  }

  // Read the configuration parameters from ini file
  ini::IniFile config;
  config.setMultiLineValues(true);
  config.load(std::format("{}/.config/nissefar/config", home));

  const std::string token = config["General"]["token"].as<std::string>();
  const std::string system_prompt =
      config["General"]["system_prompt"].as<std::string>();
  const std::string text_model =
      config["General"]["text_model"].as<std::string>();
  const std::string vision_model =
      config["General"]["vision_model"].as<std::string>();
  const std::string google_api =
      config["General"]["google_api"].as<std::string>();

  // Map to store the file metadata for TB test

  std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>>
      tbfiles;

  // Create url to fetch file data
  const std::string directory_url =
      std::format("https://www.googleapis.com/drive/v3/"
                  "files?q='1HOwktdiZmm40atGPwymzrxErMi1ZrKPP'+in+parents&"
                  "key={}&fields=files(id,name,modifiedTime,webViewLink)",
                  google_api);

  dpp::cluster bot(token, dpp::i_default_intents | dpp::i_message_content);
  bot.on_log(dpp::utility::cout_logger());

  bot.log(dpp::loglevel::ll_info,
          std::format("System prompt: {}", system_prompt));

  ollama::setReadTimeout(360);
  ollama::setWriteTimeout(360);

  // Set opp en timer som fyrer av hvert minutt
  bot.on_ready([&bot, &directory_url, &text_model,
                &tbfiles](const dpp::ready_t &event) -> dpp::task<void> {
    // Hent katalogdata fra google drive, setup pull
    dpp::http_request_completion_t response =
        co_await bot.co_request(directory_url, dpp::m_get);
    bot.log(dpp::ll_info,
            std::format("json data size: {}", response.body.size()));

    nlohmann::json directory_data = nlohmann::json::parse(response.body.data());

    // Loop over filene og hent ut filnavn og parse tid
    for (auto filedata : directory_data["files"]) {

      // try to parse the data
      // 2024-09-19T07:45:19.173Z

      std::string datestring = filedata["modifiedTime"].front();
      std::string filename = filedata["name"].front();
      std::string datestringparse =
          datestring.substr(0, 10) + " " + datestring.substr(12, 8);

      std::chrono::sys_time<std::chrono::milliseconds> tp;
      std::istringstream ds(datestringparse);

      // Dersom parse er ok, stapp i map for senere bruk
      ds >> std::chrono::parse("%Y-%m-%d %H:%M:%S", tp);
      if (ds.fail()) {
        bot.log(dpp::ll_error, std::format("Error parsing date: {}", ds.str()));
      } else {
        bot.log(dpp::ll_info, std::format("json test: {}, {}, {}", filename,
                                          datestring, tp.time_since_epoch()));
        tbfiles[filename] = tp;
      }
    }

    // Set opp timeren som skal sjekke filer hvert 5. minutt
    bot.start_timer(
        [&bot, &tbfiles, &directory_url, &text_model](const dpp::timer &timer) {
          bot.log(dpp::loglevel::ll_info, std::format("Check files: 300 sec"));

          // Hent katalogdata, denne gangen async med callback lambda
          bot.request(
              directory_url, dpp::m_get,
              [&tbfiles, &bot,
               &text_model](const dpp::http_request_completion_t &response) {
                bot.log(dpp::ll_info, std::format("json data size: {}",
                                                  response.body.size()));
                nlohmann::json directory_data =
                    nlohmann::json::parse(response.body.data());

                // Loop over fildata og hent ut tid og navn
                for (auto filedata : directory_data["files"]) {
                  std::string datestring = filedata["modifiedTime"].front();
                  std::string filename = filedata["name"].front();
                  std::string datestringparse =
                      datestring.substr(0, 10) + " " + datestring.substr(12, 8);
                  std::chrono::sys_time<std::chrono::milliseconds> tp;
                  std::istringstream ds(datestringparse);

                  // Dersom datoene parser, sjekk mot map om de har endret seg
                  // og log / lagre ny tid dersom de er endret
                  ds >> std::chrono::parse("%Y-%m-%d %H:%M:%S", tp);
                  if (ds.fail()) {
                    bot.log(dpp::ll_error,
                            std::format("Error parsing date: {}", ds.str()));
                  } else {
                    if (tbfiles[filename] < tp) {
                      bot.log(dpp::ll_info,
                              std::format("File {} has changed", filename));
                      std::string message_text;

                      try {
                        message_text = ollama::generate(
                            text_model,
                            std::format(
                                "Make a witty comment about that Bjørn just "
                                "updated the following google shiiiet file: {}",
                                filename));
                      } catch (ollama::exception &e) {
                        message_text =
                            std::format("Exception running llm: {}. But the "
                                        "file {} has been updated by Bjørn",
                                        e.what(), filename);
                      }
                      std::string drive_url = filedata["webViewLink"].front();
                      message_text.append(std::format("\n{}", drive_url));
                      tbfiles[filename] = tp;
                      bot.log(dpp::ll_info,
                              std::format("Bot answer: {}", message_text));
                      dpp::message msg(1267731118895927347, message_text);
                      bot.message_create(msg);
                    }
                  }
                }
              });
        },
        300);
  });

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
        ollama::options opts;

        opts["num_predict"] = 1000;

        req["system"] = system_prompt;
        req["prompt"] = prompt;
        req["options"] = opts["options"];

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
