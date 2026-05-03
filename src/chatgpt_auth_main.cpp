#include <ChatGptAuth.h>
#include <ChatGptDeviceAuth.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <format>
#include <iostream>
#include <thread>

namespace {

std::atomic_bool g_cancelled = false;

void handle_sigint(int) { g_cancelled = true; }

void sleep_with_cancel(std::chrono::seconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  while (!g_cancelled && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

} // namespace

int main() {
  std::signal(SIGINT, handle_sigint);
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  constexpr auto kPollingSafetyMargin = std::chrono::seconds(3);
  constexpr auto kAuthorizationTimeout = std::chrono::minutes(10);

  std::cout << "Starting ChatGPT device authorization...\n";
  std::cout << "Requesting device code from auth.openai.com...\n";

  const chatgpt_device_auth::Client client;
  const auto start = client.start_device_authorization();
  if (!start.ok()) {
    std::cerr << std::format("Failed to start device authorization: {}\n",
                             start.error);
    return 1;
  }

  const auto &authz = *start.authorization;
  std::cout << "Open this URL in your browser:\n";
  std::cout << chatgpt_device_auth::kDeviceVerificationUrl << "\n\n";
  std::cout << "Enter this code:\n";
  std::cout << authz.user_code << "\n\n";
  std::cout << std::format("Polling every {} seconds. Press Ctrl+C to cancel.\n",
                           authz.interval_seconds);

  const auto deadline = std::chrono::steady_clock::now() + kAuthorizationTimeout;
  int poll_attempt = 0;

  while (!g_cancelled && std::chrono::steady_clock::now() < deadline) {
    ++poll_attempt;
    std::cout << std::format("Polling authorization status (attempt {})...\n",
                             poll_attempt);

    const auto poll = client.poll_device_authorization(authz);
    if (!poll.ok()) {
      std::cerr << std::format("Authorization polling failed: {}\n", poll.error);
      return 1;
    }

    if (poll.result->state == chatgpt_device_auth::DevicePollResult::State::Authorized) {
      const auto token_result = client.exchange_authorization_code(
          poll.result->authorization_code, poll.result->code_verifier);
      if (!token_result.ok()) {
        std::cerr << std::format("Token exchange failed: {}\n",
                                 token_result.error);
        return 1;
      }

      const auto &tokens = *token_result.tokens;
       ChatGptAuth auth{.type = "oauth",
                        .refresh = tokens.refresh_token,
                        .access = tokens.access_token,
                        .expires = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count() +
                                   static_cast<std::int64_t>(tokens.expires_in),
                        .account_id = chatgpt_device_auth::extract_account_id(tokens)};

      const std::string write_error = ChatGptAuthStore::write(auth);
      if (!write_error.empty()) {
        std::cerr << std::format("Failed to persist auth: {}\n", write_error);
        return 1;
      }

      const std::string path = ChatGptAuthStore::default_path();
      const auto verify = ChatGptAuthStore::load();
      if (!verify.ok()) {
        std::cerr << std::format("Auth was written but could not be reloaded: {}\n",
                                 verify.error);
        return 1;
      }

      std::cout << std::format("Authentication saved to {}\n", path);
      return 0;
    }

    sleep_with_cancel(std::chrono::seconds(authz.interval_seconds) +
                      kPollingSafetyMargin);
  }

  if (g_cancelled) {
    std::cerr << "Login cancelled.\n";
    return 1;
  }

  std::cerr << "Authorization timed out.\n";
  return 1;
}
