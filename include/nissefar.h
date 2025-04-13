#ifndef NISSEFAR_H
#define NISSEFAR_H

#include <config.h>
#include <deque>
#include <dpp/dpp.h>
#include <dpp/message.h>
#include <ollama.hpp>
#include <unordered_map>

struct Message {
  const dpp::snowflake msg_id;
  const dpp::snowflake msg_replied_to;
  const std::string content;
  const dpp::snowflake author;
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
                             const ollama::images &imagelist);
  dpp::task<ollama::images>
  generate_images(std::vector<dpp::attachment> attachments);
};

#endif // NISSEFAR_H
