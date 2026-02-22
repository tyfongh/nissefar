#include <DiffUtil.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

std::string diff_csv(const std::string &olddata, const std::string &newdata,
                     int sheet_id) {
  auto sort_csv = [](const std::string &raw) -> std::string {
    if (raw.empty())
      return "";

    std::stringstream ss(raw);
    std::string line, header, result;
    std::vector<std::string> rows;

    if (std::getline(ss, header)) {
      result += header + "\n";
    }
    while (std::getline(ss, line)) {
      if (!line.empty()) {
        rows.push_back(line);
      }
    }

    std::ranges::sort(rows);

    for (const auto &row : rows) {
      result += row + "\n";
    }
    return result;
  };

  std::string sorted_old = sort_csv(olddata);
  std::string sorted_new = sort_csv(newdata);

  std::array<char, 128> buffer;
  std::string result;

  std::string tempfiledir = std::filesystem::temp_directory_path();
  std::string oldtempfile = std::format("{}/nisseold{}", tempfiledir, sheet_id);
  std::string newtempfile = std::format("{}/nissenew{}", tempfiledir, sheet_id);

  {
    std::ofstream oldfile(oldtempfile);
    if (oldfile.is_open()) {
      oldfile << sorted_old;
    }

    std::ofstream newfile(newtempfile);
    if (newfile.is_open()) {
      newfile << sorted_new;
    }
  }

  std::unique_ptr<FILE, void (*)(FILE *)> pipe(
      popen(std::format("diff -u {} {}", oldtempfile, newtempfile).c_str(),
            "r"),
      [](FILE *f) -> void { std::ignore = pclose(f); });

  if (!pipe) {
    std::filesystem::remove(oldtempfile);
    std::filesystem::remove(newtempfile);
    return std::string("Kunne ikke kjøre diff pipe");
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) !=
         nullptr)
    result += buffer.data();

  std::filesystem::remove(oldtempfile);
  std::filesystem::remove(newtempfile);

  return result;
}
