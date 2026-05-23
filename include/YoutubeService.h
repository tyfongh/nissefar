#ifndef YOUTUBESERVICE_H
#define YOUTUBESERVICE_H

#include <Config.h>
#include <LlmService.h>
#include <dpp/dpp.h>
#include <optional>
#include <mutex>
#include <string>

class YoutubeService {
public:
  struct StreamStatus {
    bool is_live;
    std::string title;
    std::optional<std::string> started_at;
    std::optional<std::int64_t> duration_seconds;
  };

  YoutubeService(Config &config, dpp::cluster &bot, const LlmService &llm_service);

  dpp::task<void> process(bool first_run);
  StreamStatus get_stream_status() const;
  dpp::task<bool> is_from_ignored_channel(const std::string &video_id) const;

private:
  Config &config;
  dpp::cluster &bot;
  const LlmService &llm_service;
  mutable std::mutex stream_status_mutex;
  bool stream_is_live = false;
  std::string stream_title;
  std::optional<std::string> stream_started_at;
};

#endif // YOUTUBESERVICE_H
