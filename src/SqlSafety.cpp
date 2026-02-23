#include <SqlSafety.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

namespace {

std::string trim_copy(const std::string &value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });

  if (first == value.end()) {
    return "";
  }

  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();

  return std::string(first, last);
}

std::string lower_ascii_copy(const std::string &value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return out;
}

bool contains_forbidden_keyword(const std::string &lower_sql) {
  static const std::regex forbidden_keywords(
      R"(\b(insert|update|delete|drop|alter|truncate|create|grant|revoke|comment|copy|do|call|execute|vacuum|analyze|reindex|refresh|listen|notify|set|show|begin|commit|rollback)\b)");
  return std::regex_search(lower_sql, forbidden_keywords);
}

} // namespace

namespace sql_safety {

ValidationResult validate_and_rewrite_channel_query(const std::string &sql) {
  const std::string trimmed = trim_copy(sql);
  if (trimmed.empty()) {
    return {"", "query is empty."};
  }

  if (trimmed.size() > 1500) {
    return {"", "query is too long (max 1500 chars)."};
  }

  const std::string lower_sql = lower_ascii_copy(trimmed);

  if (!lower_sql.starts_with("select")) {
    return {"", "only SELECT queries are allowed."};
  }

  if (lower_sql.find(';') != std::string::npos) {
    return {"", "semicolon is not allowed."};
  }

  if (lower_sql.find("--") != std::string::npos ||
      lower_sql.find("/*") != std::string::npos ||
      lower_sql.find("*/") != std::string::npos) {
    return {"", "SQL comments are not allowed."};
  }

  if (lower_sql.find(" from (") != std::string::npos ||
      lower_sql.find(" join (") != std::string::npos) {
    return {"", "subqueries in FROM/JOIN are not allowed."};
  }

  if (lower_sql.find("pg_catalog") != std::string::npos ||
      lower_sql.find("information_schema") != std::string::npos ||
      lower_sql.find("pg_") != std::string::npos) {
    return {"", "system catalogs are not allowed."};
  }

  if (contains_forbidden_keyword(lower_sql)) {
    return {"", "query contains forbidden SQL keyword."};
  }

  static const std::regex table_ref_regex(
      R"(\b(?:from|join)\s+([a-z_][a-z0-9_]*)\b)");
  static const std::set<std::string> allowed_tables = {
      "message", "reaction", "discord_user", "channel"};

  std::set<std::string> referenced_tables;
  for (std::sregex_iterator it(lower_sql.begin(), lower_sql.end(), table_ref_regex),
       end;
       it != end; ++it) {
    referenced_tables.insert((*it)[1].str());
  }

  if (referenced_tables.empty()) {
    return {"", "query must reference at least one allowed table."};
  }

  for (const auto &table : referenced_tables) {
    if (!allowed_tables.contains(table)) {
      return {"", "query references a table outside the allowlist."};
    }
  }

  static const std::regex channel_placeholder_regex(R"(\{\{\s*channel_id\s*\}\})",
                                                    std::regex_constants::icase);
  if (!std::regex_search(trimmed, channel_placeholder_regex)) {
    return {"", "query must include {{CHANNEL_ID}} placeholder for channel scope."};
  }

  std::string rewritten =
      std::regex_replace(trimmed, channel_placeholder_regex, "$$1");

  return {rewritten, ""};
}

} // namespace sql_safety
