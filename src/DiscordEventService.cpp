#include <DbOps.h>
#include <DiscordEventService.h>
#include <Formatting.h>
#include <GoogleDocsService.h>
#include <WebPageService.h>
#include <YoutubeService.h>

#include <chrono>
#include <map>
#include <memory>
#include <random>
#include <ranges>
#include <thread>
#include <variant>

DiscordEventService::DiscordEventService(
    const Config &config, dpp::cluster &bot, const LlmService &llm_service,
    const GoogleDocsService &google_docs_service,
    const WebPageService &web_page_service,
    const YoutubeService &youtube_service)
    : config(config), bot(bot), llm_service(llm_service),
      google_docs_service(google_docs_service),
      web_page_service(web_page_service), youtube_service(youtube_service) {}

std::string
DiscordEventService::format_message_history(dpp::snowflake channel_id) const {
  std::string message_history{};

  auto res = dbops::fetch_channel_history(channel_id, config.max_history);
  if (!res.empty()) {
    message_history = "Channel message history:";

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
          dbops::fetch_reactions_for_message(message["message_id"].as<std::uint64_t>());

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

std::string DiscordEventService::format_replyto_message(const Message &msg) const {
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

void DiscordEventService::store_message(const Message &message, dpp::guild *server,
                                        dpp::channel *channel,
                                        const std::string &user_name) const {
  auto ids = dbops::store_message(message, server, channel, user_name);
  bot.log(dpp::ll_info,
          std::format("server_id: {} channel id: {} user_id: {}, message_id {}",
                      ids.server_id, ids.channel_id, ids.user_id,
                      ids.message_id));
}

dpp::task<void>
DiscordEventService::handle_message(const dpp::message_create_t &event) {
  bool answer = false;

  dpp::guild *current_server = dpp::find_guild(event.msg.guild_id);
  dpp::channel *current_chan = dpp::find_channel(event.msg.channel_id);

  if (current_server->name == "tyfon's server")
    co_return;

  bot.log(dpp::ll_info,
          std::format("#{} {}: {}", current_chan->name,
                      event.msg.author.format_username(), event.msg.content));

  for (auto mention : event.msg.mentions) {
    if (mention.second.user_id == bot.me.id && event.msg.author.id != bot.me.id)
      answer = true;
  }

  auto imagelist = co_await llm_service.generate_images(event.msg.attachments);
  std::vector<std::string> image_desc{};

  for (auto image : imagelist) {
    ollama::images tmp_image;
    tmp_image.push_back(image);
    image_desc.push_back(llm_service.generate_text(
        "Describe the image.", tmp_image,
        LlmService::GenerationType::ImageDescription));
  }

  Message last_message{event.msg.id, event.msg.message_reference.message_id,
                       event.msg.content, event.msg.author.id, image_desc};

  if (answer) {
    const std::vector<LlmService::ToolDefinition> available_tools = {
        {"get_banana_data", "Get EV trunk size dataset from Banana sheet", ""},
        {"get_weight_data", "Get EV vehicle weight dataset from Weight sheet", ""},
        {"get_acceleration_data",
         "Get EV acceleration dataset from Acceleration sheet", ""},
        {"get_noise_data", "Get EV vehicle noise dataset from Noise sheet", ""},
        {"get_range_data",
         "Get EV 90 and 120 km/h range and efficiency data from Range sheet", ""},
        {"get_1000km_data", "Get EV 1000 km challenge dataset", ""},
        {"get_youtube_stream_status",
         "Check whether the tracked YouTube stream is currently live. If live, returns the current stream title.",
         ""},
        {"get_webpage_text",
         "Fetch and extract readable text from a public webpage. Use this when the user asks to summarize or answer questions about a URL.",
         R"({"type":"object","properties":{"url":{"type":"string","description":"Absolute http/https URL to fetch"}},"required":["url"]})"}};

    const auto webpage_tool_calls = std::make_shared<int>(0);

    const auto execute_tool =
        [this, webpage_tool_calls](const std::string &tool_name,
               const std::string &arguments_json) -> dpp::task<std::string> {

      static const std::map<std::string, std::string> tool_to_sheet = {
          {"get_banana_data", "Banana"},
          {"get_weight_data", "Weight"},
          {"get_acceleration_data", "Acceleration"},
          {"get_noise_data", "Noise"},
          {"get_range_data", "Range"},
          {"get_1000km_data", "1000 km"}};

      if (tool_name == "get_webpage_text") {
        if (*webpage_tool_calls >= 1) {
          co_return "Tool error: only one webpage fetch is allowed per request.";
        }

        std::string requested_url;
        try {
          ollama::json args = ollama::json::parse(arguments_json);
          if (args.contains("url") && args["url"].is_string()) {
            requested_url = args["url"].get<std::string>();
          }
        } catch (...) {
          co_return "Tool error: invalid tool arguments JSON.";
        }

        if (requested_url.empty()) {
          co_return "Tool error: missing required argument 'url'.";
        }

        *webpage_tool_calls += 1;
        co_return co_await web_page_service.fetch_webpage_text(requested_url);
      }

      if (tool_name == "get_youtube_stream_status") {
        const auto status = youtube_service.get_stream_status();
        ollama::json payload = ollama::json::object();
        payload["is_live"] = status.is_live;
        if (status.is_live && !status.title.empty()) {
          payload["title"] = status.title;
        }
        co_return payload.dump();
      }

      auto it = tool_to_sheet.find(tool_name);
      if (it == tool_to_sheet.end()) {
        co_return std::format("Tool error: unknown tool '{}'", tool_name);
      }

      auto csv_data = google_docs_service.get_sheet_csv_by_tab_name(it->second);
      if (!csv_data.has_value()) {
        co_return std::format("Tool error: dataset '{}' is not loaded", it->second);
      }

      co_return std::format("Dataset: {}\nCSV data:\n{}", it->second, *csv_data);
    };

    std::string prompt =
        std::format("\nBot user id: {}\n", bot.me.id.str()) +
        std::format("Channel name: \"{}\"\n", current_chan->name) +
        std::format("Current time: {:%Y-%m-%d %H:%M}\n",
                    std::chrono::zoned_time{std::chrono::current_zone(),
                                            std::chrono::system_clock::now()}) +
        format_message_history(event.msg.channel_id) +
        format_replyto_message(last_message);

    bot.log(dpp::ll_info, prompt);
    bot.log(dpp::ll_info, std::format("Number of images: {}", imagelist.size()));

    auto tool_answer =
        co_await llm_service.generate_text_with_tools(prompt, imagelist,
                                                      available_tools,
                                                      execute_tool);
    event.reply(tool_answer, true);
  }

  store_message(last_message, current_server, current_chan,
                event.msg.author.format_username());

  co_return;
}

dpp::task<void> DiscordEventService::handle_message_update(
    const dpp::message_update_t &event) {
  bot.log(dpp::ll_info,
          std::format("Message with snowflake id {} was updated to {}",
                      event.msg.id.str(), event.msg.content));

  auto message_id = dbops::find_message_id(event.msg.id);
  if (message_id.has_value()) {
    dbops::update_message_content(*message_id, event.msg.content);
  }
  co_return;
}

dpp::task<void>
DiscordEventService::handle_slashcommand(const dpp::slashcommand_t &event) {
  bot.log(dpp::ll_info,
          std::format("Slashcommand: {}", event.command.get_command_name()));

  if (event.command.get_command_name() == "ping") {
    event.thinking(
        true, [event, this](const dpp::confirmation_callback_t &callback) {
          auto answer = llm_service.generate_text(
              std::format("The user {} pinged you with the ping command",
                          event.command.get_issuing_user().id.str()),
              ollama::images{}, LlmService::GenerationType::TextReply);
          event.edit_original_response(
              dpp::message(answer).set_flags(dpp::m_ephemeral));
        });
  } else if (event.command.get_command_name() == "chanstats") {
    event.thinking(false, [event,
                           this](const dpp::confirmation_callback_t &callback) {
      dpp::channel channel;

      try {
        channel =
            *dpp::find_channel(std::get<dpp::snowflake>(event.get_parameter("channel")));
      } catch (const std::bad_variant_access &ex) {
        channel = event.command.channel;
      }

      bot.log(dpp::ll_info, std::format("Channel: {}", channel.name));

      auto res = dbops::fetch_chanstats(channel.id, bot.me.id);

      if (res.empty())
        event.edit_original_response(
            dpp::message("No messages posted in this channel"));
      else {
        event.edit_original_response(
            dpp::message(format_chanstat_table(res, channel.name)));
      }
    });
  }
  co_return;
}

dpp::task<void> DiscordEventService::remove_reaction(
    const dpp::message_reaction_remove_t &event) {
  std::string emoji{};
  if (event.reacting_emoji.format().size() > 4)
    emoji = std::format("<:{}>", event.reacting_emoji.format());
  else
    emoji = event.reacting_emoji.format();

  bot.log(dpp::ll_info,
          std::format("message: {}, reaction removed: {}", event.message_id.str(),
                      emoji));

  auto react_id =
      dbops::find_reaction_id(event.reacting_user_id, event.message_id, emoji);
  if (react_id.has_value()) {
    bot.log(dpp::ll_info, std::format("Deleting reaction id {}", *react_id));
    dbops::delete_reaction(*react_id);
  }

  co_return;
}

dpp::task<void>
DiscordEventService::handle_reaction(const dpp::message_reaction_add_t &event) {
  std::string emoji{};
  if (event.reacting_emoji.format().size() > 4)
    emoji = std::format("<:{}>", event.reacting_emoji.format());
  else
    emoji = event.reacting_emoji.format();

  auto message_id = dbops::find_message_id(event.message_id);
  if (message_id.has_value()) {
    auto user_id = dbops::find_user_id(event.reacting_user.id);
    if (user_id.has_value()) {
      dbops::insert_reaction(*message_id, *user_id, emoji);
      bot.log(dpp::ll_info,
              std::format("message: {}, user: {}, reaction added: {}",
                          *message_id, event.reacting_user.format_username(),
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

  if (event.message_author_id == bot.me.id &&
      event.channel_id == 1337361807471546408 &&
      event.reacting_emoji.format() == "🤡" && distn(gen) < 200) {

    thread_local std::uniform_int_distribution<> distm(0, 3);
    thread_local std::uniform_int_distribution<> dists(1, 5);

    dpp::message msg(event.channel_id, message_texts[distm(gen)]);
    std::this_thread::sleep_for(std::chrono::seconds(dists(gen)));
    bot.message_create(msg);
  }
  co_return;
}
