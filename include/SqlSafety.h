#ifndef SQLSAFETY_H
#define SQLSAFETY_H

#include <string>

namespace sql_safety {

struct ValidationResult {
  std::string rewritten_sql;
  std::string error;

  [[nodiscard]] bool ok() const { return error.empty(); }
};

ValidationResult validate_and_rewrite_channel_query(const std::string &sql);

} // namespace sql_safety

#endif // SQLSAFETY_H
