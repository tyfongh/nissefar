#include <HtmlTextExtract.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

char lower_ascii(char c) {
  const unsigned char uc = static_cast<unsigned char>(c);
  return static_cast<char>(std::tolower(uc));
}

bool equals_icase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }

  for (size_t i = 0; i < a.size(); ++i) {
    if (lower_ascii(a[i]) != lower_ascii(b[i])) {
      return false;
    }
  }

  return true;
}

size_t find_icase(std::string_view haystack, std::string_view needle, size_t pos) {
  if (needle.empty() || pos >= haystack.size()) {
    return std::string::npos;
  }

  for (size_t i = pos; i + needle.size() <= haystack.size(); ++i) {
    bool matches = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (lower_ascii(haystack[i + j]) != lower_ascii(needle[j])) {
        matches = false;
        break;
      }
    }

    if (matches) {
      return i;
    }
  }

  return std::string::npos;
}

bool is_name_char(char c) {
  const unsigned char uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '-' || c == '_' || c == ':';
}

struct TagInfo {
  bool valid = false;
  bool closing = false;
  bool self_closing = false;
  std::string name;
  size_t end_pos = 0;
};

TagInfo parse_tag(std::string_view html, size_t start_pos) {
  TagInfo tag;
  if (start_pos >= html.size() || html[start_pos] != '<') {
    return tag;
  }

  if (start_pos + 3 < html.size() && html[start_pos + 1] == '!' &&
      html[start_pos + 2] == '-' && html[start_pos + 3] == '-') {
    const size_t end_comment = html.find("-->", start_pos + 4);
    if (end_comment == std::string::npos) {
      tag.valid = true;
      tag.end_pos = html.size() - 1;
      return tag;
    }
    tag.valid = true;
    tag.end_pos = end_comment + 2;
    return tag;
  }

  size_t i = start_pos + 1;
  while (i < html.size() && std::isspace(static_cast<unsigned char>(html[i]))) {
    ++i;
  }

  if (i < html.size() && html[i] == '/') {
    tag.closing = true;
    ++i;
  }

  while (i < html.size() && std::isspace(static_cast<unsigned char>(html[i]))) {
    ++i;
  }

  const size_t name_start = i;
  while (i < html.size() && is_name_char(html[i])) {
    ++i;
  }

  if (i > name_start) {
    std::string_view name_sv = html.substr(name_start, i - name_start);
    tag.name.assign(name_sv.begin(), name_sv.end());
    std::transform(tag.name.begin(), tag.name.end(), tag.name.begin(), lower_ascii);
  }

  bool in_quote = false;
  char quote_char = '\0';
  for (; i < html.size(); ++i) {
    const char c = html[i];
    if (in_quote) {
      if (c == quote_char) {
        in_quote = false;
      }
      continue;
    }

    if (c == '\'' || c == '"') {
      in_quote = true;
      quote_char = c;
      continue;
    }

    if (c == '>') {
      size_t j = i;
      while (j > start_pos &&
             std::isspace(static_cast<unsigned char>(html[j - 1]))) {
        --j;
      }
      tag.self_closing = (j > start_pos && html[j - 1] == '/');
      tag.valid = true;
      tag.end_pos = i;
      return tag;
    }
  }

  tag.valid = true;
  tag.end_pos = html.size() - 1;
  return tag;
}

std::string decode_and_collapse_ws(std::string_view text) {
  const std::array<std::pair<std::string_view, std::string_view>, 6> entities = {
      std::pair<std::string_view, std::string_view>{"&amp;", "&"},
      std::pair<std::string_view, std::string_view>{"&lt;", "<"},
      std::pair<std::string_view, std::string_view>{"&gt;", ">"},
      std::pair<std::string_view, std::string_view>{"&quot;", "\""},
      std::pair<std::string_view, std::string_view>{"&#39;", "'"},
      std::pair<std::string_view, std::string_view>{"&nbsp;", " "}};

  std::string out;
  out.reserve(text.size());

  bool previous_space = true;
  size_t i = 0;
  while (i < text.size()) {
    bool decoded = false;
    if (text[i] == '&') {
      for (const auto &[entity, value] : entities) {
        if (i + entity.size() <= text.size() &&
            text.substr(i, entity.size()) == entity) {
          for (const char decoded_char : value) {
            if (std::isspace(static_cast<unsigned char>(decoded_char))) {
              if (!previous_space) {
                out.push_back(' ');
                previous_space = true;
              }
            } else {
              out.push_back(decoded_char);
              previous_space = false;
            }
          }
          i += entity.size();
          decoded = true;
          break;
        }
      }
    }

    if (decoded) {
      continue;
    }

    const unsigned char uc = static_cast<unsigned char>(text[i]);
    if (std::isspace(uc)) {
      if (!previous_space) {
        out.push_back(' ');
        previous_space = true;
      }
    } else {
      out.push_back(static_cast<char>(uc));
      previous_space = false;
    }
    ++i;
  }

  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }

  return out;
}

std::string strip_tags_only(std::string_view text) {
  std::string out;
  out.reserve(text.size());

  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '<') {
      const TagInfo tag = parse_tag(text, i);
      if (tag.valid) {
        i = tag.end_pos + 1;
        continue;
      }
    }
    out.push_back(text[i]);
    ++i;
  }

  return out;
}

} // namespace

namespace html_text_extract {

std::string extract_text_from_html(const std::string &html) {
  static const std::unordered_set<std::string> skipped_blocks = {
      "nav", "footer", "header", "form", "svg"};
  static const std::unordered_set<std::string> raw_skipped_blocks = {"script",
                                                                      "style"};

  std::vector<std::string> skipped_stack;
  std::string out;
  out.reserve(html.size());

  size_t i = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      const TagInfo tag = parse_tag(html, i);
      if (tag.valid) {
        if (!tag.name.empty()) {
          if (!tag.closing && !tag.self_closing &&
              raw_skipped_blocks.contains(tag.name)) {
            const std::string end_token = "</" + tag.name;
            const size_t close_tag = find_icase(html, end_token, tag.end_pos + 1);
            if (close_tag == std::string::npos) {
              break;
            }

            const size_t close_gt = html.find('>', close_tag + end_token.size());
            if (close_gt == std::string::npos) {
              break;
            }

            i = close_gt + 1;
            continue;
          }

          if (!tag.closing && !tag.self_closing &&
              skipped_blocks.contains(tag.name)) {
            skipped_stack.push_back(tag.name);
          } else if (tag.closing && !skipped_stack.empty() &&
                     equals_icase(skipped_stack.back(), tag.name)) {
            skipped_stack.pop_back();
          }
        }

        i = tag.end_pos + 1;
        continue;
      }
    }

    if (skipped_stack.empty()) {
      out.push_back(html[i]);
    }
    ++i;
  }

  return decode_and_collapse_ws(out);
}

std::string extract_title_from_html(const std::string &html) {
  size_t i = 0;
  while (i < html.size()) {
    if (html[i] != '<') {
      ++i;
      continue;
    }

    const TagInfo tag = parse_tag(html, i);
    if (!tag.valid) {
      ++i;
      continue;
    }

    if (!tag.closing && !tag.self_closing && tag.name == "title") {
      const size_t close_pos = find_icase(html, "</title", tag.end_pos + 1);
      if (close_pos == std::string::npos || close_pos <= tag.end_pos + 1) {
        return "";
      }

      const std::string_view inner = std::string_view(html).substr(
          tag.end_pos + 1, close_pos - (tag.end_pos + 1));
      return decode_and_collapse_ws(strip_tags_only(inner));
    }

    i = tag.end_pos + 1;
  }

  return "";
}

std::string normalize_plain_text(const std::string &text) {
  return decode_and_collapse_ws(text);
}

} // namespace html_text_extract
