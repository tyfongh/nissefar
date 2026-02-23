#ifndef ANALYTICSQUERY_H
#define ANALYTICSQUERY_H

#include <optional>
#include <string>

namespace analytics_query {

struct CompiledQuery {
  std::string sql;
  bool needs_emoji_param{false};
  int limit{10};
  std::string scope;
  std::string query_type;
  std::string metric;
  std::string time_range;
  std::string interval;
};

struct ParseResult {
  std::optional<CompiledQuery> query;
  std::string emoji;
  std::string error;

  [[nodiscard]] bool ok() const { return query.has_value(); }
};

ParseResult parse_and_compile(const std::string &request_json);

} // namespace analytics_query

#endif // ANALYTICSQUERY_H
