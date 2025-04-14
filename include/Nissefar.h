#ifndef NISSEFAR_H
#define NISSEFAR_H

#include <Config.h>
#include <chrono>
#include <deque>
#include <dpp/dpp.h>
#include <ollama.hpp>
#include <string_view>
#include <unordered_map>

struct Message {
  const dpp::snowflake msg_id;
  const dpp::snowflake msg_replied_to;
  const std::string content;
  const dpp::snowflake author;
};

struct Diffdata {
  std::string diffdata;
  std::string weblink;
};

class Nissefar {
private:
  Config config{};
  std::unique_ptr<dpp::cluster> bot;
  std::unordered_map<dpp::snowflake, std::deque<Message>> channel_history;

public:
  Nissefar();
  void run();
  dpp::task<void> handle_message(const dpp::message_create_t &event);
  void add_channel_message(dpp::snowflake channel_id, const Message &msg);
  const std::deque<Message> &
  get_channel_history(dpp::snowflake channel_id) const;
  std::string format_message_history(dpp::snowflake channel_id);
  std::string format_replyto_message(const Message &msg);
  std::string generate_reply(const std::string &prompt,
                             const ollama::images &imagelist, bool is_diff);
  dpp::task<ollama::images>
  generate_images(std::vector<dpp::attachment> attachments);
  dpp::task<void> process_google_docs();
  dpp::task<void> process_sheets(const std::string filename,
                                 const std::string file_id,
                                 std::string weblink);
  std::string diff(const std::string olddata, const std::string newdata,
                   const int sheet_id);

  void process_diffs();

  // variables

  std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>>
      timestamps;

  std::map<std::string, std::map<int, std::string>> sheet_data;
  std::map<std::string, std::map<int, Diffdata>> sheet_diffs;

  // To avoid messing with oauth2, just list all documents + sheets

  std::map<std::string_view, std::map<int, std::string_view>> sheet_names = {
      {"TB test results",
       {{0, "Banana"},
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
        {1066718131, "Bangkok"}}},
      {"Charging curves", {{1593904708, "Charging curve"}}}};
};
#endif // NISSEFAR_H
