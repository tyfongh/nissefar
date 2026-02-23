#include <AnalyticsQuery.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <set>

#include <ollama.hpp>

namespace {

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
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

} // namespace

namespace analytics_query {

ParseResult parse_and_compile(const std::string &request_json) {
  ollama::json request;
  try {
    request = ollama::json::parse(request_json);
  } catch (...) {
    return {std::nullopt, "", "invalid tool arguments JSON."};
  }

  if (!request.is_object()) {
    return {std::nullopt, "", "request must be a JSON object."};
  }

  if (!request.contains("query_type") || !request["query_type"].is_string()) {
    return {std::nullopt, "", "missing required argument 'query_type'."};
  }
  if (!request.contains("metric") || !request["metric"].is_string()) {
    return {std::nullopt, "", "missing required argument 'metric'."};
  }

  const std::string query_type = to_lower_copy(request["query_type"].get<std::string>());
  const std::string metric = to_lower_copy(request["metric"].get<std::string>());
  const std::string scope =
      request.contains("scope") && request["scope"].is_string()
          ? to_lower_copy(request["scope"].get<std::string>())
          : "channel";
  const std::string time_range =
      request.contains("time_range") && request["time_range"].is_string()
          ? to_lower_copy(request["time_range"].get<std::string>())
          : "last_30d";

  static const std::set<std::string> allowed_query_types = {"leaderboard",
                                                            "time_series"};
  static const std::set<std::string> allowed_metrics = {
      "messages", "images", "reactions_given", "reactions_received"};
  static const std::set<std::string> allowed_time_ranges = {
      "all_time", "last_7d", "last_30d", "this_month", "last_month"};
  static const std::set<std::string> allowed_scopes = {"channel", "server"};

  if (!allowed_query_types.contains(query_type)) {
    return {std::nullopt, "",
            "unsupported query_type. Use 'leaderboard' or 'time_series'."};
  }
  if (!allowed_metrics.contains(metric)) {
    return {std::nullopt, "",
            "unsupported metric. Use messages, images, reactions_given, or "
            "reactions_received."};
  }
  if (!allowed_time_ranges.contains(time_range)) {
    return {std::nullopt, "", "unsupported time_range."};
  }
  if (!allowed_scopes.contains(scope)) {
    return {std::nullopt, "", "unsupported scope. Use channel or server."};
  }

  std::string emoji;
  if (request.contains("emoji") && !request["emoji"].is_null()) {
    if (!request["emoji"].is_string()) {
      return {std::nullopt, "", "emoji must be a string when provided."};
    }
    emoji = request["emoji"].get<std::string>();
    if (emoji.size() > 32) {
      return {std::nullopt, "", "emoji filter is too long."};
    }
  }

  int limit = 10;
  const int default_limit = query_type == "leaderboard" ? 10 : 30;
  const int max_limit = query_type == "leaderboard" ? 25 : 120;
  if (!parse_limit(request, default_limit, max_limit, limit)) {
    return {std::nullopt, "", "limit must be an integer."};
  }

  std::string interval = "day";
  if (query_type == "time_series") {
    if (request.contains("interval") && request["interval"].is_string()) {
      interval = to_lower_copy(request["interval"].get<std::string>());
    }
    if (interval != "day" && interval != "week" && interval != "month") {
      return {std::nullopt, "", "unsupported interval. Use day, week, or month."};
    }
  }

  const std::string filter_time_expr = "m.created_at";
  const std::string time_filter = time_filter_sql(time_range, filter_time_expr);
  if (time_filter.empty()) {
    return {std::nullopt, "", "invalid time range filter."};
  }

  std::string metric_expr;
  if (metric == "messages") {
    metric_expr = "count(*)";
  } else if (metric == "images") {
    metric_expr = "sum(coalesce(array_length(m.image_descriptions, 1), 0))";
  } else {
    metric_expr = "count(*)";
  }

  std::string sql;
  bool needs_emoji_param = false;
  const std::string scope_filter =
      scope == "server" ? "s.server_snowflake_id = $1" : "c.channel_snowflake_id = $1";
  const std::string scope_join =
      scope == "server" ? " join server s on s.server_id = c.server_id " : "";

  if (query_type == "leaderboard") {
    if (metric == "messages" || metric == "images") {
      sql = std::format(
          "select u.user_name as label, {} as value "
          "from message m "
          "join discord_user u on u.user_id = m.user_id "
          "join channel c on c.channel_id = m.channel_id "
          "{}"
          "where ({}) and ({}) "
          "group by u.user_name "
          "order by value desc, label asc "
          "limit {}",
          metric_expr, scope_join, scope_filter, time_filter, limit);
    } else if (metric == "reactions_given") {
      const std::string emoji_filter = emoji.empty() ? "1 = 1" : "r.reaction = $2";
      needs_emoji_param = !emoji.empty();
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
          scope_join, scope_filter, time_filter, emoji_filter, limit);
    } else {
      const std::string emoji_filter = emoji.empty() ? "1 = 1" : "r.reaction = $2";
      needs_emoji_param = !emoji.empty();
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
          scope_join, scope_filter, time_filter, emoji_filter, limit);
    }
  } else {
    if (metric == "messages" || metric == "images") {
      sql = std::format(
          "select date_trunc('{}', m.created_at) as bucket_start, {} as value "
          "from message m "
          "join channel c on c.channel_id = m.channel_id "
          "{}"
          "where ({}) and ({}) "
          "group by bucket_start "
          "order by bucket_start asc "
          "limit {}",
          interval, metric_expr, scope_join, scope_filter, time_filter, limit);
    } else if (metric == "reactions_given") {
      const std::string emoji_filter = emoji.empty() ? "1 = 1" : "r.reaction = $2";
      needs_emoji_param = !emoji.empty();
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
          interval, scope_join, scope_filter, time_filter, emoji_filter, limit);
    } else {
      const std::string emoji_filter = emoji.empty() ? "1 = 1" : "r.reaction = $2";
      needs_emoji_param = !emoji.empty();
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
          interval, scope_join, scope_filter, time_filter, emoji_filter, limit);
    }
  }

  return {CompiledQuery{.sql = sql,
                        .needs_emoji_param = needs_emoji_param,
                        .limit = limit,
                        .scope = scope,
                        .query_type = query_type,
                        .metric = metric,
                        .time_range = time_range,
                        .interval = interval},
          emoji,
          ""};
}

} // namespace analytics_query
