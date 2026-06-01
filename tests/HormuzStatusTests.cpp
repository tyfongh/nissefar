#include <HormuzStatus.h>
#include <Json.h>

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

void expect_eq(const std::string &actual, const std::string &expected,
               const std::string &message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << "\n"
              << "  expected: '" << expected << "'\n"
              << "  actual:   '" << actual << "'\n";
    ++failures;
  }
}

void test_extracts_closed_status() {
  const std::string html = R"(
    <script>
      const STRAIT_STATUS = "closed";
      const STATUS_ANSWER = "NO";
      const STATUS_DETAIL = "Limited selective transit continues.";
      const STATUS_LABEL  = "EFFECTIVELY CLOSED";
    </script>
  )";

  const Json payload = Json::parse(
      hormuz_status::parse_status_html(html, "https://hormuzstatus.com/"));
  expect_eq(payload["source"].get<std::string>(), "https://hormuzstatus.com/",
            "sets source");
  expect_eq(payload["status"].get<std::string>(), "closed", "sets status");
  expect_eq(payload["answer"].get<std::string>(), "NO", "sets answer");
  expect_true(payload["is_open"].get<bool>() == false, "closed is not open");
  expect_eq(payload["label"].get<std::string>(), "EFFECTIVELY CLOSED",
            "sets label");
  expect_eq(payload["detail"].get<std::string>(),
            "Limited selective transit continues.", "sets detail");
  expect_true(payload.contains("fetched_at"), "sets fetched timestamp");
}

void test_extracts_open_status() {
  const std::string html = R"(
    <script>
      const STRAIT_STATUS = "open";
      const STATUS_ANSWER = "YES";
    </script>
  )";

  const Json payload = Json::parse(
      hormuz_status::parse_status_html(html, "https://hormuzstatus.com/"));
  expect_true(payload["is_open"].get<bool>() == true, "open is open");
}

void test_reports_missing_constants() {
  const std::string result =
      hormuz_status::parse_status_html("<html></html>", "https://hormuzstatus.com/");
  expect_true(result.starts_with("Tool error:"), "missing constants returns tool error");
}

} // namespace

int main() {
  test_extracts_closed_status();
  test_extracts_open_status();
  test_reports_missing_constants();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All HormuzStatus tests passed\n";
  return 0;
}
