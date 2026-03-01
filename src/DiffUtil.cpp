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

std::string transpose_csv(const std::string &raw) {
  if (raw.empty()) {
    return "";
  }

  auto split_csv_line = [](const std::string &line) -> std::vector<std::string> {
    std::vector<std::string> cells;
    std::string cell;

    for (const char c : line) {
      if (c == ',') {
        cells.push_back(cell);
        cell.clear();
      } else {
        cell += c;
      }
    }

    cells.push_back(cell);
    return cells;
  };

  auto join_csv_line = [](const std::vector<std::string> &cells) -> std::string {
    std::string line;
    for (size_t i = 0; i < cells.size(); ++i) {
      if (i != 0) {
        line += ',';
      }
      line += cells[i];
    }
    return line;
  };

  std::stringstream ss(raw);
  std::string line;
  std::vector<std::vector<std::string>> rows;

  while (std::getline(ss, line)) {
    if (!line.empty()) {
      rows.push_back(split_csv_line(line));
    }
  }

  if (rows.empty()) {
    return "";
  }

  size_t max_columns = 0;
  for (const auto &row : rows) {
    max_columns = std::max(max_columns, row.size());
  }

  for (auto &row : rows) {
    row.resize(max_columns);
  }

  std::vector<std::vector<std::string>> transposed(
      max_columns, std::vector<std::string>(rows.size()));

  for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
    for (size_t col_idx = 0; col_idx < max_columns; ++col_idx) {
      transposed[col_idx][row_idx] = rows[row_idx][col_idx];
    }
  }

  std::string result;
  for (const auto &row : transposed) {
    result += join_csv_line(row);
    result += "\n";
  }

  return result;
}

std::string diff_csv(const std::string &olddata, const std::string &newdata,
                     int sheet_id, bool transpose) {
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

  const std::string normalized_old = transpose ? transpose_csv(olddata) : olddata;
  const std::string normalized_new = transpose ? transpose_csv(newdata) : newdata;

  std::string sorted_old = sort_csv(normalized_old);
  std::string sorted_new = sort_csv(normalized_new);

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
