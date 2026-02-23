#include <AnalyticsQuery.h>
#include <Database.h>
#include <DbOps.h>
#include <SqlSafety.h>

#include <exception>
#include <format>

namespace {

std::string escape_json(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped += ch;
      break;
    }
  }
  return escaped;
}

std::string make_markdown_preview(const pqxx::result &res,
                                  const std::string &query_type,
                                  const std::string &metric) {
  if (res.empty() || res.columns() == 0) {
    return "No rows.";
  }

  std::string markdown;
  if (query_type == "leaderboard") {
    markdown += std::format("Top {}\n", metric);
  } else {
    markdown += std::format("{} over time\n", metric);
  }

  markdown += "| ";
  for (pqxx::row::size_type i = 0; i < res.columns(); ++i) {
    if (i > 0) {
      markdown += " | ";
    }
    markdown += res.column_name(i);
  }
  markdown += " |\n|";

  for (pqxx::row::size_type i = 0; i < res.columns(); ++i) {
    if (i > 0) {
      markdown += "|";
    }
    markdown += "---";
  }
  markdown += "|\n";

  const pqxx::result::size_type preview_rows = std::min<pqxx::result::size_type>(res.size(), 15);
  for (pqxx::result::size_type r = 0; r < preview_rows; ++r) {
    markdown += "| ";
    for (pqxx::row::size_type c = 0; c < res[r].size(); ++c) {
      if (c > 0) {
        markdown += " | ";
      }
      markdown += res[r][c].is_null() ? "null" : res[r][c].as<std::string>();
    }
    markdown += " |\n";
  }

  if (res.size() > preview_rows) {
    markdown += std::format("... ({} more rows)", res.size() - preview_rows);
  }

  return markdown;
}

std::string build_json_result(const pqxx::result &res,
                              const analytics_query::CompiledQuery &compiled,
                              const std::string &markdown_preview) {
  std::string output = std::format(
      "{{\"scope\":\"{}\",\"query_type\":\"{}\",\"metric\":\"{}\",\"time_range\":\"{}\","
      "\"interval\":\"{}\",\"limit\":{},\"markdown_preview\":\"{}\","
      "\"columns\":[",
      escape_json(compiled.scope), escape_json(compiled.query_type),
      escape_json(compiled.metric),
      escape_json(compiled.time_range), escape_json(compiled.interval), compiled.limit,
      escape_json(markdown_preview));

  for (pqxx::row::size_type i = 0; i < res.columns(); ++i) {
    if (i > 0) {
      output += ',';
    }
    output += '"';
    output += escape_json(res.column_name(i));
    output += '"';
  }
  output += "],\"rows\":[";

  for (pqxx::result::size_type r = 0; r < res.size(); ++r) {
    if (r > 0) {
      output += ',';
    }
    output += '[';
    for (pqxx::row::size_type c = 0; c < res[r].size(); ++c) {
      if (c > 0) {
        output += ',';
      }
      if (res[r][c].is_null()) {
        output += "null";
      } else {
        output += '"';
        output += escape_json(res[r][c].as<std::string>());
        output += '"';
      }
    }
    output += ']';
  }
  output += "]}";
  return output;
}

} // namespace

namespace dbops {

std::string run_compiled_channel_analytics_query(
    dpp::snowflake channel_id, dpp::snowflake server_id,
    const analytics_query::CompiledQuery &compiled, const std::string &emoji);

pqxx::result fetch_channel_history(dpp::snowflake channel_id, int max_history) {
  auto &db = Database::instance();
  return db.execute("select m.message_id "
                    "     , m.message_snowflake_id "
                    "     , m.reply_to_snowflake_id "
                    "     , u.user_snowflake_id "
                    "     , m.content "
                    "     , m.image_descriptions "
                    "from message m "
                    "inner join discord_user u on (u.user_id = m.user_id) "
                    "inner join channel c on (c.channel_id = m.channel_id) "
                    "where c.channel_snowflake_id = $1 "
                    "order by m.message_id desc limit $2",
                    std::stol(channel_id.str()), max_history);
}

pqxx::result fetch_reactions_for_message(std::uint64_t message_id) {
  auto &db = Database::instance();
  return db.execute("select u.user_snowflake_id "
                    "     , r.reaction "
                    "from reaction r "
                    "inner join discord_user u on (u.user_id = r.user_id) "
                    "where r.message_id = $1",
                    message_id);
}

std::optional<std::uint64_t> find_message_id(dpp::snowflake message_snowflake) {
  auto &db = Database::instance();
  auto res = db.execute(
      "select message_id from message where message_snowflake_id = $1",
      std::stol(message_snowflake.str()));

  if (res.empty()) {
    return std::nullopt;
  }
  return res[0].front().as<std::uint64_t>();
}

void update_message_content(std::uint64_t message_id, const std::string &content) {
  auto &db = Database::instance();
  db.execute("update message set content = $1 where message_id = $2", content,
             message_id);
}

StoredMessageIds store_message(const Message &message, dpp::guild *server,
                               dpp::channel *channel,
                               const std::string &user_name) {
  int server_id{0};
  int channel_id{0};
  int user_id{0};

  auto &db = Database::instance();

  auto res =
      db.execute("select server_id from server where server_snowflake_id = $1",
                 std::stol(server->id.str()));

  if (res.empty()) {
    res = db.execute("insert into server (server_name, server_snowflake_id) "
                     "values ($1, $2) returning server_id",
                     server->name, std::stol(server->id.str()));
    if (!res.empty())
      server_id = res.front()["server_id"].as<int>();
  } else {
    server_id = res.front()["server_id"].as<int>();
  }

  res = db.execute(
      "select channel_id from channel where channel_snowflake_id = $1",
      std::stol(channel->id.str()));

  if (res.empty()) {
    res = db.execute(
        "insert into channel (channel_name, server_id, channel_snowflake_id) "
        "values ($1, $2, $3) returning channel_id",
        channel->name, server_id, std::stol(channel->id.str()));
    if (!res.empty())
      channel_id = res.front()["channel_id"].as<int>();
  } else {
    channel_id = res.front()["channel_id"].as<int>();
  }

  res = db.execute("select user_id from discord_user where user_snowflake_id = $1",
                   std::stol(message.author.str()));

  if (res.empty()) {
    res = db.execute("insert into discord_user (user_name, user_snowflake_id) "
                     "values ($1, $2) returning user_id",
                     user_name, std::stol(message.author.str()));

    if (!res.empty())
      user_id = res.front()["user_id"].as<int>();
  } else {
    user_id = res.front()["user_id"].as<int>();
  }

  res = db.execute(
      "insert into message (user_id, channel_id, content, "
      "message_snowflake_id, reply_to_snowflake_id, image_descriptions, created_at) "
      "values "
      "($1, $2, $3, $4, $5, $6, to_timestamp($7)) returning message_id",
      user_id, channel_id, message.content, std::stol(message.msg_id.str()),
      std::stol(message.msg_replied_to.str()), message.image_descriptions,
      message.created_at_unix);

  return StoredMessageIds{server_id, channel_id, user_id,
                          res.front()["message_id"].as<int>()};
}

pqxx::result fetch_chanstats(dpp::snowflake channel_id, dpp::snowflake bot_id) {
  auto &db = Database::instance();
  return db.execute(
      "select"
      "  u.user_name "
      ", count(*) as nmsgs "
      ", sum(coalesce(array_length(image_descriptions, 1),0)) as nimages "
      "from message m "
      "inner join discord_user u on (m.user_id = u.user_id) "
      "inner join channel c on (m.channel_id = c.channel_id) "
      "where c.channel_snowflake_id = $1 "
      "and u.user_snowflake_id != $2 "
      "group by u.user_name "
      "order by nmsgs desc, nimages desc "
      " limit 20",
      std::stol(channel_id.str()), std::stol(bot_id.str()));
}

std::optional<std::uint64_t>
find_reaction_id(dpp::snowflake reacting_user_id, dpp::snowflake message_id,
                 const std::string &emoji) {
  auto &db = Database::instance();
  auto react_res =
      db.execute("select r.reaction_id "
                 "from reaction r "
                 "inner join message m on (m.message_id = r.message_id) "
                 "inner join discord_user u on (u.user_id = r.user_id) "
                 "where u.user_snowflake_id = $1 "
                 "and m.message_snowflake_id = $2 "
                 "and r.reaction = $3",
                 std::stol(reacting_user_id.str()), std::stol(message_id.str()),
                 emoji);

  if (react_res.empty()) {
    return std::nullopt;
  }
  return react_res[0].front().as<std::uint64_t>();
}

std::optional<std::uint64_t> find_user_id(dpp::snowflake user_snowflake) {
  auto &db = Database::instance();
  auto user_res = db.execute("select user_id from discord_user "
                             "where user_snowflake_id = $1",
                             std::stol(user_snowflake.str()));
  if (user_res.empty()) {
    return std::nullopt;
  }
  return user_res[0].front().as<std::uint64_t>();
}

void delete_reaction(std::uint64_t reaction_id) {
  auto &db = Database::instance();
  db.execute("delete from reaction where reaction_id = $1", reaction_id);
}

void insert_reaction(std::uint64_t message_id, std::uint64_t user_id,
                     const std::string &emoji) {
  auto &db = Database::instance();
  db.execute("insert into reaction (message_id, user_id, reaction) "
             "values ($1, $2, $3)",
             message_id, user_id, emoji);
}

std::string run_channel_analytics_query(dpp::snowflake channel_id,
                                        const std::string &sql) {
  const auto validation = sql_safety::validate_and_rewrite_channel_query(sql);
  if (!validation.ok()) {
    return std::format("Tool error: blocked SQL query: {}", validation.error);
  }

  auto &db = Database::instance();

  pqxx::result res;
  try {
    const std::string wrapped_sql =
        "select * from (" + validation.rewritten_sql +
        ") as analytics_result limit 50";
    pqxx::params params;
    params.append(std::stol(channel_id.str()));
    res = db.execute_with_session_limits(wrapped_sql, params, 2500, 500, 3000, true);
  } catch (const std::exception &e) {
    return std::format("Tool error: SQL query failed: {}", e.what());
  }

  std::string output = "{\"columns\":[";
  for (pqxx::row::size_type i = 0; i < res.columns(); ++i) {
    if (i > 0) {
      output += ',';
    }
    output += '"';
    output += escape_json(res.column_name(i));
    output += '"';
  }
  output += "],\"rows\":[";

  for (pqxx::result::size_type r = 0; r < res.size(); ++r) {
    if (r > 0) {
      output += ',';
    }
    output += '[';
    for (pqxx::row::size_type c = 0; c < res[r].size(); ++c) {
      if (c > 0) {
        output += ',';
      }
      if (res[r][c].is_null()) {
        output += "null";
      } else {
        output += '"';
        output += escape_json(res[r][c].as<std::string>());
        output += '"';
      }
    }
    output += ']';
  }
  output += "]}";

  return output;
}

std::string run_channel_analytics_request(dpp::snowflake channel_id,
                                          dpp::snowflake server_id,
                                          const std::string &request_json) {
  const auto parsed = analytics_query::parse_and_compile(request_json);
  if (!parsed.ok()) {
    return std::format("Tool error: invalid analytics request: {}", parsed.error);
  }

  return run_compiled_channel_analytics_query(channel_id, server_id, *parsed.query,
                                              parsed.emoji);
}

std::string run_compiled_channel_analytics_query(
    dpp::snowflake channel_id, dpp::snowflake server_id,
    const analytics_query::CompiledQuery &compiled, const std::string &emoji) {
  const std::string sql = compiled.sql;


  pqxx::params params;
  if (compiled.scope == "server") {
    if (server_id.str() == "0") {
      return "Tool error: server scope is not available in this context.";
    }
    params.append(std::stol(server_id.str()));
  } else {
    params.append(std::stol(channel_id.str()));
  }
  if (compiled.needs_emoji_param) {
    params.append(emoji);
  }

  pqxx::result res;
  try {
    res = Database::instance().execute_with_session_limits(sql, params, 2500, 500,
                                                           3000, true);
  } catch (const std::exception &e) {
    return std::format("Tool error: analytics query failed: {}", e.what());
  }

  const std::string markdown_preview =
      make_markdown_preview(res, compiled.query_type, compiled.metric);
  return build_json_result(res, compiled, markdown_preview);
}

} // namespace dbops
