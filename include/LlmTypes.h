#ifndef LLMTYPES_H
#define LLMTYPES_H

#include <Json.h>

#include <string>
#include <utility>
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

struct LlmArtifact {
  std::string id;
  std::string filename;
  std::string mime_type;
  std::string data;
  std::string description;
};

struct LlmToolResult {
  std::string output;
  std::vector<LlmArtifact> artifacts;
  bool stop_tool_loop{false};

  LlmToolResult() = default;
  LlmToolResult(std::string value) : output(std::move(value)) {}
  LlmToolResult(const char *value) : output(value) {}
};

struct LlmGenerationResult {
  std::string text;
  std::vector<LlmArtifact> artifacts;
};

struct LlmResponse {
  std::string text;
  std::vector<LlmToolCall> tool_calls;
  std::vector<LlmGeneratedImage> generated_images;
  Json output_items = Json::array();
};

#endif // LLMTYPES_H
