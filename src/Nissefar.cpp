#include <Nissefar.h>
#include <ctime>
#include <sstream>
#include <stdexcept>

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
    message_history += std::format("\n----------------------\n"
                                   "Message id: {}\n"
                                   "Reply to message id: {}\n"
                                   "Author: {}\n"
                                   "Message content:{}",
                                   msg.msg_id.str(), msg.msg_replied_to.str(),
                                   msg.author.str(), msg.content);
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
  std::string message_text = std::format(
      "\nThe message you reply to:\n"
      "----------------------\nMessage id: {}\nReply to message "
      "id: {}\nAuthor: {}\nMessage content:{}\n----------------------\n",
      msg.msg_id.str(), msg.msg_replied_to.str(), msg.author.str(),
      msg.content);

  return message_text;
}

// The function to generate the LLM text

std::string Nissefar::generate_reply(const std::string &prompt,
                                     const ollama::images &imagelist,
                                     bool is_diff) {
  ollama::request req;
  ollama::options opts;

  opts["num_predict"] = 1000;
  opts["temperature"] = 1;
  opts["top_k"] = 64;
  opts["top_p"] = 0.95;
  opts["min_p"] = 0;

  req["system"] = config.system_prompt;
  req["prompt"] = prompt;
  req["options"] = opts["options"];

  if (imagelist.size() > 0) {
    req["model"] = config.vision_model;
    req["images"] = imagelist;
  } else if (is_diff) {
    req["model"] = config.comparison_model;
    req["system"] = config.diff_system_prompt;
  } else {
    req["model"] = config.text_model;
  }

  std::string answer{};
  try {
    answer = ollama::generate(req);
  } catch (ollama::exception e) {
    answer = std::format("Exception running llm: {}", e.what());
  }

  return answer;
}

// Coroutine to handle messages from any channel and or server
// Respond when mentioned with the LLM
// Add messages to a per channel message queue

dpp::task<void> Nissefar::handle_message(const dpp::message_create_t &event) {

  bool answer = false;

  dpp::guild *current_server = dpp::find_guild(event.msg.guild_id);
  dpp::channel *current_chan = dpp::find_channel(event.msg.channel_id);

  bot->log(dpp::ll_info,
           std::format("Message on server \"{}\", channel \"{}\", channel id: "
                       "\"{}\", user: \"{}\": {}",
                       current_server->name, current_chan->name,
                       event.msg.channel_id.str(), event.msg.author.id.str(),
                       event.msg.content));

  for (auto mention : event.msg.mentions) {
    if (mention.second.user_id == bot->me.id &&
        event.msg.author.id != bot->me.id)
      answer = true;
  }

  Message last_message{event.msg.id, event.msg.message_reference.message_id,
                       event.msg.content, event.msg.author.id};

  if (answer) {
    std::string prompt =
        std::format("\nBot user id: {}\n", bot->me.id.str()) +
        std::format("Channel name: \"{}\"\n", current_chan->name) +
        format_message_history(event.msg.channel_id) +
        format_replyto_message(last_message);

    auto imagelist = co_await generate_images(event.msg.attachments);

    bot->log(dpp::ll_info, prompt);
    bot->log(dpp::ll_info,
             std::format("Number of images: {}", imagelist.size()));

    event.reply(generate_reply(prompt, imagelist, false), true);
  }

  add_channel_message(event.msg.channel_id, last_message);

  co_return;
}

// Use the diff command to compare data between two strings as a unified diff.

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

  // Easiest way to compare the file is just to pipe ye olde diff command into a
  // string

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

    // Google docs will often redirect to a new url with the location headers
    // and status 307 Make sure to try until a 200 response is hit. Vulnable to
    // redirect "bomb", need to fix
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
// take some time to run the llm and the google redirect links are short lived.

void Nissefar::process_diffs() {
  for (auto &[filename, diffmap] : sheet_diffs) {
    for (auto &[sheet_id, diffdata] : diffmap) {
      auto prompt = std::format(
          "Filename: {}\nSheet name: {}\nCSV Header: {}\nDiff:\n{}", filename,
          sheet_names[filename][sheet_id], diffdata.header, diffdata.diffdata);
      auto answer = generate_reply(prompt, ollama::images{}, true);
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
              "Mercedes Superexpensive,06.11.2022,Wet,3,Nokian R3,Winter,265 / "
              "40 - 21,265 / 40 - 21,\"54,17\",\"3,96\",2800\n");
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
          // Need to limit to "known" sheets or it will keep adding blank id's
          // to the map and fail
          co_await process_sheets(filename, file_id, weblink);
          process_diffs();
        }
      }
    }
  }
  co_return;
}

void Nissefar::run() {

  bot->on_message_create(
      [this](const dpp::message_create_t &event) -> dpp::task<void> {
        co_return co_await handle_message(event);
      });

  bot->log(dpp::ll_info, "Initial process of sheets");
  bot->on_ready([this](const dpp::ready_t &event) -> dpp::task<void> {
    co_return co_await process_google_docs();
  });

  bot->log(dpp::ll_info, "Starting directory timer, 300 seconds");
  bot->start_timer(
      [this](const dpp::timer &timer) -> dpp::task<void> {
        co_return co_await process_google_docs();
      },
      300);

  bot->log(dpp::ll_info, "Starting bot..");
  bot->start(dpp::st_wait);
}
