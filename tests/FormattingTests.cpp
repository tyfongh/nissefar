#include <Formatting.h>

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

void expect_equal(size_t actual, size_t expected, const std::string &message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << " actual=" << actual
              << " expected=" << expected << "\n";
    ++failures;
  }
}

void expect_equal(const std::string &actual, const std::string &expected,
                  const std::string &message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << " actual='" << actual
              << "' expected='" << expected << "'\n";
    ++failures;
  }
}

void test_display_width_ascii() {
  expect_equal(utf8_display_width("hello"), 5, "ascii width");
}

void test_display_width_wide_and_emoji() {
  expect_equal(utf8_display_width("\xE4\xBD\xA0\xE5\xA5\xBD"), 4,
               "CJK width is 2 per char");
  expect_equal(utf8_display_width("\xF0\x9F\x98\x80"), 2,
               "emoji display width");
}

void test_display_width_combining() {
  expect_equal(utf8_display_width("e\xCC\x81"), 1,
               "combining acute does not add width");
}

void test_truncate_by_display_width() {
  const std::string source = std::string("ab") + "\xE4\xBD\xA0" +
                             "\xE5\xA5\xBD" + "cd";
  expect_equal(utf8_truncate_to_width(source, 4),
               std::string("ab") + "\xE4\xBD\xA0",
               "truncate respects wide characters");
  expect_equal(utf8_truncate_to_width(std::string("\xF0\x9F\x98\x80") + "a", 2),
               "\xF0\x9F\x98\x80", "truncate keeps complete emoji");
}

void test_padding_saturates() {
  std::string text = "123456789";
  pad_left(text, 3, " ");
  expect_equal(text, "123456789", "left padding does not underflow");

  std::string text2 = "\xF0\x9F\x98\x80";
  pad_right(text2, 4, " ");
  expect_equal(utf8_display_width(text2), 4, "right padding uses display width");
}

} // namespace

int main() {
  test_display_width_ascii();
  test_display_width_wide_and_emoji();
  test_display_width_combining();
  test_truncate_by_display_width();
  test_padding_saturates();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All Formatting tests passed\n";
  return 0;
}
