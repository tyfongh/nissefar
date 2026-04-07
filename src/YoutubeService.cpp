#include <YoutubeService.h>

YoutubeService::YoutubeService(Config &config, dpp::cluster &bot,
                               const LlmService &llm_service)
    : config(config), bot(bot), llm_service(llm_service) {}

dpp::task<bool> YoutubeService::is_from_ignored_channel(const std::string &video_id) const {
  if (config.youtube_skip_channel_names.empty())
    co_return false;

  const std::string url = std::format(
      "https://www.googleapis.com/youtube/v3/videos?part=snippet&id={}&key={}",
      video_id, config.google_api_key);

  auto res = co_await bot.co_request(url, dpp::m_get);

  try {
    auto data = nlohmann::json::parse(res.body);
    if (data.contains("items") && data["items"].is_array() &&
        !data["items"].empty()) {
      const auto &snippet = data["items"][0]["snippet"];
      if (snippet.contains("channelTitle") &&
          snippet["channelTitle"].is_string()) {
        const std::string channel_title =
            snippet["channelTitle"].get<std::string>();
        for (const auto &name : config.youtube_skip_channel_names) {
          if (channel_title == name)
            co_return true;
        }
      }
    }
  } catch (...) {
  }

  co_return false;
}

YoutubeService::StreamStatus YoutubeService::get_stream_status() const {
  std::lock_guard<std::mutex> lock(stream_status_mutex);
  return StreamStatus{stream_is_live, stream_title};
}

dpp::task<void> YoutubeService::process(bool first_run) {
  bot.log(dpp::ll_info, "Process youtube..");
  auto res = co_await bot.co_request(config.youtube_url, dpp::m_get);

  auto live_data = nlohmann::json::parse(res.body.data());

  if (live_data.find("pageInfo") != live_data.end()) {
    int live_count = live_data["pageInfo"]["totalResults"].get<int>();
    std::string latest_stream_title;
    if (live_count > 0 && live_data.contains("items") &&
        live_data["items"].is_array() && !live_data["items"].empty() &&
        live_data["items"][0].contains("snippet") &&
        live_data["items"][0]["snippet"].contains("title") &&
        live_data["items"][0]["snippet"]["title"].is_string()) {
      latest_stream_title =
          live_data["items"][0]["snippet"]["title"].get<std::string>();
    }

    {
      std::lock_guard<std::mutex> lock(stream_status_mutex);
      stream_is_live = live_count > 0;
      stream_title = latest_stream_title;
    }

    bot.log(dpp::ll_info, std::format("Live data: {}", live_count));

    if (live_count == 0 && config.is_streaming) {
      bot.log(dpp::ll_info, "Bjørn stopped streaming");
      config.is_streaming = false;
    }

    if (live_count > 0 && !config.is_streaming) {
      bot.log(dpp::ll_info, "Bjørn started streaming");
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

        bot.log(dpp::ll_info, prompt);
        auto answer = llm_service.generate_text(
            prompt, ollama::images{}, LlmService::GenerationType::TextReply);

        for (auto video : live_streams)
          answer.append(
              std::format("\nhttps://www.youtube.com/watch?v={}", video.first));

        dpp::message msg(1267731118895927347, answer);
        bot.message_create(msg);
      }
      config.is_streaming = true;
    }
  } else {
    bot.log(dpp::ll_info, "Youtube: pageInfo key not found in json");
  }
  co_return;
}
