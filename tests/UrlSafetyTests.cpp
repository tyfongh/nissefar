#include <UrlSafety.h>

#include <iostream>
#include <optional>
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

void expect_has_error(const std::string &url, const std::string &message) {
  const auto error = url_safety::validate_public_http_url(url, nullptr);
  expect_true(error.has_value(), message + " url='" + url + "'");
}

void expect_no_error(const std::string &url, const std::string &message) {
  const auto error = url_safety::validate_public_http_url(url, nullptr);
  expect_false(error.has_value(), message + " url='" + url + "'");
}

void test_parse_http_url() {
  const auto parsed = url_safety::parse_http_url("https://example.com/some/path?q=1");
  expect_true(parsed.has_value(), "parse valid https URL");
  if (!parsed.has_value())
    return;

  expect_true(parsed->scheme == "https", "parsed scheme is https");
  expect_true(parsed->host == "example.com", "parsed host is example.com");
  expect_true(parsed->port == "443", "default https port is 443");
  expect_true(parsed->path == "/some/path?q=1", "parsed path preserved");
}

void test_parse_bracketed_ipv6_url() {
  const auto parsed = url_safety::parse_http_url("http://[2001:db8::1]:80/video");
  expect_true(parsed.has_value(), "parse bracketed ipv6 URL");
  if (!parsed.has_value())
    return;

  expect_true(parsed->scheme == "http", "parsed scheme is http");
  expect_true(parsed->host == "2001:db8::1", "parsed host strips ipv6 brackets");
  expect_true(parsed->port == "80", "parsed explicit port is 80");
}

void test_port_filtering() {
  expect_has_error("https://1.1.1.1:8080", "block non-allowed port 8080");
  expect_no_error("https://1.1.1.1:443", "allow port 443");
}

void test_blocked_local_hostnames() {
  expect_has_error("https://localhost", "block localhost");
  expect_has_error("https://LOCALHOST", "block uppercase localhost");
  expect_has_error("https://printer.local", "block .local hostname");
}

void test_blocked_private_ipv4_literals() {
  expect_has_error("https://127.0.0.1", "block loopback ipv4 literal");
  expect_has_error("https://10.1.2.3", "block private ipv4 10/8");
  expect_has_error("https://192.168.1.2", "block private ipv4 192.168/16");
  expect_has_error("https://169.254.10.20", "block link-local ipv4");
}

void test_blocked_private_ipv6_literals() {
  expect_has_error("https://[::1]", "block ipv6 loopback literal");
  expect_has_error("https://[fe80::1]", "block ipv6 link-local literal");
  expect_has_error("https://[fc00::1]", "block ipv6 ULA literal");
  expect_has_error("https://[ff02::1]", "block ipv6 multicast literal");
  expect_has_error("https://[::]", "block ipv6 unspecified literal");
}

void test_blocked_ipv4_mapped_ipv6_literals() {
  expect_has_error("https://[::ffff:127.0.0.1]",
                   "block ipv4-mapped loopback ipv6");
  expect_has_error("https://[::ffff:10.0.0.5]",
                   "block ipv4-mapped private ipv6");
}

void test_weird_utf8_and_confusable_hosts() {
  expect_has_error("https://localho\xD1\x95t",
                   "block confusable Cyrillic hostname");
  expect_has_error("https://\xEF\xBD\x85xample.com",
                   "block fullwidth hostname");
  expect_has_error("https://\xE2\x80\x8Blocalhost",
                   "block zero-width prefixed hostname");
  expect_has_error("https://localhost%2e", "block encoded-dot localhost host");
  expect_has_error("https://%6cocalhost", "block percent-encoded hostname");
}

void test_malformed_urls() {
  expect_has_error("example.com", "reject missing scheme");
  expect_has_error("ftp://example.com", "reject non-http scheme");
  expect_has_error("https://[::1", "reject malformed bracketed ipv6 host");
}

} // namespace

int main() {
  test_parse_http_url();
  test_parse_bracketed_ipv6_url();
  test_port_filtering();
  test_blocked_local_hostnames();
  test_blocked_private_ipv4_literals();
  test_blocked_private_ipv6_literals();
  test_blocked_ipv4_mapped_ipv6_literals();
  test_weird_utf8_and_confusable_hosts();
  test_malformed_urls();

if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All UrlSafety tests passed\n";
  return 0;
}
