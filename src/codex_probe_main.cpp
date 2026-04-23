#include <ChatGptAuth.h>
#include <CodexClient.h>

#include <curl/curl.h>

#include <cstdlib>
#include <format>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct ToolDefinition {
  std::string name;
  std::string description;
  std::string parameters_schema_json;
};

struct Options {
  std::string prompt;
  std::string model = "gpt-5.4";
  std::string system_prompt = "You are a helpful assistant.";
  std::string auth_path = ChatGptAuthStore::default_path();
  std::string transport = "curl";
  bool tools = false;
  bool stream = true;
  int max_iterations = 4;
  std::unordered_map<std::string, std::string> tool_responses;
};

struct CurlResponseBuffer {
  std::string body;
  std::string content_type;
  long status{0};
};

std::string truncate_for_log(std::string value, std::size_t max_size) {
  if (value.size() <= max_size) {
    return value;
  }

  value.resize(max_size);
  value += "...";
  return value;
}

bool parse_bool_flag(const std::string &value, bool &out) {
  if (value == "true" || value == "1" || value == "yes") {
    out = true;
    return true;
  }
  if (value == "false" || value == "0" || value == "no") {
    out = false;
    return true;
  }
  return false;
}

void print_usage(const char *argv0) {
  std::cerr
      << "Usage: " << argv0 << " --prompt <text> [options]\n"
      << "Options:\n"
      << "  --model <name>                 Model name (default: gpt-5.4)\n"
      << "  --system-prompt <text>         Instructions/system prompt\n"
      << "  --auth-path <path>             ChatGPT auth JSON path\n"
      << "  --transport <curl|httplib>     HTTP transport (default: curl)\n"
      << "  --tools                        Enable bot-like tool loop\n"
      << "  --stream <true|false>          Toggle streaming request mode\n"
      << "  --max-iterations <n>           Tool loop cap (default: 4)\n"
      << "  --tool-response <name=text>    Override canned tool output\n";
}

std::optional<Options> parse_args(int argc, char **argv) {
  Options options;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    auto require_value = [&](const std::string &flag) -> std::optional<std::string> {
      if (i + 1 >= argc) {
        std::cerr << std::format("Missing value for {}\n", flag);
        return std::nullopt;
      }
      return std::string(argv[++i]);
    };

    if (arg == "--prompt") {
      const auto value = require_value(arg);
      if (!value.has_value()) {
        return std::nullopt;
      }
      options.prompt = *value;
      continue;
    }

    if (arg == "--model") {
      const auto value = require_value(arg);
      if (!value.has_value()) {
        return std::nullopt;
      }
      options.model = *value;
      continue;
    }

    if (arg == "--system-prompt") {
      const auto value = require_value(arg);
      if (!value.has_value()) {
        return std::nullopt;
      }
      options.system_prompt = *value;
      continue;
    }

    if (arg == "--auth-path") {
      const auto value = require_value(arg);
      if (!value.has_value()) {
        return std::nullopt;
      }
      options.auth_path = *value;
      continue;
    }

    if (arg == "--transport") {
      const auto value = require_value(arg);
      if (!value.has_value()) {
        return std::nullopt;
      }
      if (*value != "curl" && *value != "httplib") {
        std::cerr << std::format("Invalid value for --transport: {}\n", *value);
        return std::nullopt;
      }
      options.transport = *value;
      continue;
    }

    if (arg == "--stream") {
      const auto value = require_value(arg);
      if (!value.has_value()) {
        return std::nullopt;
      }
      if (!parse_bool_flag(*value, options.stream)) {
        std::cerr << std::format("Invalid boolean for --stream: {}\n", *value);
        return std::nullopt;
      }
      continue;
    }

    if (arg == "--tools") {
      options.tools = true;
      continue;
    }

    if (arg == "--max-iterations") {
      const auto value = require_value(arg);
      if (!value.has_value()) {
        return std::nullopt;
      }
      try {
        options.max_iterations = std::stoi(*value);
      } catch (...) {
        std::cerr << std::format("Invalid integer for --max-iterations: {}\n", *value);
        return std::nullopt;
      }
      continue;
    }

    if (arg == "--tool-response") {
      const auto value = require_value(arg);
      if (!value.has_value()) {
        return std::nullopt;
      }
      const auto split = value->find('=');
      if (split == std::string::npos || split == 0) {
        std::cerr << std::format("Expected name=text for --tool-response, got {}\n",
                                 *value);
        return std::nullopt;
      }
      options.tool_responses.emplace(value->substr(0, split),
                                     value->substr(split + 1));
      continue;
    }

    std::cerr << std::format("Unknown argument: {}\n", arg);
    return std::nullopt;
  }

  if (options.prompt.empty()) {
    std::cerr << "--prompt is required\n";
    return std::nullopt;
  }

  if (options.auth_path.empty()) {
    std::cerr << "Could not resolve auth path. Use --auth-path explicitly.\n";
    return std::nullopt;
  }

  return options;
}

Json tool_schema_from_definitions(const std::vector<ToolDefinition> &available_tools) {
  Json tools = Json::array();
  for (const auto &tool : available_tools) {
    Json parameters = Json{{"type", "object"}, {"properties", Json::object()}};
    if (!tool.parameters_schema_json.empty()) {
      try {
        parameters = Json::parse(tool.parameters_schema_json);
      } catch (...) {
      }
    }

    tools.push_back(Json{{"type", "function"},
                         {"name", tool.name},
                         {"description", tool.description},
                         {"parameters", std::move(parameters)}});
  }
  return tools;
}

size_t curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *buffer = static_cast<CurlResponseBuffer *>(userdata);
  const auto bytes = size * nmemb;
  buffer->body.append(ptr, bytes);
  return bytes;
}

size_t curl_header_callback(char *buffer, size_t size, size_t nmemb, void *userdata) {
  auto *response = static_cast<CurlResponseBuffer *>(userdata);
  const std::string_view header(buffer, size * nmemb);
  constexpr std::string_view prefix = "content-type:";

  if (header.size() >= prefix.size()) {
    std::string lowercase;
    lowercase.reserve(header.size());
    for (const char ch : header) {
      lowercase.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lowercase.starts_with(prefix)) {
      std::string value = std::string(header.substr(prefix.size()));
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
      }
      while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
      }
      response->content_type = value;
    }
  }

  return size * nmemb;
}

CodexResponseResult create_response_with_curl(const ChatGptAuth &auth,
                                              const CodexRequest &request) {
  static const bool curl_initialized = [] {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
  }();
  (void)curl_initialized;

  CURL *curl = curl_easy_init();
  if (!curl) {
    return {std::nullopt, "Failed to initialize libcurl."};
  }

  CurlResponseBuffer response_buffer;
  const std::string request_body = CodexClient::build_request_json(request).dump();
  struct curl_slist *headers = nullptr;
  const std::string auth_header = std::format("Authorization: Bearer {}", auth.access);
  const std::string account_header = auth.account_id.has_value() && !auth.account_id->empty()
                                         ? std::format("ChatGPT-Account-Id: {}", *auth.account_id)
                                         : std::string();
  const std::string accept_header = request.stream ? "Accept: text/event-stream"
                                                   : "Accept: application/json";

  headers = curl_slist_append(headers, auth_header.c_str());
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, accept_header.c_str());
  headers = curl_slist_append(headers, "originator: nissefar");
  headers = curl_slist_append(headers, "session_id: codex-probe");
  if (!account_header.empty()) {
    headers = curl_slist_append(headers, account_header.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, "https://chatgpt.com/backend-api/codex/responses");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(request_body.size()));
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_buffer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "codex_probe/0.1");
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  const CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return {std::nullopt, std::format("libcurl request failed: {}",
                                      curl_easy_strerror(code))};
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_buffer.status);
  if (response_buffer.content_type.empty()) {
    char *content_type = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type) == CURLE_OK &&
        content_type) {
      response_buffer.content_type = content_type;
    }
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  CodexResponseResult result;
  result.status = static_cast<int>(response_buffer.status);
  result.content_type = response_buffer.content_type;
  result.raw_body = response_buffer.body;

  if (response_buffer.status < 200 || response_buffer.status >= 300) {
    result.error = std::format("Codex request failed: HTTP {} body={}",
                               response_buffer.status,
                               truncate_for_log(response_buffer.body, 1200));
    return result;
  }

  const bool looks_like_sse = response_buffer.content_type.find("text/event-stream") !=
                                  std::string::npos ||
                              response_buffer.body.starts_with("event:") ||
                              response_buffer.body.starts_with("data:");
  if (looks_like_sse) {
    result = CodexClient::parse_stream_body(response_buffer.body);
  } else {
    try {
      result = CodexClient::parse_response_json(Json::parse(response_buffer.body));
    } catch (const std::exception &e) {
      result = {std::nullopt,
                std::format("Failed to parse JSON response: {}", e.what())};
    }
  }

  result.status = static_cast<int>(response_buffer.status);
  result.content_type = response_buffer.content_type;
  result.raw_body = response_buffer.body;
  return result;
}

std::vector<ToolDefinition> bot_tool_definitions() {
  return {
      {"get_banana_data", "Get EV trunk size dataset from Banana sheet", ""},
      {"get_weight_data", "Get EV vehicle weight dataset from Weight sheet", ""},
      {"get_acceleration_data", "Get EV acceleration dataset from Acceleration sheet", ""},
      {"get_noise_data", "Get EV vehicle noise dataset from Noise sheet", ""},
      {"get_range_data", "Get EV 90 and 120 km/h range and efficiency data from Range sheet", ""},
      {"get_1000km_data", "Get EV 1000 km challenge dataset", ""},
      {"get_charging_curve_data", "Get EV charging power by SoC from Charging curve sheet as transposed CSV", ""},
      {"get_youtube_stream_status", "Check whether the tracked YouTube stream is currently live. If live, returns the current stream title.", ""},
      {"get_webpage_text",
       "Fetch and extract readable text from a public webpage. Use this when the user asks to summarize or answer questions about a URL.",
       R"({"type":"object","properties":{"url":{"type":"string","description":"Absolute http/https URL to fetch"}},"required":["url"]})"},
      {"summarize_video",
       "Summarize a public online video URL by transcribing audio and producing a concise summary.",
       R"({"type":"object","properties":{"url":{"type":"string","description":"Absolute http/https video URL to summarize"}},"required":["url"]})"},
      {"query_channel_analytics",
       "Run generic channel/server analytics. scope: channel or server. kind: leaderboard or time_series. target: reactions or messages. group_by: leaderboard => emoji, message, reactor, recipient, author. time_series => day, week, month. filters.emojis: array of emoji tokens like 🤡, :copium:, <:1Head:123>. time_range: all_time, last_7d, last_30d, this_month, last_month.",
       R"({"type":"object","properties":{"scope":{"type":"string","enum":["channel","server"]},"kind":{"type":"string","enum":["leaderboard","time_series"]},"target":{"type":"string","enum":["reactions","messages"]},"group_by":{"type":"string","enum":["emoji","message","reactor","recipient","author","day","week","month"]},"time_range":{"type":"string","enum":["all_time","last_7d","last_30d","this_month","last_month"]},"filters":{"type":"object","properties":{"emojis":{"type":"array","items":{"type":"string"}}}},"limit":{"type":"integer","minimum":1,"maximum":120}},"required":["kind","target","group_by"]})"},
      {"calculate_with_bc",
       "Evaluate a mathematical expression using bc -l for accurate calculations. Supports arithmetic and bc math functions like sqrt(x), l(x), e(x), s(x), c(x), a(x), j(n,x).",
       R"({"type":"object","properties":{"expression":{"type":"string","description":"Mathematical expression to evaluate"},"scale":{"type":"integer","description":"Optional decimal precision (0-100). Defaults to 10."}},"required":["expression"]})"}};
}

std::string default_tool_output(const std::string &tool_name) {
  if (tool_name == "get_youtube_stream_status") {
    return R"({"is_live":false})";
  }
  if (tool_name == "query_channel_analytics") {
    return R"({"rows":[{"label":"🤡","value":42}],"summary":"sample analytics result"})";
  }
  if (tool_name == "calculate_with_bc") {
    return "3.1415926535";
  }
  if (tool_name == "get_webpage_text") {
    return "Example webpage text returned by codex_probe.";
  }
  if (tool_name == "summarize_video") {
    return "Example video summary returned by codex_probe.";
  }
  return std::format("Stubbed tool output for {}", tool_name);
}

void print_response_details(const CodexResponseResult &result) {
  std::cout << std::format("HTTP status: {}\n", result.status);
  std::cout << std::format("Content-Type: {}\n", result.content_type);
  if (!result.raw_body.empty()) {
    std::cout << std::format("Raw body preview: {}\n",
                             truncate_for_log(result.raw_body, 1200));
  }
  if (!result.error.empty()) {
    std::cout << std::format("Error: {}\n", result.error);
  }
  if (result.response.has_value()) {
    std::cout << std::format("Parsed text bytes: {}\n", result.response->text.size());
    std::cout << std::format("Parsed tool calls: {}\n",
                             result.response->tool_calls.size());
    if (!result.response->text.empty()) {
      std::cout << std::format("Parsed text: {}\n", result.response->text);
    }
    if (result.response->output_items.is_array() && !result.response->output_items.empty()) {
      std::cout << std::format("Output items: {}\n",
                               truncate_for_log(result.response->output_items.dump(), 1200));
    }
  }
}

int run_plain_probe(const Options &options, const ChatGptAuth &auth) {
  CodexClient client("codex_probe/0.1");
  const CodexRequest request{.model = options.model,
                             .instructions = options.system_prompt,
                             .messages = {LlmMessage{.role = "user",
                                                     .content = options.prompt,
                                                     .images = {}}},
                             .stream = options.stream};

  std::cout << "Request JSON:\n" << CodexClient::build_request_json(request).dump(2)
            << "\n";

  const auto result = options.transport == "curl"
                          ? create_response_with_curl(auth, request)
                          : client.create_response(auth, request);
  print_response_details(result);
  return result.ok() ? 0 : 1;
}

int run_tool_probe(const Options &options, const ChatGptAuth &auth) {
  CodexClient client("codex_probe/0.1");
  const auto tool_definitions = bot_tool_definitions();
  const Json json_tools = tool_schema_from_definitions(tool_definitions);
  const std::vector<LlmMessage> initial_messages = {
      LlmMessage{.role = "user", .content = options.prompt, .images = {}}};
  Json accumulated_items = Json::array();
  std::unordered_set<std::string> seen_tool_calls;

  for (int iteration = 0; iteration < options.max_iterations; ++iteration) {
    const CodexRequest request{.model = options.model,
                               .instructions = options.system_prompt,
                               .messages = initial_messages,
                               .input_items = accumulated_items,
                               .tools = json_tools,
                               .stream = options.stream};

    std::cout << std::format("\n=== Iteration {} Request JSON ===\n{}\n", iteration + 1,
                             CodexClient::build_request_json(request).dump(2));

    const auto result = options.transport == "curl"
                            ? create_response_with_curl(auth, request)
                            : client.create_response(auth, request);
    print_response_details(result);
    if (!result.ok()) {
      return 1;
    }

    const auto &response = *result.response;
    if (response.output_items.is_array()) {
      for (const auto &item : response.output_items) {
        accumulated_items.push_back(item);
      }
    }

    if (response.tool_calls.empty()) {
      std::cout << "\nFinal answer:\n" << response.text << "\n";
      return 0;
    }

    for (const auto &tool_call : response.tool_calls) {
      const std::string arguments_json = tool_call.arguments.dump();
      std::cout << std::format("Executing tool: {} call_id={} args={}\n",
                               tool_call.name, tool_call.call_id, arguments_json);

      if (tool_call.call_id.empty()) {
        std::cerr << std::format("Tool call '{}' was missing call_id\n",
                                 tool_call.name);
        return 1;
      }

      const std::string tool_key = tool_call.name + "\n" + arguments_json;
      std::string tool_output;
      if (seen_tool_calls.contains(tool_key)) {
        tool_output =
            "Tool error: duplicate tool call blocked in same request. Use the prior result.";
      } else {
        seen_tool_calls.insert(tool_key);
        if (const auto it = options.tool_responses.find(tool_call.name);
            it != options.tool_responses.end()) {
          tool_output = it->second;
        } else {
          tool_output = default_tool_output(tool_call.name);
        }
      }

      std::cout << std::format("Tool output: {}\n",
                               truncate_for_log(tool_output, 600));
      accumulated_items.push_back(Json{{"type", "function_call_output"},
                                       {"call_id", tool_call.call_id},
                                       {"output", tool_output}});
    }
  }

  std::cerr << std::format("Tool-calling did not finish within {} iterations\n",
                           options.max_iterations);
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  const auto options = parse_args(argc, argv);
  if (!options.has_value()) {
    print_usage(argv[0]);
    return 1;
  }

  ChatGptAuthManager auth_manager(options->auth_path);
  const auto auth_result = auth_manager.ensure_valid();
  if (!auth_result.ok()) {
    std::cerr << std::format("Auth failure: {}\n", auth_result.error);
    return 1;
  }

  std::cout << std::format("Auth path: {}\n", auth_result.path);
  std::cout << std::format("Refreshed auth: {}\n",
                           auth_result.refreshed ? "yes" : "no");
  std::cout << std::format("Model: {}\n", options->model);
  std::cout << std::format("Transport: {}\n", options->transport);
  std::cout << std::format("Tools enabled: {}\n", options->tools ? "yes" : "no");
  std::cout << std::format("Stream enabled: {}\n", options->stream ? "yes" : "no");

  if (options->tools) {
    return run_tool_probe(*options, *auth_result.auth);
  }

  return run_plain_probe(*options, *auth_result.auth);
}
