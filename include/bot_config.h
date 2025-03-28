#ifndef BOT_CONFIG_H
#define BOT_CONFIG_H

#include <chrono>
#include <string>
#include <map>

class bot_config {
public:
  std::string discord_token;
  std::string system_prompt;
  std::string text_model;
  std::string vision_model;
  std::string google_api_key;
  std::string directory_url;
  std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>>
      tbfiles;
  bool is_valid;

  bot_config();
};

#endif // BOT_CONFIG_H
