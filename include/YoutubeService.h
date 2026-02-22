#ifndef YOUTUBESERVICE_H
#define YOUTUBESERVICE_H

#include <Config.h>
#include <LlmService.h>
#include <dpp/dpp.h>

class YoutubeService {
public:
  YoutubeService(Config &config, dpp::cluster &bot, const LlmService &llm_service);

  dpp::task<void> process(bool first_run);

private:
  Config &config;
  dpp::cluster &bot;
  const LlmService &llm_service;
};

#endif // YOUTUBESERVICE_H
