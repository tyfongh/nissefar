#ifndef YOUTUBESERVICE_H
#define YOUTUBESERVICE_H

#include <Config.h>
#include <LlmService.h>
#include <dpp/dpp.h>
#include <mutex>
#include <string>

class YoutubeService {
public:
  struct StreamStatus {
    bool is_live;
    std::string title;
  };

  YoutubeService(Config &config, dpp::cluster &bot, const LlmService &llm_service);

  dpp::task<void> process(bool first_run);
  StreamStatus get_stream_status() const;

private:
  Config &config;
  dpp::cluster &bot;
  const LlmService &llm_service;
  mutable std::mutex stream_status_mutex;
  bool stream_is_live = false;
  std::string stream_title;
};

#endif // YOUTUBESERVICE_H
