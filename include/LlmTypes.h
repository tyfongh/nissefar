#ifndef LLMTYPES_H
#define LLMTYPES_H

#include <Json.h>

#include <string>
#include <vector>

struct LlmImage {
  std::string mime_type;
  std::string base64_data;
};

using LlmImages = std::vector<LlmImage>;

struct LlmMessage {
  std::string role;
  std::string content;
  LlmImages images;
};

struct LlmToolCall {
  std::string call_id;
  std::string name;
  Json arguments;
};

struct LlmGeneratedImage {
  std::string id;
  std::string mime_type;
  std::string base64_data;
  std::string revised_prompt;
};

struct LlmResponse {
  std::string text;
  std::vector<LlmToolCall> tool_calls;
  std::vector<LlmGeneratedImage> generated_images;
  Json output_items = Json::array();
};

#endif // LLMTYPES_H
