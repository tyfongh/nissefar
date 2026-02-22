#ifndef LLMSERVICE_H
#define LLMSERVICE_H

#include <Config.h>
#include <dpp/dpp.h>
#include <ollama.hpp>
#include <string>
#include <vector>

class LlmService {
public:
  enum class GenerationType { TextReply, Diff, ImageDescription };

  LlmService(const Config &config, dpp::cluster &bot);

  std::string generate_text(const std::string &prompt,
                            const ollama::images &imagelist,
                            GenerationType gen_type) const;

  dpp::task<ollama::images>
  generate_images(const std::vector<dpp::attachment> &attachments) const;

private:
  const Config &config;
  dpp::cluster &bot;
  mutable Ollama ollama_client;
};

#endif // LLMSERVICE_H
