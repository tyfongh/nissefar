#ifndef BOT_CONFIG_H
#define BOT_CONFIG_H

#include <string>

class Config {
public:
  // Keys should not change
  const std::string discord_token;
  const std::string google_api_key;
  const int max_history;

  // Rest might be user settable

  std::string system_prompt;
  std::string diff_system_prompt;
  std::string image_description_system_prompt;
  std::string text_model;
  std::string comparison_model;
  std::string vision_model;
  std::string image_description_model;
  std::string db_connection_string;
  std::string directory_url;
  std::string youtube_url;

  bool is_valid = false;
  bool is_streaming = false;

  Config();
  Config(bool valid, std::string discord_token, std::string google_api_key,
         std::string system_prompt, std::string diff_system_prompt,
         std::string image_description_system_prompt, std::string text_model,
         std::string comparison_model, std::string vision_model,
         std::string image_description_model, std::string db_connection_string,
         int max_history);
};

#endif // BOT_CONFIG_H
