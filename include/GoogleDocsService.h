#ifndef GOOGLEDOCSSERVICE_H
#define GOOGLEDOCSSERVICE_H

#include <Config.h>
#include <Domain.h>
#include <LlmService.h>
#include <chrono>
#include <dpp/dpp.h>
#include <map>
#include <optional>

class GoogleDocsService {
public:
  GoogleDocsService(const Config &config, dpp::cluster &bot,
                    const LlmService &llm_service);

  std::string format_sheet_context() const;
  std::optional<std::string>
  get_sheet_csv_by_tab_name(const std::string &sheet_name) const;
  dpp::task<void> process_google_docs();

private:
  dpp::task<void> process_sheets(const std::string filename,
                                 const std::string file_id,
                                 std::string weblink);
  void process_diffs();

  const Config &config;
  dpp::cluster &bot;
  const LlmService &llm_service;

  std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>>
      timestamps;
  std::map<std::string, std::map<int, std::string>> sheet_data;
  std::map<std::string, std::map<int, SheetTabMetadata>> sheet_metadata;
  std::map<std::string, std::map<int, Diffdata>> sheet_diffs;
};

#endif // GOOGLEDOCSSERVICE_H
