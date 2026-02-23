#include <SqlSafety.h>

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

void test_accepts_valid_channel_scoped_query() {
  const auto result = sql_safety::validate_and_rewrite_channel_query(
      "SELECT u.user_name, count(*) AS n "
      "FROM message m "
      "JOIN discord_user u ON u.user_id = m.user_id "
      "JOIN channel c ON c.channel_id = m.channel_id "
      "WHERE c.channel_snowflake_id = {{CHANNEL_ID}} "
      "GROUP BY u.user_name ORDER BY n DESC LIMIT 10");

  expect_true(result.ok(), "valid analytics query is accepted");
  expect_false(result.rewritten_sql.empty(), "rewritten query is returned");
  expect_true(result.rewritten_sql.find("$1") != std::string::npos,
              "channel placeholder replaced with $1");
}

void test_rejects_missing_channel_placeholder() {
  const auto result = sql_safety::validate_and_rewrite_channel_query(
      "SELECT count(*) FROM message");

  expect_false(result.ok(), "query without channel placeholder is rejected");
}

void test_rejects_non_select() {
  const auto result = sql_safety::validate_and_rewrite_channel_query(
      "DELETE FROM message WHERE message_id = 1");

  expect_false(result.ok(), "non-select query is rejected");
}

void test_rejects_comments_and_semicolon() {
  const auto semicolon_result = sql_safety::validate_and_rewrite_channel_query(
      "SELECT 1 WHERE 1 = {{CHANNEL_ID}};");
  expect_false(semicolon_result.ok(), "semicolon is rejected");

  const auto comment_result = sql_safety::validate_and_rewrite_channel_query(
      "SELECT 1 WHERE 1 = {{CHANNEL_ID}} -- comment");
  expect_false(comment_result.ok(), "comment is rejected");
}

void test_rejects_unallowlisted_table() {
  const auto result = sql_safety::validate_and_rewrite_channel_query(
      "SELECT * FROM server WHERE server_id = {{CHANNEL_ID}}");

  expect_false(result.ok(), "unallowlisted table is rejected");
}

void test_rejects_subquery_in_from() {
  const auto result = sql_safety::validate_and_rewrite_channel_query(
      "SELECT * FROM (SELECT * FROM message) m WHERE 1 = {{CHANNEL_ID}}");

  expect_false(result.ok(), "subquery in FROM is rejected");
}

} // namespace

int main() {
  test_accepts_valid_channel_scoped_query();
  test_rejects_missing_channel_placeholder();
  test_rejects_non_select();
  test_rejects_comments_and_semicolon();
  test_rejects_unallowlisted_table();
  test_rejects_subquery_in_from();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All SqlSafety tests passed\n";
  return 0;
}
