#ifndef BOT_CONFIG_H
#define BOT_CONFIG_H

#include <string>
#include <vector>

class Config {
public:
  // Keys should not change
  const std::string discord_token;
  const std::string google_api_key;
  const int max_history;
  const int context_size;
  const int rate_limit_count;
  const int rate_limit_window_seconds;

  // Rest might be user settable

  std::string system_prompt;
  std::string diff_system_prompt;
  std::string image_description_system_prompt;
  std::string text_model;
  std::string comparison_model;
  std::string vision_model;
  std::string image_description_model;
  std::string ollama_server_url;
  std::string db_connection_string;
  std::string video_summary_script_path;
  std::string directory_url;
  std::string youtube_url;
  std::string youtube_summary_bot_id;
  std::string youtube_summary_channel_id;
  std::string owner_id;
  std::vector<std::string> allowed_channels;
  std::vector<std::string> youtube_skip_channel_names;

  bool is_valid = false;
  bool is_streaming = false;

  Config();
  Config(bool valid, std::string discord_token, std::string google_api_key,
         std::string system_prompt, std::string diff_system_prompt,
          std::string image_description_system_prompt, std::string text_model,
          std::string comparison_model, std::string vision_model,
          std::string image_description_model, std::string ollama_server_url,
          std::string db_connection_string,
          std::string video_summary_script_path, int max_history,
          int context_size, int rate_limit_count = 3,
          int rate_limit_window_seconds = 300,
          std::string youtube_summary_bot_id = {},
          std::string youtube_summary_channel_id = {},
          std::string owner_id = {},
          std::vector<std::string> allowed_channels = {"botspam"},
          std::vector<std::string> youtube_skip_channel_names = {});
};

#endif // BOT_CONFIG_H
