#include <DiffUtil.h>
#include <GoogleDocsService.h>

#include <set>
#include <sstream>

GoogleDocsService::GoogleDocsService(const Config &config, dpp::cluster &bot,
                                     const LlmService &llm_service)
    : config(config), bot(bot), llm_service(llm_service) {}

std::string GoogleDocsService::format_sheet_context() const {
  std::string context{};
  static const std::set<std::string> include_tabs = {"Range", "1000 km"};

  if (sheet_data.empty()) {
    return context;
  }

  context = "Google Sheets context:";

  for (const auto &[filename, tabs] : sheet_data) {
    if (filename == "Charging curves") {
      continue;
    }

    if (tabs.empty()) {
      continue;
    }

    for (const auto &[sheet_id, csv_data] : tabs) {
      std::string sheet_name = "Unknown";
      std::string header = "";

      auto file_meta = sheet_metadata.find(filename);
      if (file_meta != sheet_metadata.end()) {
        auto tab_meta = file_meta->second.find(sheet_id);
        if (tab_meta != file_meta->second.end()) {
          sheet_name = tab_meta->second.sheet_name;
          header = tab_meta->second.header;
        }
      }

      if (!include_tabs.contains(sheet_name)) {
        continue;
      }

      context += std::format("\n----------------------\n"
                             "File: {}\n"
                             "Tab: {} (gid: {})\n"
                             "Header: {}\n"
                             "CSV data:\n{}",
                             filename, sheet_name, sheet_id, header, csv_data);
    }
  }

  context += "\n----------------------\n";
  return context;
}

std::optional<std::string>
GoogleDocsService::get_sheet_csv_by_tab_name(const std::string &sheet_name) const {
  for (const auto &[filename, tab_metadata] : sheet_metadata) {
    auto data_by_tab = sheet_data.find(filename);
    if (data_by_tab == sheet_data.end()) {
      continue;
    }

    for (const auto &[sheet_id, metadata] : tab_metadata) {
      if (metadata.sheet_name != sheet_name) {
        continue;
      }

      auto csv_data = data_by_tab->second.find(sheet_id);
      if (csv_data != data_by_tab->second.end() && !csv_data->second.empty()) {
        return csv_data->second;
      }
    }
  }

  return std::nullopt;
}

dpp::task<void> GoogleDocsService::process_sheets(const std::string filename,
                                                  const std::string file_id,
                                                  std::string weblink) {
  bot.log(dpp::ll_info, std::format("Processing file {}", filename));

  std::string file_url =
      std::format("https://sheets.googleapis.com/v4/spreadsheets/"
                  "{}?key={}&fields=sheets.properties(sheetId,title)",
                  file_id, config.google_api_key);

  auto file_resp = co_await bot.co_request(file_url, dpp::m_get);

  if (file_resp.status != 200) {
    bot.log(dpp::ll_error, std::format("Error fetching sheets for file {}: {}",
                                       filename, file_resp.status));
    co_return;
  }

  auto file_data = nlohmann::json::parse(file_resp.body.data());

  for (auto sheet : file_data["sheets"]) {
    int sheet_id = sheet["properties"]["sheetId"].get<int>();
    std::string sheet_name = sheet["properties"]["title"].get<std::string>();

    if (filename == "Charging curves") {
      if (sheet_name == "Graph") {
        continue;
      }

      if (sheet_name != "Charging curve") {
        continue;
      }
    }

    std::string sheet_url =
        std::format("https://docs.google.com/spreadsheets/d/{}/"
                    "export?format=csv&gid={}",
                    file_id, sheet_id);

    bool is_done = false;
    int redirect_count = 0;
    constexpr int max_redirects = 10;

    while (!is_done) {
      auto sheet_resp = co_await bot.co_request(sheet_url, dpp::m_get);

      if (sheet_resp.status == 307) {
        if (++redirect_count > max_redirects) {
          bot.log(dpp::ll_warning,
                  std::format("Too many redirects for sheet {}", sheet_id));
          is_done = true;
        } else {
          sheet_url = sheet_resp.headers.find("location")->second;
        }
      } else if (sheet_resp.status == 200) {

        auto newdata = std::format("{}\n", sheet_resp.body.data());

        std::istringstream nds(newdata);
        std::string header{};
        std::getline(nds, header);
        sheet_metadata[filename][sheet_id] = SheetTabMetadata{sheet_name, header};

        if (sheet_data[filename][sheet_id].empty()) {
          sheet_data[filename][sheet_id] = newdata;
        } else {
          if (sheet_data[filename][sheet_id] != newdata) {
            bot.log(dpp::ll_info,
                    std::format("The sheet \"{}\" has changed", sheet_name));

            const bool transpose_for_diff =
                filename == "Charging curves" && sheet_name == "Charging curve";

            sheet_diffs[filename][sheet_id] =
                Diffdata{diff_csv(sheet_data[filename][sheet_id], newdata,
                                  sheet_id, transpose_for_diff),
                         weblink, header, sheet_name};
            sheet_data[filename][sheet_id] = newdata;
          }
        }
        is_done = true;
      } else {
        bot.log(dpp::ll_info,
                std::format("Error: unknown response: {}", sheet_resp.status));
        is_done = true;
      }
    }
  }
  co_return;
}

void GoogleDocsService::process_diffs() {
  for (auto &[filename, diffmap] : sheet_diffs) {
    for (auto &[sheet_id, diffdata] : diffmap) {
      auto prompt = std::format(
          "Filename: {}\nSheet name: {}\nCSV Header: {}\nDiff:\n{}", filename,
          diffdata.sheet_name, diffdata.header, diffdata.diffdata);
      auto answer = llm_service.generate_text(
          prompt, ollama::images{}, LlmService::GenerationType::Diff);
      answer += std::format("\n{}", diffdata.weblink);
      dpp::message msg(1267731118895927347, answer);
      bot.message_create(msg);
    }
  }
  sheet_diffs.clear();
}

dpp::task<void> GoogleDocsService::process_google_docs() {
  bot.log(dpp::ll_info, "Processing directory");

  auto response = co_await bot.co_request(config.directory_url, dpp::m_get);

  auto directory_data = nlohmann::json::parse(response.body.data());

  for (auto filedata : directory_data["files"]) {
    std::string datestring = filedata["modifiedTime"].get<std::string>();
    std::string filename = filedata["name"].get<std::string>();
    if (filename != "TB test results" && filename != "Charging curves")
      continue;
    const std::string file_id = filedata["id"].get<std::string>();
    const std::string weblink = filedata["webViewLink"].get<std::string>();

    std::chrono::sys_time<std::chrono::milliseconds> tp;

    std::istringstream ds(datestring);

    ds >> std::chrono::parse("%Y-%m-%dT%H:%M:%S%Z", tp);

    if (ds.fail()) {
      bot.log(dpp::ll_info,
              std::format("Error parsing timestamp: {}", ds.str()));
    } else {
      const std::string ntime = std::format("{:%Y-%m-%d %H:%M:%S %Z}", tp);
      if (timestamps[filename].time_since_epoch() ==
          std::chrono::milliseconds(0)) {
        bot.log(dpp::ll_info, std::format("New entry: {}, {}", filename, ntime));
        timestamps[filename] = tp;
        co_await process_sheets(filename, file_id, weblink);
      } else {
        if (timestamps[filename] != tp) {
          const std::string otime =
              std::format("{:%Y-%m-%d %H:%M:%S %Z}", timestamps[filename]);
          timestamps[filename] = tp;
          bot.log(
              dpp::ll_info,
              std::format("File {} has changed.\nOld time: {}, New time: {}",
                          filename, otime, ntime));
          co_await process_sheets(filename, file_id, weblink);
          process_diffs();
        }
      }
    }
  }
  co_return;
}
