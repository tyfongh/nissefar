#include <Formatting.h>

#include <format>

size_t utf8_len(const std::string &text) {
  size_t len = 0;
  const char *s = text.c_str();
  while (*s)
    len += (*s++ & 0xc0) != 0x80;
  return len;
}

void pad_right(std::string &text, size_t num, const std::string &pad_char) {
  auto len = num - utf8_len(text);
  for (size_t i = 0; i < len; i++) {
    text.append(pad_char);
  }
}

void pad_left(std::string &text, size_t num, const std::string &pad_char) {
  auto len = num - utf8_len(text);

  for (size_t i = 0; i < len; i++) {
    text.insert(0, pad_char);
  }
}

std::string format_chanstat_table(const pqxx::result &res, std::string channel) {
  channel.insert(0, "#");

  if (utf8_len(channel) > 20)
    channel.resize(19);
  pad_right(channel, 20, "═");

  std::string chanstats_table =
      std::format("```╔═{}╦══msgs══╦══imgs══╗\n", channel);

  bool first = true;

  for (auto row : res) {
    if (!first)
      chanstats_table.append("╠═════════════════════╬════════╬════════╣\n");
    std::string username = row["user_name"].as<std::string>();
    std::string msgs = row["nmsgs"].as<std::string>();
    std::string imgs = row["nimages"].as<std::string>();

    if (utf8_len(username) > 19)
      username.resize(19);
    pad_right(username, 20, " ");
    pad_left(msgs, 7, " ");
    pad_left(imgs, 7, " ");
    chanstats_table.append(
        std::format("║ {}║{} ║{} ║\n", username, msgs, imgs));

    first = false;
  }

  chanstats_table.append("╚═════════════════════╩════════╩════════╝```");

  return chanstats_table;
}
