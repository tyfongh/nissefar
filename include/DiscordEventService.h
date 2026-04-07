#ifndef DISCORDEVENTSERVICE_H
#define DISCORDEVENTSERVICE_H

#include <Config.h>
#include <Domain.h>
#include <LlmService.h>
#include <dpp/dpp.h>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class GoogleDocsService;
class WebPageService;
class YoutubeService;
class VideoSummaryService;
class CalculationService;

class DiscordEventService {
public:
  DiscordEventService(const Config &config, dpp::cluster &bot,
                      const LlmService &llm_service,
                      const GoogleDocsService &google_docs_service,
                      const WebPageService &web_page_service,
                      const YoutubeService &youtube_service,
                      const VideoSummaryService &video_summary_service,
                      const CalculationService &calculation_service);

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
  dpp::task<void> handle_carlbot_video(const dpp::message_create_t &event);
  dpp::task<void> run_summary_queue(dpp::snowflake channel_id);

  const Config &config;
  dpp::cluster &bot;
  const LlmService &llm_service;
  const GoogleDocsService &google_docs_service;
  const WebPageService &web_page_service;
  const YoutubeService &youtube_service;
  const VideoSummaryService &video_summary_service;
  const CalculationService &calculation_service;
  bool is_rate_limited(dpp::snowflake user_id) const;

  mutable std::mutex heavy_tool_mutex;
  mutable std::mutex rate_limit_mutex;
  std::mutex summary_queue_mutex;
  std::deque<std::string> summary_queue;
  bool summary_loop_running = false;
  mutable std::unordered_map<dpp::snowflake,
                             std::vector<std::chrono::steady_clock::time_point>>
      rate_limit_map;
};

#endif // DISCORDEVENTSERVICE_H
