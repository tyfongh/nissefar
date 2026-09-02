#include <HtmlTextExtract.h>

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

void test_extracts_text_and_skips_heavy_blocks() {
  const std::string html =
      "<html><head><title>Demo</title><style>.x{display:none}</style></head>"
      "<body><header>Top menu</header><p>Hello <b>world</b> &amp; friends.</p>"
      "<script>var x = '<p>hidden</p>';</script><footer>Bottom links</footer>"
      "</body></html>";

  const std::string text = html_text_extract::extract_text_from_html(html);
  expect_true(text.find("Hello world & friends.") != std::string::npos,
              "keeps readable text");
  expect_true(text.find("Top menu") == std::string::npos,
              "drops header block text");
  expect_true(text.find("hidden") == std::string::npos,
              "drops script block text");
}

void test_extracts_title_case_insensitive() {
  const std::string html =
      "<html><head><TiTlE> Volvo &amp; Safety </TiTlE></head><body>x</body></html>";
  expect_eq(html_text_extract::extract_title_from_html(html), "Volvo & Safety",
            "extracts and decodes title");
}

void test_normalize_plain_text() {
  const std::string input = "  one\n\t two  &amp;   three&nbsp; ";
  expect_eq(html_text_extract::normalize_plain_text(input), "one two & three",
            "collapses whitespace and decodes entities");
}

void test_classifies_web_content_types() {
  using html_text_extract::ContentFormat;
  expect_true(html_text_extract::classify_content_type(
                  "text/markdown; charset=utf-8") == ContentFormat::Markdown,
              "recognizes Markdown with parameters");
  expect_true(html_text_extract::classify_content_type(" TEXT/X-MARKDOWN ") ==
                  ContentFormat::Markdown,
              "recognizes legacy Markdown case-insensitively");
  expect_true(html_text_extract::classify_content_type("text/html") ==
                  ContentFormat::Html,
              "recognizes HTML");
  expect_true(html_text_extract::classify_content_type("") ==
                  ContentFormat::Html,
              "treats a missing content type as HTML");
  expect_true(html_text_extract::classify_content_type("text/plain") ==
                  ContentFormat::PlainText,
              "recognizes plain text fallback");
}

void test_prepares_negotiated_web_content() {
  using html_text_extract::ContentFormat;
  const std::string markdown =
      "# Heading\n\n- one\n- two\n\n[Link](https://example.com)\n";
  expect_eq(html_text_extract::prepare_webpage_text(
                markdown, ContentFormat::Markdown),
            markdown, "preserves Markdown structure");
  expect_eq(html_text_extract::prepare_webpage_text(
                "<main><h1>Heading</h1><p>Body</p></main>",
                ContentFormat::Html),
            "HeadingBody", "extracts HTML text");
  expect_eq(html_text_extract::prepare_webpage_text(
                " one\n  two ", ContentFormat::PlainText),
            "one two", "normalizes plain text");
}

void test_handles_large_malformed_html_without_regex() {
  std::string html = "<html><body><script>";
  for (int i = 0; i < 200000; ++i) {
    html += "<div";
  }
  html += "still alive";

  const std::string text = html_text_extract::extract_text_from_html(html);
  expect_true(text.empty(),
              "malformed unterminated raw block yields safe empty output");
}

} // namespace

int main() {
  test_extracts_text_and_skips_heavy_blocks();
  test_extracts_title_case_insensitive();
  test_normalize_plain_text();
  test_classifies_web_content_types();
  test_prepares_negotiated_web_content();
  test_handles_large_malformed_html_without_regex();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All HtmlTextExtract tests passed\n";
  return 0;
}
