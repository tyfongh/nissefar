#ifndef NISSEFAR_H
#define NISSEFAR_H

#include <Config.h>
#include <chrono>
#include <dpp/dpp.h>
#include <ollama.hpp>
#include <pqxx/pqxx>
#include <string_view>

struct Message {
  const dpp::snowflake msg_id;
  const dpp::snowflake msg_replied_to;
  const std::string content;
  const dpp::snowflake author;
  const std::vector<std::string> image_descriptions;
};

struct Diffdata {
  std::string diffdata;
  std::string weblink;
  std::string header;
};

class Nissefar {
private:
  // variables

  Config config{};
  std::unique_ptr<dpp::cluster> bot;

  enum class GenerationType { TextReply, Diff, ImageDescription };

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

  // Methods

  dpp::task<void> handle_message(const dpp::message_create_t &event);
  dpp::task<void> handle_message_update(const dpp::message_update_t &event);
  dpp::task<void> handle_reaction(const dpp::message_reaction_add_t &event);
  dpp::task<void> remove_reaction(const dpp::message_reaction_remove_t &event);
  std::string format_message_history(dpp::snowflake channel_id);
  std::string format_replyto_message(const Message &msg);
  std::string generate_text(const std::string &prompt,
                            const ollama::images &imagelist,
                            const GenerationType gen_type);
  dpp::task<ollama::images>
  generate_images(std::vector<dpp::attachment> attachments);
  dpp::task<void> process_google_docs();
  dpp::task<void> process_youtube(bool first_run);
  dpp::task<void> process_sheets(const std::string filename,
                                 const std::string file_id,
                                 std::string weblink);
  std::string diff(const std::string olddata, const std::string newdata,
                   const int sheet_id);

  void process_diffs();
  void store_message(const Message &message, dpp::guild *server,
                     dpp::channel *channel, const std::string user_name);

  dpp::task<void> setup_slashcommands();
  dpp::task<void> handle_slashcommand(const dpp::slashcommand_t &event);

  size_t utf8_len(std::string &text);
  void pad_right(std::string &text, const size_t num, std::string pad_char);
  void pad_left(std::string &text, const size_t num, std::string pad_char);

  std::string format_chanstat(const pqxx::result res, std::string channel);

public:
  Nissefar();
  void run();
};
#endif // NISSEFAR_H
