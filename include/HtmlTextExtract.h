#ifndef HTMLTEXTEXTRACT_H
#define HTMLTEXTEXTRACT_H

#include <string>

namespace html_text_extract {

std::string extract_text_from_html(const std::string &html);
std::string extract_title_from_html(const std::string &html);
std::string normalize_plain_text(const std::string &text);

} // namespace html_text_extract

#endif // HTMLTEXTEXTRACT_H
