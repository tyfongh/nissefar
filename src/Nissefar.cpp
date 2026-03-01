#include <Database.h>
#include <DiscordEventService.h>
#include <CalculationService.h>
#include <GoogleDocsService.h>
#include <LlmService.h>
#include <Nissefar.h>
#include <VideoSummaryService.h>
#include <WebPageService.h>
#include <YoutubeService.h>
#include <dpp/misc-enum.h>
#include <stdexcept>

Nissefar::Nissefar() {
  if (!config.is_valid)
    throw std::runtime_error("Configuration is invalid");

  bot = std::make_unique<dpp::cluster>(
      config.discord_token, dpp::i_default_intents | dpp::i_message_content);

  bot->on_log(dpp::utility::cout_logger());

  // Allow for some minutes of LLM generation

  bot->log(dpp::ll_info,
           std::format("Ollama server url: {}", config.ollama_server_url));
  bot->log(dpp::ll_info,
           std::format("LLM context size: {}", config.context_size));

  llm_service = std::make_unique<LlmService>(config, *bot);
  google_docs_service =
      std::make_unique<GoogleDocsService>(config, *bot, *llm_service);
  youtube_service =
      std::make_unique<YoutubeService>(config, *bot, *llm_service);
  web_page_service = std::make_unique<WebPageService>(*bot);
  video_summary_service =
      std::make_unique<VideoSummaryService>(config, *bot);
  calculation_service = std::make_unique<CalculationService>(*bot);
  discord_event_service = std::make_unique<DiscordEventService>(
      config, *bot, *llm_service, *google_docs_service, *web_page_service,
      *youtube_service, *video_summary_service, *calculation_service);

  bot->log(dpp::ll_info, "Bot initialized");
}

Nissefar::~Nissefar() = default;

dpp::task<void> Nissefar::setup_slashcommands() {
  if (dpp::run_once<struct register_bot_commands>()) {
    bot->global_bulk_command_delete();
    dpp::slashcommand pingcommand("ping", "Ping the nisse", bot->me.id);
    dpp::slashcommand chanstats("chanstats", "Show stats for the channel",
                                bot->me.id);
    chanstats.add_option(dpp::command_option(dpp::co_channel, "channel",
                                             "Stats from this channel", false));

    bot->global_bulk_command_create({pingcommand, chanstats});
    bot->log(dpp::ll_info, "Slashcommands setup");
  }
  co_return;
}

void Nissefar::run() {

  auto &db = Database::instance();
  if (db.initialize(config.db_connection_string))
    std::cout << "Connected to db" << std::endl;
  else
    std::cout << "Failed to connect to db" << std::endl;

  bot->on_message_create(
      [this](const dpp::message_create_t &event) -> dpp::task<void> {
        co_return co_await discord_event_service->handle_message(event);
      });

  bot->on_message_update(
      [this](const dpp::message_update_t &event) -> dpp::task<void> {
        co_return co_await discord_event_service->handle_message_update(event);
      });

  bot->on_message_reaction_add(
      [this](const dpp::message_reaction_add_t &event) -> dpp::task<void> {
        co_return co_await discord_event_service->handle_reaction(event);
      });

  bot->on_message_reaction_remove(
      [this](const dpp::message_reaction_remove_t &event) -> dpp::task<void> {
        co_return co_await discord_event_service->remove_reaction(event);
      });

  bot->log(dpp::ll_info, "Initial process of sheets");
  bot->on_ready([this](const dpp::ready_t &event) -> dpp::task<void> {
    // Only run slashcommands setup when changing things
    // co_await setup_slashcommands();
    co_await youtube_service->process(true);
    co_await google_docs_service->process_google_docs();
    co_return;
  });

  bot->on_slashcommand(
      [this](const dpp::slashcommand_t &event) -> dpp::task<void> {
        co_return co_await discord_event_service->handle_slashcommand(event);
      });

  bot->log(dpp::ll_info, "Starting directory timer, 300 seconds");
  bot->start_timer(
      [this](const dpp::timer &timer) -> dpp::task<void> {
        co_return co_await google_docs_service->process_google_docs();
      },
      300);

  bot->log(dpp::ll_info, "Starting youtube timer, 1500 seconds");
  bot->start_timer(
      [this](const dpp::timer &timer) -> dpp::task<void> {
        co_return co_await youtube_service->process(false);
      },
      1500);

  bot->log(dpp::ll_info, "Starting bot..");
  bot->start(dpp::st_wait);
}
