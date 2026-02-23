#include <AnalyticsQuery.h>

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++failures;
  }
}

void expect_false(bool condition, const std::string &message) {
  expect_true(!condition, message);
}

void test_leaderboard_messages_query() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"query_type":"leaderboard","metric":"messages","time_range":"last_month","limit":7})");
  expect_true(parsed.ok(), "leaderboard messages query parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->sql.find("group by u.user_name") != std::string::npos,
              "leaderboard groups by user");
  expect_true(parsed.query->scope == "channel", "default scope is channel");
  expect_true(parsed.query->sql.find("date_trunc('month', now()) - interval '1 month'") !=
                  std::string::npos,
              "last month time filter included");
  expect_true(parsed.query->limit == 7, "explicit limit is used");
}

void test_timeseries_default_interval() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"query_type":"time_series","metric":"messages","time_range":"last_30d"})");
  expect_true(parsed.ok(), "time series query parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->sql.find("date_trunc('day'", 0) != std::string::npos,
              "default interval is day");
}

void test_reactions_with_emoji_filter() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"query_type":"leaderboard","metric":"reactions_received","emoji":"🤡","limit":10})");
  expect_true(parsed.ok(), "reaction query parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->needs_emoji_param, "emoji param required when supplied");
  expect_true(parsed.query->sql.find("r.reaction = $2") != std::string::npos,
              "emoji placeholder included in SQL");
}

void test_invalid_metric_rejected() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"query_type":"leaderboard","metric":"toxicity_score"})");
  expect_false(parsed.ok(), "invalid metric is rejected");
}

void test_invalid_limit_type_rejected() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"query_type":"leaderboard","metric":"messages","limit":"ten"})");
  expect_false(parsed.ok(), "non-integer limit is rejected");
}

void test_limit_clamped() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"query_type":"leaderboard","metric":"messages","limit":999})");
  expect_true(parsed.ok(), "limit-clamped query still parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->limit == 25, "leaderboard limit is clamped to 25");
}

void test_server_scope_compiles_to_server_filter() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"scope":"server","query_type":"leaderboard","metric":"reactions_received","emoji":"🤡"})");
  expect_true(parsed.ok(), "server scope query parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->scope == "server", "scope is server");
  expect_true(parsed.query->sql.find("join server s on s.server_id = c.server_id") !=
                  std::string::npos,
              "server join added");
  expect_true(parsed.query->sql.find("s.server_snowflake_id = $1") != std::string::npos,
              "server scope filter uses parameter");
}

void test_invalid_scope_rejected() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"scope":"global","query_type":"leaderboard","metric":"messages"})");
  expect_false(parsed.ok(), "invalid scope is rejected");
}

} // namespace

int main() {
  test_leaderboard_messages_query();
  test_timeseries_default_interval();
  test_reactions_with_emoji_filter();
  test_invalid_metric_rejected();
  test_invalid_limit_type_rejected();
  test_limit_clamped();
  test_server_scope_compiles_to_server_filter();
  test_invalid_scope_rejected();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All AnalyticsQuery tests passed\n";
  return 0;
}
