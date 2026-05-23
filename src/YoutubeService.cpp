#include <YoutubeService.h>

#include <ctime>
#include <format>

namespace {

std::optional<std::string> extract_actual_start_time(const nlohmann::json &video_data) {
  if (!video_data.contains("items") || !video_data["items"].is_array() ||
      video_data["items"].empty() || !video_data["items"][0].is_object()) {
    return std::nullopt;
  }

  const auto &item = video_data["items"][0];
  if (!item.contains("liveStreamingDetails") ||
      !item["liveStreamingDetails"].is_object()) {
    return std::nullopt;
  }

  const auto &details = item["liveStreamingDetails"];
  if (!details.contains("actualStartTime") ||
      !details["actualStartTime"].is_string()) {
    return std::nullopt;
  }

  const std::string started_at = details["actualStartTime"].get<std::string>();
  if (started_at.empty()) {
    return std::nullopt;
  }

  return started_at;
}

std::optional<std::int64_t> parse_rfc3339_utc_seconds(const std::string &value) {
  if (value.size() < 20) {
    return std::nullopt;
  }

  std::tm tm{};
  tm.tm_year = std::stoi(value.substr(0, 4)) - 1900;
  tm.tm_mon = std::stoi(value.substr(5, 2)) - 1;
  tm.tm_mday = std::stoi(value.substr(8, 2));
  tm.tm_hour = std::stoi(value.substr(11, 2));
  tm.tm_min = std::stoi(value.substr(14, 2));
  tm.tm_sec = std::stoi(value.substr(17, 2));

  const std::time_t epoch = timegm(&tm);
  if (epoch < 0) {
    return std::nullopt;
  }

  return static_cast<std::int64_t>(epoch);
}

std::optional<std::int64_t>
duration_seconds_from_start_time(const std::optional<std::string> &started_at) {
  if (!started_at.has_value()) {
    return std::nullopt;
  }

  try {
    const auto started_epoch = parse_rfc3339_utc_seconds(*started_at);
    if (!started_epoch.has_value()) {
      return std::nullopt;
    }

    const auto now = std::time(nullptr);
    if (now < *started_epoch) {
      return 0;
    }

    return static_cast<std::int64_t>(now) - *started_epoch;
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace

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
  return StreamStatus{stream_is_live, stream_title, stream_started_at,
                      duration_seconds_from_start_time(stream_started_at)};
}

dpp::task<void> YoutubeService::process(bool first_run) {
  bot.log(dpp::ll_info, "Process youtube..");
  auto res = co_await bot.co_request(config.youtube_url, dpp::m_get);

  auto live_data = nlohmann::json::parse(res.body.data());

  if (live_data.find("pageInfo") != live_data.end()) {
    int live_count = live_data["pageInfo"]["totalResults"].get<int>();
    std::string latest_stream_title;
    std::optional<std::string> latest_stream_started_at;
    std::string latest_stream_video_id;
    if (live_count > 0 && live_data.contains("items") &&
        live_data["items"].is_array() && !live_data["items"].empty() &&
        live_data["items"][0].contains("snippet") &&
        live_data["items"][0]["snippet"].contains("title") &&
        live_data["items"][0]["snippet"]["title"].is_string()) {
      latest_stream_title =
          live_data["items"][0]["snippet"]["title"].get<std::string>();
      if (live_data["items"][0].contains("id") && live_data["items"][0]["id"].is_object() &&
          live_data["items"][0]["id"].contains("videoId") &&
          live_data["items"][0]["id"]["videoId"].is_string()) {
        latest_stream_video_id =
            live_data["items"][0]["id"]["videoId"].get<std::string>();
      }
    }

    if (!latest_stream_video_id.empty()) {
      const std::string details_url = std::format(
          "https://www.googleapis.com/youtube/v3/videos?part=liveStreamingDetails,snippet&id={}&key={}",
          latest_stream_video_id, config.google_api_key);
      auto details_res = co_await bot.co_request(details_url, dpp::m_get);
      try {
        latest_stream_started_at =
            extract_actual_start_time(nlohmann::json::parse(details_res.body.data()));
      } catch (...) {
      }
    }

    {
      std::lock_guard<std::mutex> lock(stream_status_mutex);
      stream_is_live = live_count > 0;
      stream_title = latest_stream_title;
      stream_started_at = latest_stream_started_at;
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
            "stream(s). Write the announcement in English. "
            "Do not include any link to the stream. Do not include any user "
            "ids.";

        for (auto video : live_streams)
          prompt.append(std::format("\nLive stream title: {}", video.second));

        bot.log(dpp::ll_info, prompt);
        auto answer = llm_service.generate_text(
            prompt, LlmImages{}, LlmService::GenerationType::TextReply);

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
