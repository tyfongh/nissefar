#include <Config.h>
#include <cstdlib>
#include <format>
#include <inicpp.h>
#include <sstream>
#include <vector>

Config::Config()
    : Config([] {
        const char *home = std::getenv("HOME");
        if (!home)
          return Config(false, std::string(""), std::string(""),
                        std::string(""), std::string(""), std::string(""),
                        std::string(""), std::string(""), std::string(""),
                        std::string(""), std::string(""), std::string(""),
                        std::string(""), 0, 40000);

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
        std::string text_model = ini["General"]["text_model"].as<std::string>();
        std::string comparison_model =
            ini["General"]["comparison_model"].as<std::string>();
        std::string vision_model =
            ini["General"]["vision_model"].as<std::string>();
        std::string image_description_model =
            ini["General"]["image_description_model"].as<std::string>();
        std::string ollama_server_url = "http://localhost:11434";

        try {
          std::string configured_url =
              ini["General"]["ollama_server_url"].as<std::string>();
          if (!configured_url.empty())
            ollama_server_url = configured_url;
        } catch (...) {
        }

        std::string db_connection_string =
            ini["Database"]["db_connection_string"].as<std::string>();

        std::string video_summary_script_path;
        try {
          video_summary_script_path =
              ini["General"]["video_summary_script_path"].as<std::string>();
        } catch (...) {
        }

        int max_history = ini["General"]["max_history"].as<int>();
        int context_size = 40000;

        try {
          int configured_context_size = ini["General"]["context_size"].as<int>();
          if (configured_context_size > 0) {
            context_size = configured_context_size;
          }
        } catch (...) {
        }

        int rate_limit_count = 3;
        try {
          int v = ini["General"]["rate_limit_count"].as<int>();
          if (v > 0)
            rate_limit_count = v;
        } catch (...) {
        }

        int rate_limit_window_seconds = 300;
        try {
          int v = ini["General"]["rate_limit_window_seconds"].as<int>();
          if (v > 0)
            rate_limit_window_seconds = v;
        } catch (...) {
        }

        std::string youtube_summary_bot_id;
        try {
          youtube_summary_bot_id =
              ini["General"]["youtube_summary_bot_id"].as<std::string>();
        } catch (...) {
        }

        std::string youtube_summary_channel_id;
        try {
          youtube_summary_channel_id =
              ini["General"]["youtube_summary_channel_id"].as<std::string>();
        } catch (...) {
        }

        std::string owner_id;
        try {
          owner_id = ini["General"]["owner_id"].as<std::string>();
        } catch (...) {
        }

        std::vector<std::string> allowed_channels = {"botspam"};
        try {
          std::string csv =
              ini["General"]["allowed_channels"].as<std::string>();
          if (!csv.empty()) {
            std::vector<std::string> parsed;
            std::istringstream ss(csv);
            std::string token;
            while (std::getline(ss, token, ',')) {
              auto s = token.find_first_not_of(" \t");
              auto e = token.find_last_not_of(" \t");
              if (s != std::string::npos)
                parsed.push_back(token.substr(s, e - s + 1));
            }
            if (!parsed.empty())
              allowed_channels = std::move(parsed);
          }
        } catch (...) {
        }

        std::vector<std::string> youtube_skip_channel_names;
        try {
          std::string csv =
              ini["General"]["youtube_skip_channel_names"].as<std::string>();
          if (!csv.empty()) {
            std::istringstream ss(csv);
            std::string token;
            while (std::getline(ss, token, ',')) {
              auto s = token.find_first_not_of(" \t");
              auto e = token.find_last_not_of(" \t");
              if (s != std::string::npos)
                youtube_skip_channel_names.push_back(token.substr(s, e - s + 1));
            }
          }
        } catch (...) {
        }

        if (discord_token.empty() || google_api_key.empty() ||
            system_prompt.empty() || diff_system_prompt.empty() ||
            text_model.empty() || comparison_model.empty() ||
            vision_model.empty() || image_description_model.empty() ||
            db_connection_string.empty() || max_history == 0)
          valid = false;

        return Config(valid, discord_token, google_api_key, system_prompt,
                        diff_system_prompt, image_description_system_prompt,
                        text_model, comparison_model, vision_model,
                        image_description_model, ollama_server_url,
                        db_connection_string, video_summary_script_path,
                        max_history, context_size, rate_limit_count,
                        rate_limit_window_seconds, youtube_summary_bot_id,
                        youtube_summary_channel_id, owner_id,
                        allowed_channels, youtube_skip_channel_names);
      }()) {}

Config::Config(bool valid, std::string discord_token,
               std::string google_api_key, std::string system_prompt,
               std::string diff_system_prompt,
               std::string image_description_system_prompt,
               std::string text_model, std::string comparison_model,
               std::string vision_model, std::string image_description_model,
               std::string ollama_server_url,
               std::string db_connection_string,
               std::string video_summary_script_path, int max_history,
               int context_size, int rate_limit_count,
               int rate_limit_window_seconds,
               std::string youtube_summary_bot_id,
               std::string youtube_summary_channel_id,
               std::string owner_id,
               std::vector<std::string> allowed_channels,
               std::vector<std::string> youtube_skip_channel_names)
    : discord_token(std::move(discord_token)),
      google_api_key(std::move(google_api_key)),
      max_history(max_history),
      context_size(context_size),
      rate_limit_count(rate_limit_count),
      rate_limit_window_seconds(rate_limit_window_seconds),
      system_prompt(std::move(system_prompt)),
      diff_system_prompt(std::move(diff_system_prompt)),
      image_description_system_prompt(
          std::move(image_description_system_prompt)),
      text_model(std::move(text_model)),
      comparison_model(std::move(comparison_model)),
      vision_model(std::move(vision_model)),
      image_description_model(std::move(image_description_model)),
      ollama_server_url(std::move(ollama_server_url)),
      db_connection_string(std::move(db_connection_string)),
      video_summary_script_path(std::move(video_summary_script_path)),
      youtube_summary_bot_id(std::move(youtube_summary_bot_id)),
      youtube_summary_channel_id(std::move(youtube_summary_channel_id)),
      owner_id(std::move(owner_id)),
      allowed_channels(std::move(allowed_channels)),
      youtube_skip_channel_names(std::move(youtube_skip_channel_names)),
      is_valid(valid) {
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
