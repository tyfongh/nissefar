#ifndef FORMATTING_H
#define FORMATTING_H

#include <pqxx/pqxx>
#include <string>

size_t utf8_len(const std::string &text);
void pad_right(std::string &text, size_t num, const std::string &pad_char);
void pad_left(std::string &text, size_t num, const std::string &pad_char);
std::string format_chanstat_table(const pqxx::result &res, std::string channel);

#endif // FORMATTING_H
