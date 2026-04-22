#ifndef LLMTYPES_H
#define LLMTYPES_H

#include <Json.h>

#include <string>
#include <vector>

using LlmImage = std::string;
using LlmImages = std::vector<LlmImage>;

struct LlmMessage {
  std::string role;
  std::string content;
  LlmImages images;
};

struct LlmToolCall {
  std::string name;
  Json arguments;
};

struct LlmResponse {
  std::string text;
  std::vector<LlmToolCall> tool_calls;
};

#endif // LLMTYPES_H
