#ifndef BOT_CONFIG_H
#define BOT_CONFIG_H

#include <chrono>
#include <map>
#include <string>
#include <string_view>

class bot_config {
public:
  std::string discord_token;
  std::string system_prompt;
  std::string text_model;
  std::string comparison_model;
  std::string vision_model;
  std::string google_api_key;
  std::string directory_url;
  std::string sheet_url;
  std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>>
      tbfiles;
  bool is_valid;
  bool timer_is_running;

  const std::map<int, std::string_view> tb_test_sheets = {
      {0, "Banana"},
      {1865415711, "Weight"},
      {378787627, "Acceleration"},
      {2069101638, "Noise"},
      {26964202, "Braking"},
      {735351678, "Range"},
      {866693557, "Sunday"},
      {15442336, "1000 km"},
      {1229113299, "500 km"},
      {2118810793, "Geilo"},
      {244400016, "Degradation"},
      {52159941, "Zero mile"},
      {478179452, "Arctic Circle"},
      {1066718131, "Bangkok"}};

  const std::map<int, std::string_view> charge_curve_sheets = {
      {1593904708, "charging curve"}};

  std::map<int, std::string> sheets_data;

  bot_config();
};

#endif // BOT_CONFIG_H
