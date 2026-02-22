#include <Database.h>
#include <Nissefar.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

Nissefar::Nissefar() {
  if (!config.is_valid)
    throw std::runtime_error("Configuration is invalid");

  bot = std::make_unique<dpp::cluster>(
      config.discord_token, dpp::i_default_intents | dpp::i_message_content);

  bot->on_log(dpp::utility::cout_logger());

  // Allow for some minutes of LLM generation

  ollama::setReadTimeout(360);
  ollama::setWriteTimeout(360);

  bot->log(dpp::ll_info, "Bot initialized");
}

// Pull channel message history from the SQL database instead of deque
// in memory database. Will persist through reboots.

std::string Nissefar::format_message_history(dpp::snowflake channel_id) {
  std::string message_history{};

  auto &db = Database::instance();
  auto res = db.execute("select m.message_id "
                        "     , m.message_snowflake_id "
                        "     , m.reply_to_snowflake_id "
                        "     , u.user_snowflake_id "
                        "     , m.content "
                        "     , m.image_descriptions "
                        "from message m "
                        "inner join discord_user u on (u.user_id = m.user_id) "
                        "inner join channel c on (c.channel_id = m.channel_id) "
                        "where c.channel_snowflake_id = $1 "
                        "order by m.message_id desc limit $2",
                        std::stol(channel_id.str()), config.max_history);
  if (!res.empty()) {
    message_history = "Channel message history:";

    // Need to reverse here due to the nature of the best, the SQL server picks
    // the top 20 but to do so it orders the messages from last to first.

    for (auto message : res | std::views::reverse) {
      message_history +=
          std::format("\n----------------------\n"
                      "Message id: {}\n"
                      "Reply to message id: {}\n"
                      "Author: {}\n"
                      "Message content: {}",
                      message["message_snowflake_id"].as<std::string>(),
                      message["reply_to_snowflake_id"].as<std::string>(),
                      message["user_snowflake_id"].as<std::string>(),
                      message["content"].as<std::string>());

      auto react_res =
          db.execute("select u.user_snowflake_id "
                     "     , r.reaction "
                     "from reaction r "
                     "inner join discord_user u on (u.user_id = r.user_id) "
                     "where r.message_id = $1",
                     message["message_id"].as<std::uint64_t>());

      if (!react_res.empty()) {
        for (auto reaction : react_res) {
          message_history +=
              std::format("\nReaction by {}: {}",
                          reaction["user_snowflake_id"].as<std::string>(),
                          reaction["reaction"].as<std::string>());
        }
      }

      pqxx::array<std::string> image_descriptions =
          message["image_descriptions"].as_sql_array<std::string>();

      for (int i = 0; i < image_descriptions.size(); ++i) {
        message_history +=
            std::format("\nImage {}, {}", i, image_descriptions[i]);
      }
    }
    message_history += "\n----------------------\n";
  }
  return message_history;
}

std::string Nissefar::format_sheet_context() {
  std::string context{};

  if (sheet_data.empty()) {
    return context;
  }

  context = "Google Sheets context:";

  for (const auto &[filename, tabs] : sheet_data) {
    if (filename == "Charging curves") {
      continue;
    }

    if (tabs.empty()) {
      continue;
    }

    for (const auto &[sheet_id, csv_data] : tabs) {
      std::string sheet_name = "Unknown";
      std::string header = "";

      auto file_meta = sheet_metadata.find(filename);
      if (file_meta != sheet_metadata.end()) {
        auto tab_meta = file_meta->second.find(sheet_id);
        if (tab_meta != file_meta->second.end()) {
          sheet_name = tab_meta->second.sheet_name;
          header = tab_meta->second.header;
        }
      }

      context += std::format(
          "\n----------------------\n"
          "File: {}\n"
          "Tab: {} (gid: {})\n"
          "Header: {}\n"
          "CSV data:\n{}",
          filename, sheet_name, sheet_id, header, csv_data);
    }
  }

  context += "\n----------------------\n";
  return context;
}

// Fetch image data and encode them for the LLM

dpp::task<ollama::images>
Nissefar::generate_images(std::vector<dpp::attachment> attachments) {
  ollama::images imagelist;
  for (auto attachment : attachments) {
    if (attachment.content_type == "image/jpeg" ||
        attachment.content_type == "image/webp" ||
        attachment.content_type == "image/png") {
      dpp::http_request_completion_t attachment_data =
          co_await bot->co_request(attachment.url, dpp::m_get);
      bot->log(dpp::ll_info,
               std::format("Image size: {}", attachment_data.body.size()));
      imagelist.push_back(ollama::image(
          macaron::Base64::Encode(std::string(attachment_data.body))));
    }
  }
  co_return imagelist;
}

// Format the last message that is not in the channel message ringbuffer
// This is to show the message that it will respond to.

std::string Nissefar::format_replyto_message(const Message &msg) {
  std::string message_text =
      std::format("\nThe message you reply to:\n"
                  "----------------------\n"
                  "Message id: {}\nReply to message id: {}\n"
                  "Author: {}\n"
                  "Message content: {}"
                  "\n----------------------\n",
                  msg.msg_id.str(), msg.msg_replied_to.str(), msg.author.str(),
                  msg.content);

  return message_text;
}

// The function to generate the LLM text

std::string Nissefar::generate_text(const std::string &prompt,
                                    const ollama::images &imagelist,
                                    const GenerationType gen_type) {
  ollama::request req;
  ollama::options opts;

  opts["num_predict"] = 1000;
  opts["num_ctx"] = 40000;
  // opts["temperature"] = 1;
  // opts["top_k"] = 64;
  // opts["top_p"] = 0.95;
  // opts["min_p"] = 0;

  req["prompt"] = prompt;
  req["options"] = opts["options"];

  using enum GenerationType;
  switch (gen_type) {
  case TextReply:
    req["system"] = config.system_prompt;
    if (imagelist.size() > 0) {
      req["images"] = imagelist;
      req["model"] = config.vision_model;
    } else {
      req["model"] = config.text_model;
    }
    break;
  case Diff:
    req["system"] = config.diff_system_prompt;
    req["model"] = config.comparison_model;
    break;
  case ImageDescription:
    req["system"] = config.image_description_system_prompt;
    req["model"] = config.image_description_model;
    req["images"] = imagelist;
    break;
  }

  std::string answer{};
  try {
    answer = ollama::generate(req);
  } catch (ollama::exception e) {
    answer = std::format("Exception running llm: {}", e.what());
  }

  if (gen_type == ImageDescription) {
    bot->log(dpp::ll_info, std::format("Got image description: {}", answer));
  }

  // Discord messages does not support more than 2000 charachters, leave room
  // for url in some cases

  if (answer.length() > 1800)
    answer.resize(1800);

  return answer;
}

// Coroutine to handle messages from any channel and or server
// Respond when mentioned with the LLM
// Add messages to a per channel message queue

dpp::task<void> Nissefar::handle_message(const dpp::message_create_t &event) {

  bool answer = false;

  dpp::guild *current_server = dpp::find_guild(event.msg.guild_id);
  dpp::channel *current_chan = dpp::find_channel(event.msg.channel_id);

  if (current_server->name == "tyfon's server")
    co_return;

  bot->log(dpp::ll_info,
           std::format("#{} {}: {}", current_chan->name,
                       event.msg.author.format_username(), event.msg.content));

  for (auto mention : event.msg.mentions) {
    if (mention.second.user_id == bot->me.id &&
        event.msg.author.id != bot->me.id)
      //         event.msg.channel_id == 1337361807471546408) ||
      //        current_server->name == "tyfon's server")
      answer = true;
  }

  auto imagelist = co_await generate_images(event.msg.attachments);
  std::vector<std::string> image_desc{};

  for (auto image : imagelist) {
    ollama::images tmp_image;
    tmp_image.push_back(image);
    image_desc.push_back(generate_text("Describe the image.", tmp_image,
                                       GenerationType::ImageDescription));
  }

  /*
  format_usernameto emoji_rep = co_await
  bot->co_guild_emojis_get(current_server->id); auto emojis =
  emoji_rep.get<dpp::emoji_map>(); for (auto emoji : emojis)
  {
    bot->log(dpp::ll_info, std::format("Emoji: {}", emoji.second.format()));
  }
*/
  thread_local std::mt19937 gen(std::random_device{}());
  thread_local std::uniform_int_distribution<> dist(1, 1000);

  const int random_n = dist(gen);

  // React to approximately 5% of the messages

  Message last_message{event.msg.id, event.msg.message_reference.message_id,
                       event.msg.content, event.msg.author.id, image_desc};

  if (answer) {

    std::string prompt =
        std::format("\nBot user id: {}\n", bot->me.id.str()) +
        std::format("Channel name: \"{}\"\n", current_chan->name) +
        std::format("Current time: {:%Y-%m-%d %H:%M}\n",
                    std::chrono::zoned_time{std::chrono::current_zone(),
                                            std::chrono::system_clock::now()}) +
        format_sheet_context() +
        format_message_history(event.msg.channel_id) +
        format_replyto_message(last_message);

    bot->log(dpp::ll_info, prompt);
    bot->log(dpp::ll_info,
             std::format("Number of images: {}", imagelist.size()));

    event.reply(generate_text(prompt, imagelist, GenerationType::TextReply),
                true);
  }

  // Need to store last to avoid including in message history + need to
  // include bot in data
  store_message(last_message, current_server, current_chan,
                event.msg.author.format_username());

  co_return;
}

// Routine to handle message edits

dpp::task<void>
Nissefar::handle_message_update(const dpp::message_update_t &event) {

  bot->log(dpp::ll_info,
           std::format("Message with snowflake id {} was updated to {}",
                       event.msg.id.str(), event.msg.content));

  auto &db = Database::instance();
  auto res = db.execute(
      "select message_id from message where message_snowflake_id = $1",
      std::stol(event.msg.id.str()));

  if (!res.empty()) {
    std::uint64_t message_id = res[0].front().as<std::uint64_t>();
    auto res = db.execute("update message set content = $1 "
                          "where message_id = $2",
                          event.msg.content, message_id);
  }
  co_return;
}

// Use the diff command to compare data between two strings as a unified
// diff.

std::string Nissefar::diff(const std::string olddata, const std::string newdata,
                           const int sheet_id) {

  // Helper to sort CSV content: keep header, sort the rest
  auto sort_csv = [](const std::string &raw) -> std::string {
    if (raw.empty())
      return "";

    std::stringstream ss(raw);
    std::string line, header, result;
    std::vector<std::string> rows;

    // Grab header
    if (std::getline(ss, header)) {
      result += header + "\n";
    }
    // Grab remaining rows
    while (std::getline(ss, line)) {
      if (!line.empty()) {
        rows.push_back(line);
      }
    }

    // Sort rows alphabetically to ignore positional changes
    std::ranges::sort(rows);

    // Reassemble
    for (const auto &row : rows) {
      result += row + "\n";
    }
    return result;
  };

  std::string sorted_old = sort_csv(olddata);
  std::string sorted_new = sort_csv(newdata);

  std::array<char, 128> buffer;
  std::string result;

  // Create the temporary files and fill them
  std::string tempfiledir = std::filesystem::temp_directory_path();
  std::string oldtempfile = std::format("{}/nisseold{}", tempfiledir, sheet_id);

  std::string newtempfile = std::format("{}/nissenew{}", tempfiledir, sheet_id);

  // Use scope to ensure files are closed and flushed before diff runs
  {
    std::ofstream oldfile(oldtempfile);
    if (oldfile.is_open()) {
      oldfile << sorted_old;
    }

    std::ofstream newfile(newtempfile);
    if (newfile.is_open()) {
      newfile << sorted_new;
    }
  }

  // Easiest way to compare the file is just to pipe ye olde diff command
  // into a string

  std::unique_ptr<FILE, void (*)(FILE *)> pipe(
      popen(std::format("diff -u {} {}", oldtempfile, newtempfile).c_str(),
            "r"),
      [](FILE *f) -> void { std::ignore = pclose(f); });

  if (!pipe) {
    std::filesystem::remove(oldtempfile);
    std::filesystem::remove(newtempfile);
    return std::string("Kunne ikke kjøre diff pipe");
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) !=
         nullptr)
    result += buffer.data();

  std::filesystem::remove(oldtempfile);
  std::filesystem::remove(newtempfile);

  return result;
}

// Process the google shieeets

dpp::task<void> Nissefar::process_sheets(const std::string filename,
                                         const std::string file_id,
                                         std::string weblink) {

  // Loop over all the sheets

  bot->log(dpp::ll_info, std::format("Processing file {}", filename));

  std::string file_url =
      std::format("https://sheets.googleapis.com/v4/spreadsheets/"
                  "{}?key={}&fields=sheets.properties(sheetId,title)",
                  file_id, config.google_api_key);

  auto file_resp = co_await bot->co_request(file_url, dpp::m_get);

  if (file_resp.status != 200) {
    bot->log(dpp::ll_error, std::format("Error fetching sheets for file {}: {}",
                                        filename, file_resp.status));
    co_return;
  }

  auto file_data = nlohmann::json::parse(file_resp.body.data());

  for (auto sheet : file_data["sheets"]) {
    int sheet_id = sheet["properties"]["sheetId"].get<int>();
    std::string sheet_name = sheet["properties"]["title"].get<std::string>();

    std::string sheet_url =
        std::format("https://docs.google.com/spreadsheets/d/{}/"
                    "export?format=csv&gid={}",
                    file_id, sheet_id);

    // Google docs will often redirect to a new url with the location
    // headers and status 307 Make sure to try until a 200 response is hit.
    bool is_done = false;
    int redirect_count = 0;
    constexpr int max_redirects = 10;

    while (!is_done) {
      auto sheet_resp = co_await bot->co_request(sheet_url, dpp::m_get);

      // Found 307, try again with location
      if (sheet_resp.status == 307) {
        if (++redirect_count > max_redirects) {
          bot->log(dpp::ll_warning,
                   std::format("Too many redirects for sheet {}", sheet_id));
          is_done = true;
        } else {
          sheet_url = sheet_resp.headers.find("location")->second;
        }

        // Found actual data, proceed to check
      } else if (sheet_resp.status == 200) {

        auto newdata = std::format("{}\n", sheet_resp.body.data());

        std::istringstream nds(newdata);
        std::string header{};
        std::getline(nds, header);
        sheet_metadata[filename][sheet_id] = SheetTabMetadata{sheet_name,
                                                              header};

        // If we do not have this data before (probably first run)
        if (sheet_data[filename][sheet_id].empty()) {
          sheet_data[filename][sheet_id] = newdata;

          // Else do the comparison
        } else {
          if (sheet_data[filename][sheet_id] != newdata) {
            bot->log(dpp::ll_info,
                     std::format("The sheet \"{}\" has changed", sheet_name));

            sheet_diffs[filename][sheet_id] = Diffdata{
                diff(sheet_data[filename][sheet_id], newdata, sheet_id),
                weblink, header, sheet_name};
            sheet_data[filename][sheet_id] = newdata;
          }
        }
        is_done = true;
        // Handle unknown status code
      } else {
        bot->log(dpp::ll_info,
                 std::format("Error: unknown response: {}", sheet_resp.status));
        is_done = true;
      }
    }
  }
  co_return;
}

// Process the diffs. We do this after processing the shieets since it might
// take some time to run the llm and the google redirect links are short
// lived.

void Nissefar::process_diffs() {
  for (auto &[filename, diffmap] : sheet_diffs) {
    for (auto &[sheet_id, diffdata] : diffmap) {
      auto prompt = std::format(
          "Filename: {}\nSheet name: {}\nCSV Header: {}\nDiff:\n{}", filename,
          diffdata.sheet_name, diffdata.header, diffdata.diffdata);
      auto answer =
          generate_text(prompt, ollama::images{}, GenerationType::Diff);
      answer += std::format("\n{}", diffdata.weblink);
      dpp::message msg(1267731118895927347, answer);
      bot->message_create(msg);
    }
  }
  // Need to clear the diff data between each run :)
  sheet_diffs.clear();
}

// Check the google drive directory to see if the file timestamp has changed
// anywhere

dpp::task<void> Nissefar::process_google_docs() {
  bot->log(dpp::ll_info, "Processing directory");

  auto response = co_await bot->co_request(config.directory_url, dpp::m_get);

  auto directory_data = nlohmann::json::parse(response.body.data());

  for (auto filedata : directory_data["files"]) {
    std::string datestring = filedata["modifiedTime"].get<std::string>();
    std::string filename = filedata["name"].get<std::string>();
    if (filename != "TB test results" && filename != "Charging curves")
      continue;
    const std::string file_id = filedata["id"].get<std::string>();
    const std::string weblink = filedata["webViewLink"].get<std::string>();

    std::chrono::sys_time<std::chrono::milliseconds> tp;

    std::istringstream ds(datestring);

    ds >> std::chrono::parse("%Y-%m-%dT%H:%M:%S%Z", tp);

    if (ds.fail()) {
      bot->log(dpp::ll_info,
               std::format("Error parsing timestamp: {}", ds.str()));
    } else {
      const std::string ntime = std::format("{:%Y-%m-%d %H:%M:%S %Z}", tp);
      if (timestamps[filename].time_since_epoch() ==
          std::chrono::milliseconds(0)) {
        bot->log(dpp::ll_info,
                 std::format("New entry: {}, {}", filename, ntime));
        timestamps[filename] = tp;
        co_await process_sheets(filename, file_id, weblink);

        /* Uncomment for test
        if (filename == "TB test results") {
          timestamps[filename] -= std::chrono::minutes(1);
          sheet_data[filename][26964202] += std::string(
              "Mercedes Superexpensive,06.11.2022,Wet,3,Nokian R3,Winter,265
        / " "40 - 21,265 / 40 - 21,\"54,17\",\"3,96\",2800\n");
        }
        */

      } else {
        if (timestamps[filename] != tp) {
          const std::string otime =
              std::format("{:%Y-%m-%d %H:%M:%S %Z}", timestamps[filename]);
          timestamps[filename] = tp;
          bot->log(
              dpp::ll_info,
              std::format("File {} has changed.\nOld time: {}, New time: {}",
                          filename, otime, ntime));
          // Need to limit to "known" sheets or it will keep adding blank
          // id's to the map and fail
          if (filename == "TB test results") {
            co_await process_sheets(filename, file_id, weblink);
            process_diffs();
          } else {
            dpp::message msg(
                1267731118895927347,
                std::format("Charging curves has changed\n{}", weblink));
            bot->message_create(msg);
          }
        }
      }
    }
  }
  co_return;
}

dpp::task<void> Nissefar::process_youtube(bool first_run) {
  bot->log(dpp::ll_info, "Process youtube..");
  auto res = co_await bot->co_request(config.youtube_url, dpp::m_get);

  auto live_data = nlohmann::json::parse(res.body.data());

  if (live_data.find("pageInfo") != live_data.end()) {
    int live_count = live_data["pageInfo"]["totalResults"].get<int>();
    bot->log(dpp::ll_info, std::format("Live data: {}", live_count));

    if (live_count == 0 && config.is_streaming) {
      bot->log(dpp::ll_info, "Bjørn stopped streaming");
      /*
      dpp::message msg(1267731118895927347, "Bjørn stopped streaming");
      bot->message_create(msg);
      */
      config.is_streaming = false;
    }

    if (live_count > 0 && !config.is_streaming) {
      bot->log(dpp::ll_info, "Bjørn started streaming");
      if (!first_run) {
        std::vector<std::pair<std::string, std::string>> live_streams{};
        for (auto video_item : live_data["items"])
          live_streams.push_back(
              {video_item["id"]["videoId"].get<std::string>(),
               video_item["snippet"]["title"].get<std::string>()});

        std::string prompt =
            "Bjørn Nyland just started a live stream on youtube. Make your "
            "comment an "
            "announcement of that. Below are the titles of the live "
            "stream(s). "
            "Do not include any link to the stream. Do not include any user "
            "ids.";

        for (auto video : live_streams)
          prompt.append(std::format("\nLive stream title: {}", video.second));

        bot->log(dpp::ll_info, prompt);
        auto answer =
            generate_text(prompt, ollama::images{}, GenerationType::TextReply);

        for (auto video : live_streams)
          answer.append(
              std::format("\nhttps://www.youtube.com/watch?v={}", video.first));

        dpp::message msg(1267731118895927347, answer);
        bot->message_create(msg);
      }
      config.is_streaming = true;
    }
  } else {
    bot->log(dpp::ll_info, "Youtube: pageInfo key not found in json");
  }
  co_return;
}

void Nissefar::store_message(const Message &message, dpp::guild *server,
                             dpp::channel *channel,
                             const std::string user_name) {
  int server_id{0};
  int channel_id{0};
  int user_id{0};

  auto &db = Database::instance();

  auto res =
      db.execute("select server_id from server where server_snowflake_id = $1",
                 std::stol(server->id.str()));

  if (res.empty()) {
    res = db.execute("insert into server (server_name, server_snowflake_id) "
                     "values ($1, $2) returning server_id",
                     server->name, std::stol(server->id.str()));
    if (!res.empty())
      server_id = res.front()["server_id"].as<int>();
  } else {
    server_id = res.front()["server_id"].as<int>();
  }

  res = db.execute(
      "select channel_id from channel where channel_snowflake_id = $1",
      std::stol(channel->id.str()));

  if (res.empty()) {
    res = db.execute(
        "insert into channel (channel_name, server_id, channel_snowflake_id) "
        "values ($1, $2, $3) returning channel_id",
        channel->name, server_id, std::stol(channel->id.str()));
    if (!res.empty())
      channel_id = res.front()["channel_id"].as<int>();
  } else {
    channel_id = res.front()["channel_id"].as<int>();
  }

  res = db.execute(
      "select user_id from discord_user where user_snowflake_id = $1",
      std::stol(message.author.str()));

  if (res.empty()) {
    res = db.execute("insert into discord_user (user_name, user_snowflake_id) "
                     "values ($1, $2) returning user_id",
                     user_name, std::stol(message.author.str()));

    if (!res.empty())
      user_id = res.front()["user_id"].as<int>();
  } else {
    user_id = res.front()["user_id"].as<int>();
  }

  res = db.execute(
      "insert into message (user_id, channel_id, content, "
      "message_snowflake_id, reply_to_snowflake_id, image_descriptions) "
      "values "
      "($1, $2, $3, $4, $5, $6) returning message_id",
      user_id, channel_id, message.content, std::stol(message.msg_id.str()),
      std::stol(message.msg_replied_to.str()), message.image_descriptions);

  bot->log(
      dpp::ll_info,
      std::format("server_id: {} channel id: {} user_id: {}, message_id {}",
                  server_id, channel_id, user_id,
                  res.front()["message_id"].as<int>()));
}

dpp::task<void> Nissefar::setup_slashcommands() {
  if (dpp::run_once<struct register_bot_commands>()) {
    bot->global_bulk_command_delete();
    dpp::slashcommand pingcommand("ping", "Ping the nisse", bot->me.id);
    dpp::slashcommand chanstats("chanstats", "Show stats for the channel",
                                bot->me.id);
    chanstats.add_option(dpp::command_option(dpp::co_channel, "channel",
                                             "Stats from this channel", false));

    bot->global_bulk_command_create({pingcommand, chanstats});
    bot->log(dpp::ll_info, "Slashcommands setup");
  }
  co_return;
}

dpp::task<void>
Nissefar::handle_slashcommand(const dpp::slashcommand_t &event) {

  bot->log(dpp::ll_info,
           std::format("Slashcommand: {}", event.command.get_command_name()));

  if (event.command.get_command_name() == "ping") {
    // Processing takes more than 3 seconds, respond to not time out
    event.thinking(
        true, [event, this](const dpp::confirmation_callback_t &callback) {
          auto answer = generate_text(
              std::format("The user {} pinged you with the ping command",
                          event.command.get_issuing_user().id.str()),
              ollama::images{}, GenerationType::TextReply);
          event.edit_original_response(
              dpp::message(answer).set_flags(dpp::m_ephemeral));
        });
  } else if (event.command.get_command_name() == "chanstats") {
    event.thinking(false, [event,
                           this](const dpp::confirmation_callback_t &callback) {
      dpp::channel channel;

      try {

        channel = *dpp::find_channel(
            std::get<dpp::snowflake>(event.get_parameter("channel")));
      } catch (const std::bad_variant_access &ex) {
        channel = event.command.channel;
      }

      bot->log(dpp::ll_info, std::format("Channel: {}", channel.name));

      auto &db = Database::instance();
      auto res = db.execute(
          "select"
          "  u.user_name "
          ", count(*) as nmsgs "
          ", sum(coalesce(array_length(image_descriptions, 1),0)) as nimages "
          "from message m "
          "inner join discord_user u on (m.user_id = u.user_id) "
          "inner join channel c on (m.channel_id = c.channel_id) "
          "where c.channel_snowflake_id = $1 "
          "and u.user_snowflake_id != $2 "
          "group by u.user_name "
          "order by nmsgs desc, nimages desc "
          " limit 20",
          std::stol(channel.id.str()), std::stol(bot->me.id.str()));

      if (res.empty())
        event.edit_original_response(
            dpp::message("No messages posted in this channel"));
      else {
        event.edit_original_response(
            dpp::message(format_chanstat(res, channel.name)));
      }
    });
  }
  co_return;
}

size_t Nissefar::utf8_len(std::string &text) {
  size_t len = 0;
  const char *s = text.c_str();
  while (*s)
    len += (*s++ & 0xc0) != 0x80;
  return len;
}

void Nissefar::pad_right(std::string &text, const size_t num,
                         const std::string pad_char) {
  auto len = num - utf8_len(text);
  for (int i = 0; i < len; i++) {
    text.append(pad_char);
  }
}

void Nissefar::pad_left(std::string &text, const size_t num,
                        const std::string pad_char) {
  auto len = num - utf8_len(text);

  for (int i = 0; i < len; i++) {
    text.insert(0, pad_char);
  }
}

std::string Nissefar::format_chanstat(const pqxx::result res,
                                      std::string channel) {

  channel.insert(0, "#");

  // Need fix, this will chop too many characters if UTF-8 is present
  // Tabel structure still fine

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

    // Need fix, this will chop too many characters if UTF-8 is present
    // Tabel structure still fine
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

dpp::task<void>
Nissefar::remove_reaction(const dpp::message_reaction_remove_t &event) {
  std::string emoji{};
  if (event.reacting_emoji.format().size() > 4)
    emoji = std::format("<:{}>", event.reacting_emoji.format());
  else
    emoji = event.reacting_emoji.format();

  bot->log(dpp::ll_info, std::format("message: {}, reaction removed: {}",
                                     event.message_id.str(), emoji));

  auto &db = Database::instance();

  auto react_res =
      db.execute("select r.reaction_id "
                 "from reaction r "
                 "inner join message m on (m.message_id = r.message_id) "
                 "inner join discord_user u on (u.user_id = r.user_id) "
                 "where u.user_snowflake_id = $1 "
                 "and m.message_snowflake_id = $2 "
                 "and r.reaction = $3",
                 std::stol(event.reacting_user_id.str()),
                 std::stol(event.message_id.str()), emoji);

  if (!react_res.empty()) {
    std::uint64_t react_id = react_res[0].front().as<std::uint64_t>();
    bot->log(dpp::ll_info, std::format("Deleting reaction id {}", react_id));

    auto del_res =
        db.execute("delete from reaction where reaction_id = $1", react_id);
  }

  co_return;
}

dpp::task<void>
Nissefar::handle_reaction(const dpp::message_reaction_add_t &event) {
  std::string emoji{};
  if (event.reacting_emoji.format().size() > 4)
    emoji = std::format("<:{}>", event.reacting_emoji.format());
  else
    emoji = event.reacting_emoji.format();

  auto &db = Database::instance();
  auto msg_res = db.execute(
      "select message_id from message where message_snowflake_id = $1",
      std::stol(event.message_id.str()));

  if (!msg_res.empty()) {
    std::uint64_t message_id = msg_res[0].front().as<std::uint64_t>();

    auto user_res = db.execute("select user_id from discord_user "
                               "where user_snowflake_id = $1",
                               std::stol(event.reacting_user.id.str()));
    if (!user_res.empty()) {
      std::uint64_t user_id = user_res[0].front().as<std::uint64_t>();

      auto up_res =
          db.execute("insert into reaction (message_id, user_id, reaction) "
                     "values ($1, $2, $3)",
                     message_id, user_id, emoji);
      bot->log(dpp::ll_info,
               std::format("message: {}, user: {}, reaction added: {}",
                           message_id, event.reacting_user.format_username(),
                           emoji));
    }
  }

  const std::vector<std::string> message_texts = {
      std::format("<@{}> why {}", event.reacting_user.id.str(), emoji),
      std::format("<@{}> why {}?", event.reacting_user.id.str(), emoji),
      std::format("why {} <@{}>", emoji, event.reacting_user.id.str()),
      std::format("why {} <@{}>?", emoji, event.reacting_user.id.str())};

  thread_local std::mt19937 gen(std::random_device{}());
  thread_local std::uniform_int_distribution<> distn(0, 1000);

  if (event.message_author_id == bot->me.id &&
      event.channel_id == 1337361807471546408 &&
      event.reacting_emoji.format() == "🤡" && distn(gen) < 200) {

    thread_local std::uniform_int_distribution<> distm(0, 3);
    thread_local std::uniform_int_distribution<> dists(1, 5);

    dpp::message msg(event.channel_id, message_texts[distm(gen)]);
    std::this_thread::sleep_for(std::chrono::seconds(dists(gen)));
    bot->message_create(msg);
  }
  co_return;
}

void Nissefar::run() {

  auto &db = Database::instance();
  if (db.initialize(config.db_connection_string))
    std::cout << "Connected to db" << std::endl;
  else
    std::cout << "Failed to connect to db" << std::endl;

  bot->on_message_create(
      [this](const dpp::message_create_t &event) -> dpp::task<void> {
        co_return co_await handle_message(event);
      });

  bot->on_message_update(
      [this](const dpp::message_update_t &event) -> dpp::task<void> {
        co_return co_await handle_message_update(event);
      });

  bot->on_message_reaction_add(
      [this](const dpp::message_reaction_add_t &event) -> dpp::task<void> {
        co_return co_await handle_reaction(event);
      });

  bot->on_message_reaction_remove(
      [this](const dpp::message_reaction_remove_t &event) -> dpp::task<void> {
        co_return co_await remove_reaction(event);
      });

  bot->log(dpp::ll_info, "Initial process of sheets");
  bot->on_ready([this](const dpp::ready_t &event) -> dpp::task<void> {
    // Only run slashcommands setup when changing things
    // co_await setup_slashcommands();
    co_await process_youtube(true);
    co_await process_google_docs();
    co_return;
  });

  bot->on_slashcommand(
      [this](const dpp::slashcommand_t &event) -> dpp::task<void> {
        co_return co_await handle_slashcommand(event);
      });

  bot->log(dpp::ll_info, "Starting directory timer, 300 seconds");
  bot->start_timer(
      [this](const dpp::timer &timer) -> dpp::task<void> {
        co_return co_await process_google_docs();
      },
      300);

  bot->log(dpp::ll_info, "Starting youtube timer, 1500 seconds");
  bot->start_timer(
      [this](const dpp::timer &timer) -> dpp::task<void> {
        co_return co_await process_youtube(false);
      },
      1500);

  bot->log(dpp::ll_info, "Starting bot..");
  bot->start(dpp::st_wait);
}
