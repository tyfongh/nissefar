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

void test_reaction_emoji_leaderboard() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"scope":"server","kind":"leaderboard","target":"reactions","group_by":"emoji","time_range":"all_time","limit":10})");
  expect_true(parsed.ok(), "reaction emoji leaderboard parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->sql.find("select r.reaction as label") != std::string::npos,
              "emoji leaderboard selects reaction label");
  expect_true(parsed.query->sql.find("group by r.reaction") != std::string::npos,
              "emoji leaderboard groups by reaction");
}

void test_most_clown_posts_compiles_to_message_leaderboard() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"scope":"server","kind":"leaderboard","target":"messages","group_by":"message","filters":{"emojis":["🤡"]},"time_range":"all_time","limit":10})");
  expect_true(parsed.ok(), "message leaderboard with emoji filter parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->sql.find("message_snowflake_id::text as message_id") !=
                  std::string::npos,
              "message leaderboard returns message identifier");
  expect_true(parsed.query->sql.find("count(r.reaction) as value") !=
                  std::string::npos,
              "message leaderboard counts reactions");
  expect_true(parsed.query->bind_params.size() == 1,
              "unicode emoji filter adds one bind parameter");
}

void test_multi_emoji_filter_generates_multiple_bindings() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"scope":"server","kind":"leaderboard","target":"reactions","group_by":"emoji","filters":{"emojis":["🤡",":copium:",":1Head:",":3Head:"]},"time_range":"all_time","limit":20})");
  expect_true(parsed.ok(), "multi emoji leaderboard parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->bind_params.size() == 4,
              "multi emoji filter generates four bind params");
  expect_true(parsed.query->sql.find("r.reaction = $2") != std::string::npos,
              "first emoji uses exact match placeholder");
  expect_true(parsed.query->sql.find("r.reaction ~ $3") != std::string::npos,
              "custom emoji name uses regex placeholder");
}

void test_message_author_leaderboard() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"scope":"channel","kind":"leaderboard","target":"messages","group_by":"author","time_range":"last_month","limit":7})");
  expect_true(parsed.ok(), "message author leaderboard parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->sql.find("group by u.user_name") != std::string::npos,
              "message author leaderboard groups by user");
  expect_true(parsed.query->scope == "channel", "scope preserved");
}

void test_reaction_time_series() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"scope":"server","kind":"time_series","target":"reactions","group_by":"week","time_range":"last_30d","filters":{"emojis":[":copium:"]}})");
  expect_true(parsed.ok(), "reaction time series parses");
  if (!parsed.ok()) {
    return;
  }

  expect_true(parsed.query->sql.find("date_trunc('week', m.created_at)") !=
                  std::string::npos,
              "time series uses requested week bucket");
}

void test_invalid_combination_rejected() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"kind":"leaderboard","target":"messages","group_by":"emoji"})");
  expect_false(parsed.ok(), "invalid target/group_by combination rejected");
}

void test_invalid_limit_type_rejected() {
  const auto parsed = analytics_query::parse_and_compile(
      R"({"kind":"leaderboard","target":"reactions","group_by":"emoji","limit":"ten"})");
  expect_false(parsed.ok(), "non-integer limit rejected");
}

} // namespace

int main() {
  test_reaction_emoji_leaderboard();
  test_most_clown_posts_compiles_to_message_leaderboard();
  test_multi_emoji_filter_generates_multiple_bindings();
  test_message_author_leaderboard();
  test_reaction_time_series();
  test_invalid_combination_rejected();
  test_invalid_limit_type_rejected();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All AnalyticsQuery tests passed\n";
  return 0;
}
