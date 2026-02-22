#ifndef LLMSERVICE_H
#define LLMSERVICE_H

#include <Config.h>
#include <dpp/dpp.h>
#include <ollama.hpp>
#include <functional>
#include <string>
#include <vector>

class LlmService {
public:
  enum class GenerationType { TextReply, Diff, ImageDescription };

  struct ToolDefinition {
    std::string name;
    std::string description;
    std::string parameters_schema_json;
  };

  LlmService(const Config &config, dpp::cluster &bot);

  std::string generate_text(const std::string &prompt,
                            const ollama::images &imagelist,
                            GenerationType gen_type) const;

  dpp::task<std::string>
  generate_text_with_tools(const std::string &prompt,
                           const ollama::images &imagelist,
                           const std::vector<ToolDefinition> &available_tools,
                           const std::function<dpp::task<std::string>(
                               const std::string &, const std::string &)>
                               &tool_executor) const;

  dpp::task<ollama::images>
  generate_images(std::vector<dpp::attachment> attachments) const;

private:
  const Config &config;
  dpp::cluster &bot;
  mutable Ollama ollama_client;
};

#endif // LLMSERVICE_H
