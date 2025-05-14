#include <Database.h>
#include <Nissefar.h>
#include <dpp/misc-enum.h>
#include <dpp/nlohmann/json_fwd.hpp>
#include <dpp/queues.h>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
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

// This function will add a message to the channel ringbuffer
// If max_history is reached it will remove the oldest.

void Nissefar::add_channel_message(dpp::snowflake channel_id,
                                   const Message &msg) {
  auto &buffer = channel_history[channel_id];
  if (buffer.size() == config.max_history)
    buffer.pop_front();
  buffer.push_back(msg);
}

// Retrieve the channel history as an iterable data construct

const std::deque<Message> &
Nissefar::get_channel_history(dpp::snowflake channel_id) const {
  static const std::deque<Message> empty;
  auto history = channel_history.find(channel_id);
  if (history == channel_history.end())
    return empty;
  return history->second;
}

// Format the message history for the bots context

std::string Nissefar::format_message_history(dpp::snowflake channel_id) {
  std::string message_history{};
  auto msgs = get_channel_history(channel_id);
  if (msgs.size() > 0)
    message_history = std::string("Channel message history:");
  for (auto msg : get_channel_history(channel_id)) {
    std::string image_descs{};
    int i = 0;
    for (auto image_desc : msg.image_descriptions) {
      i++;
      image_descs.append(std::format("\nImage {}: {}", i, image_desc));
    }

    message_history += std::format("\n----------------------\n"
                                   "Message id: {}\n"
                                   "Reply to message id: {}\n"
                                   "Author: {}\n"
                                   "Message content:{}{}",
                                   msg.msg_id.str(), msg.msg_replied_to.str(),
                                   msg.author.str(), msg.content, image_descs);
  }
  if (!message_history.empty())
    message_history += "\n----------------------";
  return message_history;
}

// Fetch image data and encode them for the LLM

dpp::task<ollama::images>
Nissefar::generate_images(std::vector<dpp::attachment> attachments) {
  ollama::images imagelist;
  for (auto attachment : attachments) {
    if (attachment.content_type == "image/jpeg" ||
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
                  "Message content:{}"
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
  opts["temperature"] = 1;
  opts["top_k"] = 64;
  opts["top_p"] = 0.95;
  opts["min_p"] = 0;

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
  case Reaction:
    req["system"] = config.reaction_system_prompt;
    req["model"] = config.reaction_model;
    if (imagelist.size() > 0)
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

// Routine to get the classification of the message mood according to the LLM

std::string Nissefar::get_message_mood(const std::string content,
                                       const ollama::images &imagelist) {
  std::string mood =
      generate_text(content, imagelist, GenerationType::Reaction);
  while (!mood.empty() && (mood.back() == '\n' || mood.back() == '\r'))
    mood.pop_back();
  return mood;
}

void Nissefar::add_message_reaction(const std::string mood,
                                    const dpp::snowflake channel_id,
                                    const dpp::snowflake message_id) {

  const std::vector<std::string> charge_emotes = {
      "🔋", "⚡", "supercharger:1285697435653373972",
      "tesla:1267788459049877534"};

  const std::vector<std::string> neutral_emotes = {
      "3Head:1289162421960704070", "clueless:1268236855367962707",
      "KKomrade:1300359527929221151", "Okayge:1267829394009882665"};

  const std::vector<std::string> funny_emotes = {
      "ICANT:1268258243424157859", "KEKW:1267825467533295769",
      "KKonaW:1288852238869069844", "omegalul:1267788480503615518"};

  const std::vector<std::string> sad_emotes = {
      "Sadge:1274422757836460233", "Okayge:1267829394009882665",
      "haHAA:1290193349772574760", "NotLikeThis:1267826684963323975"};

  thread_local std::mt19937 gen(std::random_device{}());
  thread_local std::uniform_int_distribution<> dist(0, 3);

  const int react_rnd = dist(gen);

  std::string reaction{};
  if (mood == "Happy")
    reaction = "🤗";
  else if (mood == "Funny")
    reaction = funny_emotes[react_rnd];
  else if (mood == "Sad")
    reaction = sad_emotes[react_rnd];
  else if (mood == "Angry")
    reaction = "🤬";
  else if (mood == "Copium")
    reaction = "copium:1267788485788438630";
  else if (mood == "Clown")
    reaction = "🤡";
  else if (mood == "Enthusiastic EV")
    reaction = charge_emotes[react_rnd];
  else if (mood == "Neutral")
    reaction = neutral_emotes[react_rnd];

  if (!reaction.empty())
    bot->message_add_reaction(message_id, channel_id, reaction);
  bot->log(dpp::ll_info, std::format("Suggested reaction: {}", reaction));
}

// Coroutine to handle messages from any channel and or server
// Respond when mentioned with the LLM
// Add messages to a per channel message queue

dpp::task<void> Nissefar::handle_message(const dpp::message_create_t &event) {

  bool answer = false;

  dpp::guild *current_server = dpp::find_guild(event.msg.guild_id);
  dpp::channel *current_chan = dpp::find_channel(event.msg.channel_id);

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

  std::string mood_msg{};
  if (event.msg.content.empty() && imagelist.size() > 0)
    mood_msg = image_desc[0];
  else
    mood_msg = event.msg.content;

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
  std::string mood = get_message_mood(mood_msg, imagelist);
  bot->log(dpp::ll_info, std::format("Message mood: {}, {}", mood, random_n));

  // React to approximately 5% of the messages
  if (event.msg.author.id != bot->me.id &&
      (current_server->name == "tyfon's server" ||
       (event.msg.channel_id == 1337361807471546408 && random_n <= 200)))
    add_message_reaction(mood, event.msg.channel_id, event.msg.id);

  Message last_message{
      event.msg.id,        event.msg.message_reference.message_id,
      event.msg.content,   mood,
      event.msg.author.id, image_desc};

  if (event.msg.author.id != bot->me.id)
    store_message(last_message, current_server, current_chan,
                  event.msg.author.format_username());

  if (answer) {
    std::string prompt =
        std::format("\nBot user id: {}\n", bot->me.id.str()) +
        std::format("Channel name: \"{}\"\n", current_chan->name) +
        format_message_history(event.msg.channel_id) +
        format_replyto_message(last_message);

    bot->log(dpp::ll_info, prompt);
    bot->log(dpp::ll_info,
             std::format("Number of images: {}", imagelist.size()));

    event.reply(generate_text(prompt, imagelist, GenerationType::TextReply),
                true);
  }

  add_channel_message(event.msg.channel_id, last_message);

  co_return;
}

// Use the diff command to compare data between two strings as a unified
// diff.

std::string Nissefar::diff(const std::string olddata, const std::string newdata,
                           const int sheet_id) {

  std::array<char, 128> buffer;
  std::string result;

  // Create the temporary files and fill them
  std::string tempfiledir = std::filesystem::temp_directory_path();
  std::string oldtempfile = std::format("{}/nisseold{}", tempfiledir, sheet_id);

  std::string newtempfile = std::format("{}/nissenew{}", tempfiledir, sheet_id);

  std::ofstream oldfile(oldtempfile);
  if (oldfile.is_open()) {
    oldfile << olddata;
    oldfile.flush();
  }

  std::ofstream newfile(newtempfile);
  if (newfile.is_open()) {
    newfile << newdata;
    newfile.flush();
  }

  // Easiest way to compare the file is just to pipe ye olde diff command
  // into a string

  std::unique_ptr<FILE, void (*)(FILE *)> pipe(
      popen(std::format("diff -u {} {}", oldtempfile, newtempfile).c_str(),
            "r"),
      [](FILE *f) -> void { std::ignore = pclose(f); });
  if (!pipe)
    return std::string("Kunne ikke kjøre diff pipe");

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

  for (const auto &[sheet_id, sheet_name] : sheet_names[filename]) {

    std::string sheet_url =
        std::format("https://docs.google.com/spreadsheets/d/{}/"
                    "export?format=csv&gid={}",
                    file_id, sheet_id);

    // Google docs will often redirect to a new url with the location
    // headers and status 307 Make sure to try until a 200 response is hit.
    // Vulnable to redirect "bomb", need to fix
    bool is_done = false;
    while (!is_done) {
      auto sheet_resp = co_await bot->co_request(sheet_url, dpp::m_get);

      // Found 307, try again with location
      if (sheet_resp.status == 307) {
        sheet_url = sheet_resp.headers.find("location")->second;

        // Found actual data, proceed to check
      } else if (sheet_resp.status == 200) {

        // If we do not have this data before (probably first run)
        if (sheet_data[filename][sheet_id].empty()) {
          sheet_data[filename][sheet_id] =
              std::format("{}\n", sheet_resp.body.data());

          // Else do the comparison
        } else {
          if (sheet_data[filename][sheet_id] !=
              std::format("{}\n", sheet_resp.body.data())) {
            bot->log(dpp::ll_info,
                     std::format("The sheet \"{}\" has changed", sheet_name));

            // Extract the CSV header of the file
            auto newdata = std::format("{}\n", sheet_resp.body.data());

            std::istringstream nds(newdata);
            std::string header{};
            std::getline(nds, header);

            sheet_diffs[filename][sheet_id] = Diffdata{
                diff(sheet_data[filename][sheet_id], newdata, sheet_id),
                weblink, header};
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
          sheet_names[filename][sheet_id], diffdata.header, diffdata.diffdata);
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
    std::string datestring = filedata["modifiedTime"].front();
    std::string filename = filedata["name"].front();
    if (filename != "TB test results" && filename != "Charging curves")
      break;
    const std::string file_id = filedata["id"].front();
    const std::string weblink = filedata["webViewLink"].front();

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
          co_await process_sheets(filename, file_id, weblink);
          process_diffs();
        }
      }
    }
  }
  co_return;
}

dpp::task<void> Nissefar::process_youtube() {
  bot->log(dpp::ll_info, "Process youtube..");
  auto res = co_await bot->co_request(config.youtube_url, dpp::m_get);

  auto live_data = nlohmann::json::parse(res.body.data());

  int live_count = live_data["pageInfo"]["totalResults"].front();
  bot->log(dpp::ll_info, std::format("Live data: {}", live_count));

  if (live_count == 0 && config.is_streaming) {
    bot->log(dpp::ll_info, "Bjørn stopped streaming");
    dpp::message msg(1267731118895927347, "Bjørn stopped streaming");
    bot->message_create(msg);
    config.is_streaming = false;
  }

  if (live_count > 0 && !config.is_streaming) {
    bot->log(dpp::ll_info, "Bjørn started streaming");
    dpp::message msg(1267731118895927347, "Bjørn started streaming");
    bot->message_create(msg);
    config.is_streaming = true;
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
      "insert into message (user_id, channel_id, content, mood, "
      "message_snowflake_id, reply_to_snowflake_id, image_descriptions) values "
      "($1, $2, $3, $4, $5, $6, $7) returning message_id",
      user_id, channel_id, message.content, message.mood,
      std::stol(message.author.str()), std::stol(message.msg_replied_to.str()),
      message.image_descriptions);

  bot->log(
      dpp::ll_info,
      std::format("server_id: {} channel id: {} user_id: {}, message_id {}",
                  server_id, channel_id, user_id,
                  res.front()["message_id"].as<int>()));
}

dpp::task<void> Nissefar::setup_slashcommands() {
  if (dpp::run_once<struct register_bot_commands>()) {
    dpp::slashcommand pingcommand("ping", "Ping the nisse", bot->me.id);
    dpp::slashcommand chanstats("chanstats", "Show stats for the channel",
                                bot->me.id);

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
          "group by u.user_name "
          "order by nmsgs desc, nimages desc",
          std::stol(event.command.channel_id.str()));

      if (res.empty())
        event.edit_original_response(
            dpp::message("No messages posted in this channel"));
      else {
        event.edit_original_response(
            dpp::message(format_chanstat(res, event.command.channel.name)));
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

  bot->log(dpp::ll_info, "Initial process of sheets");
  bot->on_ready([this](const dpp::ready_t &event) -> dpp::task<void> {
    // Only run slashcommands setup when changing things
    // co_await setup_slashcommands();
    co_await process_youtube();
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
        co_await process_google_docs();
        co_await process_youtube();
        co_return;
      },
      300);

  bot->log(dpp::ll_info, "Starting bot..");
  bot->start(dpp::st_wait);
}
