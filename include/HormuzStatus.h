#ifndef HORMUZSTATUS_H
#define HORMUZSTATUS_H

#include <string>

namespace hormuz_status {

std::string parse_status_html(const std::string &html,
                              const std::string &source_url);

} // namespace hormuz_status

#endif // HORMUZSTATUS_H
