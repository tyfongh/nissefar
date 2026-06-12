#include <Sentiment.h>

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

void test_parses_valid_sentiment() {
  const auto parsed = sentiment::parse_evaluation_json(
      R"({"label":"negative","score":-0.75,"tone":["angry","sarcastic"],"confidence":0.82})");
  expect_true(parsed.has_value(), "valid sentiment parses");
  if (!parsed.has_value()) {
    return;
  }

  expect_eq(parsed->label, "negative", "sets label");
  expect_true(parsed->score == -0.75, "sets score");
  expect_true(parsed->confidence == 0.82, "sets confidence");
  expect_true(parsed->tone.size() == 2, "sets tone tags");
}

void test_rejects_invalid_label() {
  const auto parsed = sentiment::parse_evaluation_json(
      R"({"label":"bad","score":0,"tone":[],"confidence":1})");
  expect_true(!parsed.has_value(), "invalid label is rejected");
}

void test_formats_history_compactly() {
  const std::string payload =
      R"({"label":"positive","score":0.5,"tone":["joking"],"confidence":0.9})";
  expect_eq(sentiment::format_for_history(payload),
            "Sentiment: positive, score=0.50, confidence=0.90, tone=joking",
            "formats history line");
}

} // namespace

int main() {
  test_parses_valid_sentiment();
  test_rejects_invalid_label();
  test_formats_history_compactly();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All Sentiment tests passed\n";
  return 0;
}
