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

  dpp::task<void> handle_message(const dpp::message_create_t &event);
  dpp::task<void> handle_message_update(const dpp::message_update_t &event);
  dpp::task<void> handle_reaction(const dpp::message_reaction_add_t &event);
  dpp::task<void> remove_reaction(const dpp::message_reaction_remove_t &event);
  std::string format_sheet_context();
  dpp::task<void> process_google_docs();
  dpp::task<void> process_youtube(bool first_run);

  dpp::task<void> setup_slashcommands();
  dpp::task<void> handle_slashcommand(const dpp::slashcommand_t &event);

public:
  Nissefar();
  ~Nissefar();
  void run();
};
#endif // NISSEFAR_H
