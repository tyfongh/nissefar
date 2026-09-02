#ifndef HTMLTEXTEXTRACT_H
#define HTMLTEXTEXTRACT_H

#include <string>

namespace html_text_extract {

enum class ContentFormat { Html, Markdown, PlainText };

std::string extract_text_from_html(const std::string &html);
std::string extract_title_from_html(const std::string &html);
std::string normalize_plain_text(const std::string &text);
ContentFormat classify_content_type(const std::string &content_type);
std::string prepare_webpage_text(const std::string &body, ContentFormat format);

} // namespace html_text_extract

#endif // HTMLTEXTEXTRACT_H
