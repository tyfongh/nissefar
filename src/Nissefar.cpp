#include <Database.h>
#include <DiscordEventService.h>
#include <GoogleDocsService.h>
#include <LlmService.h>
#include <Nissefar.h>
#include <YoutubeService.h>
#include <stdexcept>

Nissefar::Nissefar() {
  if (!config.is_valid)
    throw std::runtime_error("Configuration is invalid");

  bot = std::make_unique<dpp::cluster>(
      config.discord_token, dpp::i_default_intents | dpp::i_message_content);

  bot->on_log(dpp::utility::cout_logger());

  // Allow for some minutes of LLM generation

  ollama::setReadTimeout(360);
  ollama::setWriteTimeout(360);

  llm_service = std::make_unique<LlmService>(config, *bot);
  google_docs_service =
      std::make_unique<GoogleDocsService>(config, *bot, *llm_service);
  discord_event_service = std::make_unique<DiscordEventService>(
      config, *bot, *llm_service, *google_docs_service);
  youtube_service = std::make_unique<YoutubeService>(config, *bot, *llm_service);

  bot->log(dpp::ll_info, "Bot initialized");
}

Nissefar::~Nissefar() = default;

std::string Nissefar::format_sheet_context() {
  return google_docs_service->format_sheet_context();
}

dpp::task<void> Nissefar::handle_message(const dpp::message_create_t &event) {
  co_return co_await discord_event_service->handle_message(event);
}

dpp::task<void>
Nissefar::handle_message_update(const dpp::message_update_t &event) {
  co_return co_await discord_event_service->handle_message_update(event);
}

dpp::task<void> Nissefar::process_google_docs() {
  co_return co_await google_docs_service->process_google_docs();
}

dpp::task<void> Nissefar::process_youtube(bool first_run) {
  co_return co_await youtube_service->process(first_run);
}

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

dpp::task<void>
Nissefar::handle_slashcommand(const dpp::slashcommand_t &event) {
  co_return co_await discord_event_service->handle_slashcommand(event);
}

dpp::task<void>
Nissefar::remove_reaction(const dpp::message_reaction_remove_t &event) {
  co_return co_await discord_event_service->remove_reaction(event);
}

dpp::task<void>
Nissefar::handle_reaction(const dpp::message_reaction_add_t &event) {
  co_return co_await discord_event_service->handle_reaction(event);
}

void Nissefar::run() {

  auto &db = Database::instance();
  if (db.initialize(config.db_connection_string))
    std::cout << "Connected to db" << std::endl;
  else
    std::cout << "Failed to connect to db" << std::endl;

  bot->on_message_create(
      [this](const dpp::message_create_t &event) -> dpp::task<void> {
        co_return co_await handle_message(event);
      });

  bot->on_message_update(
      [this](const dpp::message_update_t &event) -> dpp::task<void> {
        co_return co_await handle_message_update(event);
      });

  bot->on_message_reaction_add(
      [this](const dpp::message_reaction_add_t &event) -> dpp::task<void> {
        co_return co_await handle_reaction(event);
      });

  bot->on_message_reaction_remove(
      [this](const dpp::message_reaction_remove_t &event) -> dpp::task<void> {
        co_return co_await remove_reaction(event);
      });

  bot->log(dpp::ll_info, "Initial process of sheets");
  bot->on_ready([this](const dpp::ready_t &event) -> dpp::task<void> {
    // Only run slashcommands setup when changing things
    // co_await setup_slashcommands();
    co_await process_youtube(true);
    co_await process_google_docs();
    co_return;
  });

  bot->on_slashcommand(
      [this](const dpp::slashcommand_t &event) -> dpp::task<void> {
        co_return co_await handle_slashcommand(event);
      });

  bot->log(dpp::ll_info, "Starting directory timer, 300 seconds");
  bot->start_timer(
      [this](const dpp::timer &timer) -> dpp::task<void> {
        co_return co_await process_google_docs();
      },
      300);

  bot->log(dpp::ll_info, "Starting youtube timer, 1500 seconds");
  bot->start_timer(
      [this](const dpp::timer &timer) -> dpp::task<void> {
        co_return co_await process_youtube(false);
      },
      1500);

  bot->log(dpp::ll_info, "Starting bot..");
  bot->start(dpp::st_wait);
}
