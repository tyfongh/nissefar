#ifndef NISSEFAR_H
#define NISSEFAR_H

#include <Config.h>
#include <Domain.h>
#include <chrono>
#include <dpp/dpp.h>
#include <memory>
#include <pqxx/pqxx>
#include <string_view>

class LlmService;
class DiscordEventService;
class GoogleDocsService;
class YoutubeService;

class Nissefar {
private:
  // variables

  Config config{};
  std::unique_ptr<dpp::cluster> bot;
  std::unique_ptr<LlmService> llm_service;
  std::unique_ptr<DiscordEventService> discord_event_service;
  std::unique_ptr<GoogleDocsService> google_docs_service;
  std::unique_ptr<YoutubeService> youtube_service;

  // Methods

  dpp::task<void> setup_slashcommands();

public:
  Nissefar();
  ~Nissefar();
  void run();
};
#endif // NISSEFAR_H
