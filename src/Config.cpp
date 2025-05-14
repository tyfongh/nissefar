#include <Config.h>
#include <cstdlib>
#include <format>
#include <inicpp.h>

Config::Config()
    : Config([] {
        const char *home = std::getenv("HOME");
        if (!home)
          return Config(false, std::string(""), std::string(""),
                        std::string(""), std::string(""), std::string(""),
                        std::string(""), std::string(""), std::string(""),
                        std::string(""), std::string(""), std::string(""),
                        std::string(""), 0);

        ini::IniFile ini;
        ini.setMultiLineValues(true);
        ini.load(std::format("{}/.config/nissefar/config.ini", home));

        bool valid = true;

        std::string discord_token =
            ini["General"]["discord_token"].as<std::string>();
        std::string google_api_key =
            ini["General"]["google_api_key"].as<std::string>();
        std::string system_prompt =
            ini["General"]["system_prompt"].as<std::string>();
        std::string diff_system_prompt =
            ini["General"]["diff_system_prompt"].as<std::string>();
        std::string image_description_system_prompt =
            ini["General"]["image_description_system_prompt"].as<std::string>();
        std::string reaction_system_prompt =
            ini["General"]["reaction_system_prompt"].as<std::string>();
        std::string text_model = ini["General"]["text_model"].as<std::string>();
        std::string comparison_model =
            ini["General"]["comparison_model"].as<std::string>();
        std::string vision_model =
            ini["General"]["vision_model"].as<std::string>();
        std::string image_description_model =
            ini["General"]["image_description_model"].as<std::string>();
        std::string reaction_model =
            ini["General"]["reaction_model"].as<std::string>();

        std::string db_connection_string =
            ini["Database"]["db_connection_string"].as<std::string>();

        int max_history = ini["General"]["max_history"].as<int>();

        if (discord_token.empty() || google_api_key.empty() ||
            system_prompt.empty() || diff_system_prompt.empty() ||
            reaction_system_prompt.empty() || text_model.empty() ||
            comparison_model.empty() || vision_model.empty() ||
            image_description_model.empty() || reaction_model.empty() ||
            db_connection_string.empty() || max_history == 0)
          valid = false;

        return Config(valid, discord_token, google_api_key, system_prompt,
                      diff_system_prompt, image_description_system_prompt,
                      reaction_system_prompt, text_model, comparison_model,
                      vision_model, image_description_model, reaction_model,
                      db_connection_string, max_history);
      }()) {}

Config::Config(bool valid, std::string discord_token,
               std::string google_api_key, std::string system_prompt,
               std::string diff_system_prompt,
               std::string image_description_system_prompt,
               std::string reaction_system_prompt, std::string text_model,
               std::string comparison_model, std::string vision_model,
               std::string image_description_model, std::string reaction_model,
               std::string db_connection_string, int max_history)
    : discord_token(std::move(discord_token)),
      google_api_key(std::move(google_api_key)),
      system_prompt(std::move(system_prompt)),
      diff_system_prompt(std::move(diff_system_prompt)),
      image_description_system_prompt(
          std::move(image_description_system_prompt)),
      reaction_system_prompt(std::move(reaction_system_prompt)),
      text_model(std::move(text_model)),
      comparison_model(std::move(comparison_model)),
      vision_model(std::move(vision_model)),
      image_description_model(std::move(image_description_model)),
      reaction_model(std::move(reaction_model)),
      db_connection_string(std::move(db_connection_string)), is_valid(valid),
      max_history(max_history) {
  directory_url = std::format("https://www.googleapis.com/drive/v3/"
                              "files?q='1HOwktdiZmm40atGPwymzrxErMi1ZrKPP'+in+"
                              "parents&key={}&fields=files("
                              "id,name,modifiedTime,webViewLink)",
                              this->google_api_key);
  youtube_url =
      std::format("https://www.googleapis.com/youtube/v3/"
                  "search?part=snippet&channelId=UCD3YwI6vR9BSHufERd4sqwQ&"
                  "eventType=live&type=video&key={}",
                  this->google_api_key);
}
