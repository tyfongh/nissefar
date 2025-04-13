#ifndef NISSEFAR_H
#define NISSEFAR_H

#include <config.h>
#include <deque>
#include <dpp/dpp.h>
#include <dpp/snowflake.h>
#include <unordered_map>

struct Message {
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
  const std::deque<Message>& get_channel_history(dpp::snowflake channel_id) const;
};

#endif // NISSEFAR_H
