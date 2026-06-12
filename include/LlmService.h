#ifndef LLMSERVICE_H
#define LLMSERVICE_H

#include <Config.h>
#include <ChatGptAuth.h>
#include <CodexClient.h>
#include <Domain.h>
#include <LlmTypes.h>
#include <Sentiment.h>
#include <dpp/dpp.h>
#include <functional>
#include <string>
#include <vector>

class LlmService {
public:
  enum class GenerationType { TextReply, Diff, ImageDescription, Sentiment };

  struct ToolDefinition {
    std::string name;
    std::string description;
    std::string parameters_schema_json;
  };

  LlmService(const Config &config, dpp::cluster &bot,
             std::shared_ptr<ChatGptAuthManager> auth_manager);

  std::string generate_text(const std::string &prompt,
                            const LlmImages &imagelist,
                            GenerationType gen_type) const;

  std::optional<LlmGeneratedImage>
  generate_image(const std::string &prompt, const LlmImages &imagelist) const;

  std::optional<SentimentEvaluation>
  evaluate_sentiment(const Message &message) const;

  dpp::task<std::string>
  generate_text_with_tools(const std::string &prompt,
                           const LlmImages &imagelist,
                           const std::vector<ToolDefinition> &available_tools,
                           const std::function<dpp::task<std::string>(
                               const std::string &, const std::string &)>
                               &tool_executor) const;

  dpp::task<LlmImages> generate_images(std::vector<dpp::attachment> attachments) const;

private:
  CodexResponseResult
  create_codex_response_with_auth_retry(const CodexRequest &request) const;

  const Config &config;
  dpp::cluster &bot;
  std::shared_ptr<ChatGptAuthManager> auth_manager;
  CodexClient codex_client;
};

#endif // LLMSERVICE_H
