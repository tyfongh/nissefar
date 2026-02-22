#ifndef DBOPS_H
#define DBOPS_H

#include <Domain.h>
#include <optional>
#include <pqxx/pqxx>

namespace dbops {

pqxx::result fetch_channel_history(dpp::snowflake channel_id, int max_history);
pqxx::result fetch_reactions_for_message(std::uint64_t message_id);
std::optional<std::uint64_t> find_message_id(dpp::snowflake message_snowflake);
void update_message_content(std::uint64_t message_id, const std::string &content);

struct StoredMessageIds {
  int server_id;
  int channel_id;
  int user_id;
  int message_id;
};

StoredMessageIds store_message(const Message &message, dpp::guild *server,
                               dpp::channel *channel,
                               const std::string &user_name);

pqxx::result fetch_chanstats(dpp::snowflake channel_id, dpp::snowflake bot_id);

std::optional<std::uint64_t>
find_reaction_id(dpp::snowflake reacting_user_id, dpp::snowflake message_id,
                 const std::string &emoji);

std::optional<std::uint64_t> find_user_id(dpp::snowflake user_snowflake);

void delete_reaction(std::uint64_t reaction_id);
void insert_reaction(std::uint64_t message_id, std::uint64_t user_id,
                     const std::string &emoji);

} // namespace dbops

#endif // DBOPS_H
