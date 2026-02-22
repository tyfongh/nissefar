#ifndef DISCORDEVENTSERVICE_H
#define DISCORDEVENTSERVICE_H

#include <Config.h>
#include <Domain.h>
#include <LlmService.h>
#include <dpp/dpp.h>
#include <string>

class GoogleDocsService;
class WebPageService;

class DiscordEventService {
public:
  DiscordEventService(const Config &config, dpp::cluster &bot,
                      const LlmService &llm_service,
                      const GoogleDocsService &google_docs_service,
                      const WebPageService &web_page_service);

  dpp::task<void> handle_message(const dpp::message_create_t &event);
  dpp::task<void> handle_message_update(const dpp::message_update_t &event);
  dpp::task<void> handle_slashcommand(const dpp::slashcommand_t &event);
  dpp::task<void> handle_reaction(const dpp::message_reaction_add_t &event);
  dpp::task<void> remove_reaction(const dpp::message_reaction_remove_t &event);

private:
  std::string format_message_history(dpp::snowflake channel_id) const;
  std::string format_replyto_message(const Message &msg) const;
  void store_message(const Message &message, dpp::guild *server,
                     dpp::channel *channel, const std::string &user_name) const;

  const Config &config;
  dpp::cluster &bot;
  const LlmService &llm_service;
  const GoogleDocsService &google_docs_service;
  const WebPageService &web_page_service;
};

#endif // DISCORDEVENTSERVICE_H
