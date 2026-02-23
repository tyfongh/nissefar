#include <Formatting.h>

#include <format>
#include <pqxx/pqxx>

std::string format_chanstat_table(const pqxx::result &res, std::string channel) {
  channel.insert(0, "#");

  if (utf8_display_width(channel) > 20)
    channel = utf8_truncate_to_width(channel, 20);
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

    if (utf8_display_width(username) > 20)
      username = utf8_truncate_to_width(username, 20);
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
