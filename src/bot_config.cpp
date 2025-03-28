#include <bot_config.h>
#include <inicpp.h>
#include <string>

bot_config::bot_config() {

  // Try to get the home directory
  const char *home = getenv("HOME");
  if (home == NULL) {
    is_valid = false;
    return;
  }

  // Load the ini file
  ini::IniFile config;
  config.setMultiLineValues(true);
  config.load(std::format("{}/.config/nissefar/config.ini", home));

  // Read paramters from the file

  discord_token = config["General"]["discord_token"].as<std::string>();
  system_prompt = config["General"]["system_prompt"].as<std::string>();
  text_model = config["General"]["text_model"].as<std::string>();
  vision_model = config["General"]["vision_model"].as<std::string>();
  google_api_key = config["General"]["google_api_key"].as<std::string>();

  directory_url =
      std::format("https://www.googleapis.com/drive/v3/"
                  "files?q='1HOwktdiZmm40atGPwymzrxErMi1ZrKPP'+in+parents&key={"
                  "}&fields=files(id,name,modifiedTime,webViewLink)",
                  google_api_key);
  is_valid = true;
}
