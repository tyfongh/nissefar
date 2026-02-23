#ifndef ANALYTICSQUERY_H
#define ANALYTICSQUERY_H

#include <optional>
#include <vector>
#include <string>

namespace analytics_query {

struct CompiledQuery {
  std::string sql;
  std::vector<std::string> bind_params;
  int limit{10};
  std::string scope;
  std::string kind;
  std::string target;
  std::string group_by;
  std::string time_range;
};

struct ParseResult {
  std::optional<CompiledQuery> query;
  std::string error;

  [[nodiscard]] bool ok() const { return query.has_value(); }
};

ParseResult parse_and_compile(const std::string &request_json);

} // namespace analytics_query

#endif // ANALYTICSQUERY_H
