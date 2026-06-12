#include <Sentiment.h>

#include <Json.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <sstream>

namespace {

bool valid_label(const std::string &label) {
  static constexpr std::array labels{"positive", "neutral", "negative", "mixed",
                                     "unclear"};
  return std::ranges::find(labels, label) != labels.end();
}

double clamp(double value, double low, double high) {
  if (!std::isfinite(value)) {
    return 0.0;
  }
  return std::max(low, std::min(value, high));
}

std::string join_tone(const std::vector<std::string> &tone) {
  std::string result;
  for (const auto &tag : tone) {
    if (!result.empty()) {
      result += ",";
    }
    result += tag;
  }
  return result;
}

std::optional<Json> parse_json_object(const std::string &text) {
  try {
    Json parsed = Json::parse(text);
    if (parsed.is_object()) {
      return parsed;
    }
  } catch (...) {
  }

  const auto start = text.find('{');
  const auto end = text.rfind('}');
  if (start == std::string::npos || end == std::string::npos || end <= start) {
    return std::nullopt;
  }

  try {
    Json parsed = Json::parse(text.substr(start, end - start + 1));
    if (parsed.is_object()) {
      return parsed;
    }
  } catch (...) {
  }

  return std::nullopt;
}

} // namespace

namespace sentiment {

std::optional<SentimentEvaluation> parse_evaluation_json(const std::string &text) {
  const auto parsed = parse_json_object(text);
  if (!parsed.has_value()) {
    return std::nullopt;
  }

  const Json &json = *parsed;
  if (!json.contains("label") || !json["label"].is_string() ||
      !json.contains("score") || !json["score"].is_number() ||
      !json.contains("confidence") || !json["confidence"].is_number()) {
    return std::nullopt;
  }

  SentimentEvaluation evaluation;
  evaluation.label = json["label"].get<std::string>();
  if (!valid_label(evaluation.label)) {
    return std::nullopt;
  }

  evaluation.score = clamp(json["score"].get<double>(), -1.0, 1.0);
  evaluation.confidence = clamp(json["confidence"].get<double>(), 0.0, 1.0);

  if (json.contains("tone") && json["tone"].is_array()) {
    for (const auto &tag : json["tone"]) {
      if (tag.is_string()) {
        evaluation.tone.push_back(tag.get<std::string>());
      }
      if (evaluation.tone.size() >= 5) {
        break;
      }
    }
  }

  return evaluation;
}

std::string to_json_string(const SentimentEvaluation &evaluation) {
  return Json{{"label", evaluation.label},
              {"score", evaluation.score},
              {"tone", evaluation.tone},
              {"confidence", evaluation.confidence}}
      .dump();
}

std::string format_for_log(const SentimentEvaluation &evaluation) {
  return std::format("label={} score={:.2f} confidence={:.2f} tone={}",
                     evaluation.label, evaluation.score, evaluation.confidence,
                     join_tone(evaluation.tone));
}

std::string format_for_history(const std::string &sentiment_json) {
  const auto parsed = parse_json_object(sentiment_json);
  if (!parsed.has_value()) {
    return "";
  }

  const Json &json = *parsed;
  if (!json.contains("label") || !json["label"].is_string()) {
    return "";
  }

  std::ostringstream out;
  out << "Sentiment: " << json["label"].get<std::string>();
  if (json.contains("score") && json["score"].is_number()) {
    out << std::format(", score={:.2f}", json["score"].get<double>());
  }
  if (json.contains("confidence") && json["confidence"].is_number()) {
    out << std::format(", confidence={:.2f}",
                       json["confidence"].get<double>());
  }
  if (json.contains("tone") && json["tone"].is_array()) {
    std::vector<std::string> tone;
    for (const auto &tag : json["tone"]) {
      if (tag.is_string()) {
        tone.push_back(tag.get<std::string>());
      }
    }
    if (!tone.empty()) {
      out << ", tone=" << join_tone(tone);
    }
  }
  return out.str();
}

} // namespace sentiment
