#include <Database.h>
#include <DbOps.h>
#include <DiscordEventService.h>
#include <CalculationService.h>
#include <ChatGptAuth.h>
#include <GoogleDocsService.h>
#include <LlmService.h>
#include <Nissefar.h>
#include <NordPoolService.h>
#include <VideoSummaryService.h>
#include <WebPageService.h>
#include <YoutubeService.h>
#include <dpp/misc-enum.h>
#include <chrono>
#include <stdexcept>

namespace {

std::int64_t current_unix_time() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

Nissefar::Nissefar() {
  if (!config.is_valid) {
    throw std::runtime_error(config.validation_error.empty()
                                 ? "Configuration is invalid"
                                 : config.validation_error);
  }

  auth_manager = std::make_shared<ChatGptAuthManager>();
  const auto auth_result = auth_manager->load();
  if (!auth_result.ok()) {
    throw std::runtime_error(auth_result.error.empty()
                                 ? "ChatGPT auth is invalid"
                                 : auth_result.error);
  }

  bot = std::make_unique<dpp::cluster>(
      config.discord_token, dpp::i_default_intents | dpp::i_message_content);

  bot->on_log(dpp::utility::cout_logger());

  // Allow for some minutes of LLM generation

  bot->log(dpp::ll_info,
           std::format("ChatGPT model: {}", config.chatgpt_model));
  bot->log(dpp::ll_info,
           std::format("ChatGPT auth file: {}", auth_result.path));
  if (auth_result.auth.has_value()) {
    const auto now = current_unix_time();
    const auto expires_in = auth_result.auth->expires - now;
    bot->log(dpp::ll_info,
             std::format("ChatGPT auth expiry: unix={} expires_in={}s refresh_needed={}",
                         auth_result.auth->expires, expires_in,
                         ChatGptAuthManager::is_expired(*auth_result.auth, now)
                             ? "yes"
                             : "no"));
    if (auth_result.auth->account_id.has_value()) {
      bot->log(dpp::ll_info,
               std::format("ChatGPT account id present: {}",
                           *auth_result.auth->account_id));
    } else {
      bot->log(dpp::ll_info, "ChatGPT account id present: no");
    }
  }
  bot->log(dpp::ll_info,
           std::format("LLM context size: {}", config.context_size));

  llm_service = std::make_unique<LlmService>(config, *bot, auth_manager);
  google_docs_service =
      std::make_unique<GoogleDocsService>(config, *bot, *llm_service);
  youtube_service =
      std::make_unique<YoutubeService>(config, *bot, *llm_service);
  web_page_service = std::make_unique<WebPageService>(*bot);
  video_summary_service =
      std::make_unique<VideoSummaryService>(config, *bot);
  calculation_service = std::make_unique<CalculationService>(*bot);
  nord_pool_service = std::make_unique<NordPoolService>();
  discord_event_service = std::make_unique<DiscordEventService>(
      config, *bot, *llm_service, *google_docs_service, *web_page_service,
      *youtube_service, *video_summary_service, *calculation_service,
      *nord_pool_service);

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

    dpp::slashcommand announce("announce", "Send a message to a channel",
                               bot->me.id);
    announce.add_option(dpp::command_option(dpp::co_channel, "channel",
                                            "Target channel", true));
    announce.add_option(
        dpp::command_option(dpp::co_string, "message", "Message to send", true));

    bot->global_bulk_command_create({pingcommand, chanstats, announce});
    bot->log(dpp::ll_info, "Slashcommands setup");
  }
  co_return;
}

void Nissefar::run() {

  auto &db = Database::instance();
  if (db.initialize(config.db_connection_string)) {
    std::cout << "Connected to db" << std::endl;
    dbops::ensure_message_sentiment_columns();
  } else {
    std::cout << "Failed to connect to db" << std::endl;
  }

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
