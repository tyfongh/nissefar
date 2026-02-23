#ifndef DOMAIN_H
#define DOMAIN_H

#include <dpp/dpp.h>
#include <cstdint>
#include <string>
#include <vector>

struct Message {
  const dpp::snowflake msg_id;
  const dpp::snowflake msg_replied_to;
  const std::string content;
  const dpp::snowflake author;
  const std::int64_t created_at_unix;
  const std::vector<std::string> image_descriptions;
};

struct Diffdata {
  std::string diffdata;
  std::string weblink;
  std::string header;
  std::string sheet_name;
};

struct SheetTabMetadata {
  std::string sheet_name;
  std::string header;
};

#endif // DOMAIN_H
