#include <AnalyticsQuery.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <set>
#include <string>

#include <ollama.hpp>

namespace {

struct EmojiFilter {
  enum class Mode { Exact, Regex };
  Mode mode;
  std::string value;
};

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string trim_copy(const std::string &value) {
  const auto start = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  if (start == value.end()) {
    return "";
  }
  const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();
  return std::string(start, end);
}

bool is_simple_emoji_name(const std::string &value) {
  if (value.empty()) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_';
  });
}

bool split_custom_emoji_payload(const std::string &payload, std::string &name,
                                std::string &id) {
  std::string core = payload;
  if (!core.empty() && core[0] == 'a' && core.size() >= 3 && core[1] == ':') {
    core = core.substr(2);
  }

  const auto colon_pos = core.find(':');
  if (colon_pos == std::string::npos) {
    return false;
  }

  name = core.substr(0, colon_pos);
  id = core.substr(colon_pos + 1);
  if (!is_simple_emoji_name(name) || id.empty()) {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
    return std::isdigit(ch) != 0;
  });
}

std::optional<EmojiFilter> normalize_emoji_filter(const std::string &raw) {
  const std::string token = trim_copy(raw);
  if (token.empty()) {
    return std::nullopt;
  }

  if (token.size() >= 3 && token.front() == ':' && token.back() == ':') {
    const std::string name = token.substr(1, token.size() - 2);
    if (!is_simple_emoji_name(name)) {
      return std::nullopt;
    }
    return EmojiFilter{EmojiFilter::Mode::Regex,
                       std::format("^<:(?:a:)?{}:[0-9]+>$", name)};
  }

  if (token.size() >= 4 && token.front() == '<' && token.back() == '>') {
    std::string name;
    std::string id;
    if (token[1] == ':') {
      if (!split_custom_emoji_payload(token.substr(2, token.size() - 3), name, id)) {
        return std::nullopt;
      }
      return EmojiFilter{EmojiFilter::Mode::Regex,
                         std::format("^<:(?:a:)?{}:[0-9]+>$", name)};
    }

    if (token[1] == 'a' && token.size() > 5 && token[2] == ':') {
      if (!split_custom_emoji_payload(token.substr(3, token.size() - 4), name, id)) {
        return std::nullopt;
      }
      return EmojiFilter{EmojiFilter::Mode::Regex,
                         std::format("^<:(?:a:)?{}:[0-9]+>$", name)};
    }
  }

  if (is_simple_emoji_name(token)) {
    return EmojiFilter{EmojiFilter::Mode::Regex,
                       std::format("^<:(?:a:)?{}:[0-9]+>$", token)};
  }

  return EmojiFilter{EmojiFilter::Mode::Exact, token};
}

std::string time_filter_sql(const std::string &time_range, const std::string &time_expr) {
  if (time_range == "all_time") {
    return "1 = 1";
  }
  if (time_range == "last_7d") {
    return std::format("{} >= now() - interval '7 days'", time_expr);
  }
  if (time_range == "last_30d") {
    return std::format("{} >= now() - interval '30 days'", time_expr);
  }
  if (time_range == "this_month") {
    return std::format("{} >= date_trunc('month', now())", time_expr);
  }
  if (time_range == "last_month") {
    return std::format(
        "{} >= date_trunc('month', now()) - interval '1 month' and {} < "
        "date_trunc('month', now())",
        time_expr, time_expr);
  }

  return "";
}

bool parse_limit(const ollama::json &request, int default_limit, int max_limit,
                 int &out_limit) {
  out_limit = default_limit;
  if (!request.contains("limit")) {
    return true;
  }

  if (!request["limit"].is_number_integer()) {
    return false;
  }

  int parsed = request["limit"].get<int>();
  if (parsed < 1) {
    parsed = 1;
  }
  if (parsed > max_limit) {
    parsed = max_limit;
  }
  out_limit = parsed;
  return true;
}

bool parse_emoji_filters(const ollama::json &request,
                         std::vector<EmojiFilter> &filters,
                         std::vector<std::string> &bind_params,
                         std::string &error) {
  if (!request.contains("filters") || !request["filters"].is_object()) {
    return true;
  }

  const auto &obj = request["filters"];
  if (!obj.contains("emojis")) {
    return true;
  }
  if (!obj["emojis"].is_array()) {
    error = "filters.emojis must be an array.";
    return false;
  }

  std::set<std::string> dedupe;
  for (const auto &item : obj["emojis"]) {
    if (!item.is_string()) {
      error = "filters.emojis values must be strings.";
      return false;
    }

    const std::string raw = trim_copy(item.get<std::string>());
    if (raw.empty()) {
      continue;
    }

    auto filter = normalize_emoji_filter(raw);
    if (!filter.has_value()) {
      error = std::format("unsupported emoji token '{}'.", raw);
      return false;
    }

    const std::string key =
        std::format("{}:{}", filter->mode == EmojiFilter::Mode::Exact ? "eq" : "rx",
                    filter->value);
    if (dedupe.contains(key)) {
      continue;
    }
    dedupe.insert(key);
    filters.push_back(*filter);
  }

  if (filters.size() > 12) {
    error = "at most 12 emoji filters are allowed.";
    return false;
  }

  for (const auto &filter : filters) {
    bind_params.push_back(filter.value);
  }
  return true;
}

std::string build_emoji_clause(const std::vector<EmojiFilter> &filters,
                               int first_param_index) {
  if (filters.empty()) {
    return "1 = 1";
  }

  std::string clause = "(";
  for (std::size_t i = 0; i < filters.size(); ++i) {
    if (i > 0) {
      clause += " or ";
    }
    const int param_index = first_param_index + static_cast<int>(i);
    if (filters[i].mode == EmojiFilter::Mode::Exact) {
      clause += std::format("r.reaction = ${}", param_index);
    } else {
      clause += std::format("r.reaction ~ ${}", param_index);
    }
  }
  clause += ")";
  return clause;
}

} // namespace

namespace analytics_query {

ParseResult parse_and_compile(const std::string &request_json) {
  ollama::json request;
  try {
    request = ollama::json::parse(request_json);
  } catch (...) {
    return {std::nullopt, "invalid tool arguments JSON."};
  }

  if (!request.is_object()) {
    return {std::nullopt, "request must be a JSON object."};
  }

  if (!request.contains("kind") || !request["kind"].is_string()) {
    return {std::nullopt, "missing required argument 'kind'."};
  }
  if (!request.contains("target") || !request["target"].is_string()) {
    return {std::nullopt, "missing required argument 'target'."};
  }
  if (!request.contains("group_by") || !request["group_by"].is_string()) {
    return {std::nullopt, "missing required argument 'group_by'."};
  }

  const std::string kind = to_lower_copy(request["kind"].get<std::string>());
  const std::string target = to_lower_copy(request["target"].get<std::string>());
  const std::string group_by = to_lower_copy(request["group_by"].get<std::string>());
  const std::string scope =
      request.contains("scope") && request["scope"].is_string()
          ? to_lower_copy(request["scope"].get<std::string>())
          : "channel";
  const std::string time_range =
      request.contains("time_range") && request["time_range"].is_string()
          ? to_lower_copy(request["time_range"].get<std::string>())
          : "last_30d";

  static const std::set<std::string> allowed_kinds = {"leaderboard", "time_series"};
  static const std::set<std::string> allowed_targets = {"reactions", "messages"};
  static const std::set<std::string> allowed_scopes = {"channel", "server"};
  static const std::set<std::string> allowed_time_ranges = {
      "all_time", "last_7d", "last_30d", "this_month", "last_month"};

  if (!allowed_kinds.contains(kind)) {
    return {std::nullopt, "unsupported kind. Use leaderboard or time_series."};
  }
  if (!allowed_targets.contains(target)) {
    return {std::nullopt, "unsupported target. Use reactions or messages."};
  }
  if (!allowed_scopes.contains(scope)) {
    return {std::nullopt, "unsupported scope. Use channel or server."};
  }
  if (!allowed_time_ranges.contains(time_range)) {
    return {std::nullopt, "unsupported time_range."};
  }

  static const std::set<std::string> leaderboard_group_by = {
      "emoji", "message", "reactor", "recipient", "author"};
  static const std::set<std::string> time_series_group_by = {"day", "week", "month"};

  if (kind == "leaderboard" && !leaderboard_group_by.contains(group_by)) {
    return {std::nullopt,
            "unsupported group_by for leaderboard. Use emoji, message, reactor, "
            "recipient, or author."};
  }
  if (kind == "time_series" && !time_series_group_by.contains(group_by)) {
    return {std::nullopt,
            "unsupported group_by for time_series. Use day, week, or month."};
  }

  if (target == "messages" &&
      (group_by == "emoji" || group_by == "reactor" || group_by == "recipient")) {
    return {std::nullopt,
            "unsupported target/group_by combination for messages target."};
  }
  if (target == "reactions" && group_by == "author") {
    return {std::nullopt,
            "unsupported target/group_by combination for reactions target."};
  }

  int limit = 10;
  const int default_limit = kind == "leaderboard" ? 10 : 30;
  const int max_limit = kind == "leaderboard" ? 25 : 120;
  if (!parse_limit(request, default_limit, max_limit, limit)) {
    return {std::nullopt, "limit must be an integer."};
  }

  const std::string time_filter = time_filter_sql(time_range, "m.created_at");
  if (time_filter.empty()) {
    return {std::nullopt, "invalid time range filter."};
  }

  std::vector<EmojiFilter> emoji_filters;
  std::vector<std::string> bind_params;
  std::string emoji_error;
  if (!parse_emoji_filters(request, emoji_filters, bind_params, emoji_error)) {
    return {std::nullopt, emoji_error};
  }

  const std::string scope_filter =
      scope == "server" ? "s.server_snowflake_id = $1" : "c.channel_snowflake_id = $1";
  const std::string scope_join =
      scope == "server" ? " join server s on s.server_id = c.server_id " : "";
  const std::string emoji_filter_clause = build_emoji_clause(emoji_filters, 2);

  std::string sql;
  if (kind == "leaderboard") {
    if (target == "reactions" && group_by == "emoji") {
      sql = std::format(
          "select r.reaction as label, count(*) as value "
          "from reaction r "
          "join message m on m.message_id = r.message_id "
          "join channel c on c.channel_id = m.channel_id "
          "{}"
          "where ({}) and ({}) and ({}) "
          "group by r.reaction "
          "order by value desc, label asc "
          "limit {}",
          scope_join, scope_filter, time_filter, emoji_filter_clause, limit);
    } else if (target == "reactions" && group_by == "reactor") {
      sql = std::format(
          "select u.user_name as label, count(*) as value "
          "from reaction r "
          "join discord_user u on u.user_id = r.user_id "
          "join message m on m.message_id = r.message_id "
          "join channel c on c.channel_id = m.channel_id "
          "{}"
          "where ({}) and ({}) and ({}) "
          "group by u.user_name "
          "order by value desc, label asc "
          "limit {}",
          scope_join, scope_filter, time_filter, emoji_filter_clause, limit);
    } else if (target == "reactions" && group_by == "recipient") {
      sql = std::format(
          "select u.user_name as label, count(*) as value "
          "from reaction r "
          "join message m on m.message_id = r.message_id "
          "join discord_user u on u.user_id = m.user_id "
          "join channel c on c.channel_id = m.channel_id "
          "{}"
          "where ({}) and ({}) and ({}) "
          "group by u.user_name "
          "order by value desc, label asc "
          "limit {}",
          scope_join, scope_filter, time_filter, emoji_filter_clause, limit);
    } else if ((target == "reactions" || target == "messages") &&
               group_by == "message") {
      const std::string reaction_join = target == "reactions"
                                            ? "join reaction r on r.message_id = m.message_id "
                                            : "left join reaction r on r.message_id = m.message_id ";
      sql = std::format(
          "select m.message_snowflake_id::text as message_id, "
          "left(coalesce(m.content, ''), 120) as snippet, "
          "count(r.reaction) as value "
          "from message m "
          "{}"
          "join channel c on c.channel_id = m.channel_id "
          "{}"
          "where ({}) and ({}) and ({}) "
          "group by m.message_snowflake_id, m.content "
          "order by value desc, message_id desc "
          "limit {}",
          reaction_join, scope_join, scope_filter, time_filter, emoji_filter_clause,
          limit);
    } else if (target == "messages" && group_by == "author") {
      sql = std::format(
          "select u.user_name as label, count(*) as value "
          "from message m "
          "join discord_user u on u.user_id = m.user_id "
          "join channel c on c.channel_id = m.channel_id "
          "{}"
          "where ({}) and ({}) "
          "group by u.user_name "
          "order by value desc, label asc "
          "limit {}",
          scope_join, scope_filter, time_filter, limit);
    } else {
      return {std::nullopt, "unsupported leaderboard combination."};
    }
  } else {
    if (target == "messages") {
      sql = std::format(
          "select date_trunc('{}', m.created_at) as bucket_start, count(*) as value "
          "from message m "
          "join channel c on c.channel_id = m.channel_id "
          "{}"
          "where ({}) and ({}) "
          "group by bucket_start "
          "order by bucket_start asc "
          "limit {}",
          group_by, scope_join, scope_filter, time_filter, limit);
    } else {
      sql = std::format(
          "select date_trunc('{}', m.created_at) as bucket_start, count(*) as value "
          "from reaction r "
          "join message m on m.message_id = r.message_id "
          "join channel c on c.channel_id = m.channel_id "
          "{}"
          "where ({}) and ({}) and ({}) "
          "group by bucket_start "
          "order by bucket_start asc "
          "limit {}",
          group_by, scope_join, scope_filter, time_filter, emoji_filter_clause,
          limit);
    }
  }

  return {CompiledQuery{.sql = sql,
                        .bind_params = bind_params,
                        .limit = limit,
                        .scope = scope,
                        .kind = kind,
                        .target = target,
                        .group_by = group_by,
                        .time_range = time_range},
          ""};
}

} // namespace analytics_query
