#ifndef SENTIMENT_H
#define SENTIMENT_H

#include <optional>
#include <string>
#include <vector>

struct SentimentEvaluation {
  std::string label;
  double score = 0.0;
  std::vector<std::string> tone;
  double confidence = 0.0;
};

namespace sentiment {

std::optional<SentimentEvaluation> parse_evaluation_json(const std::string &text);
std::string to_json_string(const SentimentEvaluation &evaluation);
std::string format_for_log(const SentimentEvaluation &evaluation);
std::string format_for_history(const std::string &sentiment_json);

} // namespace sentiment

#endif // SENTIMENT_H
