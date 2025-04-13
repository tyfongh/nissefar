#ifndef MESSAGE_H
#define MESSAGE_H

#include <dpp/snowflake.h>
class Message
{
  const std::string message;
  const dpp::snowflake author_id;
};

#endif // MESSAGE_H
