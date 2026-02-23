#ifndef FORMATTING_H
#define FORMATTING_H

#include <string>

namespace pqxx {
class result;
}

size_t utf8_len(const std::string &text);
size_t utf8_display_width(const std::string &text);
std::string utf8_truncate_to_width(const std::string &text, size_t max_width);
void pad_right(std::string &text, size_t num, const std::string &pad_char);
void pad_left(std::string &text, size_t num, const std::string &pad_char);
std::string format_chanstat_table(const pqxx::result &res, std::string channel);

#endif // FORMATTING_H
