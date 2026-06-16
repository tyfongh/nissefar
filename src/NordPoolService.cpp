#include <NordPoolService.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <format>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {

std::string uppercase_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

std::string lowercase_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool is_valid_area(const std::string &area) {
  if (area.empty() || area.size() > 6) {
    return false;
  }
  return std::ranges::all_of(area, [](unsigned char ch) {
    return std::isalnum(ch) || ch == '_';
  });
}

bool is_valid_date(const std::string &date) {
  if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
    return false;
  }
  for (std::size_t i = 0; i < date.size(); ++i) {
    if (i == 4 || i == 7) {
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(date[i]))) {
      return false;
    }
  }
  return true;
}

std::optional<std::chrono::system_clock::time_point>
parse_utc_timestamp(const std::string &value) {
  if (value.size() < 19) {
    return std::nullopt;
  }

  std::tm tm{};
  std::istringstream in(value.substr(0, 19));
  in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (in.fail()) {
    return std::nullopt;
  }

  const std::time_t seconds = timegm(&tm);
  if (seconds == static_cast<std::time_t>(-1)) {
    return std::nullopt;
  }
  return std::chrono::system_clock::from_time_t(seconds);
}

std::string format_utc(std::chrono::system_clock::time_point value) {
  const std::time_t time = std::chrono::system_clock::to_time_t(value);
  std::tm tm{};
  gmtime_r(&time, &tm);
  char buffer[32]{};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

std::string format_oslo(std::chrono::system_clock::time_point value) {
  const auto *zone = std::chrono::locate_zone("Europe/Oslo");
  const std::chrono::zoned_time zoned{zone, value};
  return std::format("{:%Y-%m-%d %H:%M %Z}", zoned);
}

std::optional<std::chrono::system_clock::time_point>
oslo_local_time_to_sys(const std::string &date, const std::string &time) {
  if (!is_valid_date(date) || time.size() < 4) {
    return std::nullopt;
  }

  const int year = std::stoi(date.substr(0, 4));
  const unsigned month = static_cast<unsigned>(std::stoi(date.substr(5, 2)));
  const unsigned day = static_cast<unsigned>(std::stoi(date.substr(8, 2)));

  std::string hhmm = time;
  if (hhmm.size() >= 5) {
    hhmm = hhmm.substr(0, 5);
  }
  if (hhmm.size() != 5 || hhmm[2] != ':' ||
      !std::isdigit(static_cast<unsigned char>(hhmm[0])) ||
      !std::isdigit(static_cast<unsigned char>(hhmm[1])) ||
      !std::isdigit(static_cast<unsigned char>(hhmm[3])) ||
      !std::isdigit(static_cast<unsigned char>(hhmm[4]))) {
    return std::nullopt;
  }

  const int hour = std::stoi(hhmm.substr(0, 2));
  const int minute = std::stoi(hhmm.substr(3, 2));
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return std::nullopt;
  }

  const auto local = std::chrono::local_days{
                         std::chrono::year{year} / std::chrono::month{month} /
                         std::chrono::day{day}} +
                     std::chrono::hours{hour} + std::chrono::minutes{minute};
  const auto *zone = std::chrono::locate_zone("Europe/Oslo");
  return zone->to_sys(local, std::chrono::choose::earliest);
}

std::optional<std::chrono::system_clock::time_point>
target_time_for_request(const NordPoolService::Request &request,
                        std::chrono::system_clock::time_point now) {
  if (!request.time.has_value() || request.time->empty() ||
      lowercase_ascii(*request.time) == "now") {
    return now;
  }
  return oslo_local_time_to_sys(request.date, *request.time);
}

std::vector<NordPoolService::PriceEntry>
entries_for_area(const Json &data, const std::string &area) {
  std::vector<NordPoolService::PriceEntry> entries;
  if (!data.contains("multiAreaEntries") || !data["multiAreaEntries"].is_array()) {
    return entries;
  }

  for (const auto &entry : data["multiAreaEntries"]) {
    if (!entry.contains("deliveryStart") || !entry.contains("deliveryEnd") ||
        !entry.contains("entryPerArea") || !entry["entryPerArea"].contains(area) ||
        !entry["entryPerArea"][area].is_number()) {
      continue;
    }

    const auto start = parse_utc_timestamp(entry["deliveryStart"].get<std::string>());
    const auto end = parse_utc_timestamp(entry["deliveryEnd"].get<std::string>());
    if (!start.has_value() || !end.has_value()) {
      continue;
    }

    entries.push_back({*start, *end, entry["entryPerArea"][area].get<double>()});
  }

  return entries;
}

std::optional<NordPoolService::PriceEntry>
find_entry_at(const std::vector<NordPoolService::PriceEntry> &entries,
              std::chrono::system_clock::time_point target) {
  const auto it = std::ranges::find_if(entries, [&](const auto &entry) {
    return target >= entry.delivery_start && target < entry.delivery_end;
  });
  if (it == entries.end()) {
    return std::nullopt;
  }
  return *it;
}

std::optional<double> area_average_from_payload(const Json &data,
                                                const std::string &area) {
  if (!data.contains("areaAverages") || !data["areaAverages"].is_array()) {
    return std::nullopt;
  }
  for (const auto &item : data["areaAverages"]) {
    if (item.contains("areaCode") && item["areaCode"].is_string() &&
        item["areaCode"].get<std::string>() == area && item.contains("price") &&
        item["price"].is_number()) {
      return item["price"].get<double>();
    }
  }
  return std::nullopt;
}

double round_2(double value) { return std::round(value * 100.0) / 100.0; }

std::string query_escape(const std::string &value) {
  std::ostringstream out;
  out << std::uppercase << std::hex;
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
      out << static_cast<char>(ch);
    } else {
      out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return out.str();
}

Json area_state(const Json &data, const std::string &area) {
  if (!data.contains("areaStates") || !data["areaStates"].is_array()) {
    return nullptr;
  }
  for (const auto &state : data["areaStates"]) {
    if (!state.contains("areas") || !state["areas"].is_array()) {
      continue;
    }
    for (const auto &state_area : state["areas"]) {
      if (state_area.is_string() && state_area.get<std::string>() == area) {
        return state.contains("state") ? state["state"] : Json(nullptr);
      }
    }
  }
  return nullptr;
}

} // namespace

NordPoolService::NordPoolService()
    : client("dataportal-api.nordpoolgroup.com", "nissefar/0.1", 15) {}

NordPoolService::LookupResult NordPoolService::lookup(const Request &request) const {
  Request normalized = request;
  normalized.area = uppercase_ascii(normalized.area);
  normalized.statistic = lowercase_ascii(normalized.statistic.empty() ? "all" : normalized.statistic);

  if (normalized.date == "today") {
    normalized.date = oslo_date(std::chrono::system_clock::now());
  } else if (normalized.date == "yesterday") {
    normalized.date = oslo_date(std::chrono::system_clock::now(), -1);
  }

  if (!is_valid_area(normalized.area)) {
    return {false, "Tool error: invalid or missing area code.", Json::object()};
  }
  if (!is_valid_date(normalized.date)) {
    return {false, "Tool error: date must be today, yesterday, or yyyy-MM-dd.", Json::object()};
  }

  const std::string path =
      std::format("/api/DayAheadPrices?date={}&market=DayAhead&deliveryArea={}&currency=EUR",
                  query_escape(normalized.date), query_escape(normalized.area));
  auto response = client.get(path, {{"Accept", "application/json"}});
  if (!response.ok()) {
    return {false, std::format("Tool error: Nord Pool request failed: {}", response.error), Json::object()};
  }
  if (response.response->status < 200 || response.response->status >= 300) {
    return {false, std::format("Tool error: Nord Pool returned HTTP {}.", response.response->status), Json::object()};
  }
  if (!response.response->json.has_value()) {
    return {false, "Tool error: Nord Pool response did not contain JSON.", Json::object()};
  }

  return lookup_from_json(*response.response->json, normalized,
                          std::chrono::system_clock::now());
}

NordPoolService::LookupResult NordPoolService::lookup_from_json(
    const Json &data, const Request &request,
    std::chrono::system_clock::time_point now) {
  Request normalized = request;
  normalized.area = uppercase_ascii(normalized.area);
  normalized.statistic = lowercase_ascii(normalized.statistic.empty() ? "all" : normalized.statistic);

  const auto entries = entries_for_area(data, normalized.area);
  if (entries.empty()) {
    return {false, std::format("Tool error: no price entries found for area {}.", normalized.area), Json::object()};
  }

  double min_price = std::numeric_limits<double>::infinity();
  double max_price = -std::numeric_limits<double>::infinity();
  double sum = 0.0;
  for (const auto &entry : entries) {
    min_price = std::min(min_price, entry.price);
    max_price = std::max(max_price, entry.price);
    sum += entry.price;
  }

  const double computed_average = sum / static_cast<double>(entries.size());
  const double average_price = area_average_from_payload(data, normalized.area)
                                   .value_or(round_2(computed_average));

  Json payload = Json::object();
  payload["area"] = normalized.area;
  payload["date"] = data.value("deliveryDateCET", normalized.date);
  payload["currency"] = data.value("currency", "EUR");
  payload["unit"] = "EUR/MWh";
  payload["market"] = data.value("market", "DayAhead");
  payload["status"] = area_state(data, normalized.area);
  payload["updated_at"] = data.value("updatedAt", "");
  payload["min_price"] = round_2(min_price);
  payload["max_price"] = round_2(max_price);
  payload["average_price"] = round_2(average_price);
  payload["period_count"] = entries.size();

  if (normalized.statistic == "price" || normalized.statistic == "all") {
    const auto target = target_time_for_request(normalized, now);
    if (!target.has_value()) {
      return {false, "Tool error: time must be HH:mm, now, or omitted.", Json::object()};
    }
    const auto entry = find_entry_at(entries, *target);
    if (!entry.has_value()) {
      return {false, "Tool error: requested time was outside the returned delivery periods.", Json::object()};
    }

    payload["price"] = round_2(entry->price);
    payload["period_start_utc"] = format_utc(entry->delivery_start);
    payload["period_end_utc"] = format_utc(entry->delivery_end);
    payload["period_start_oslo"] = format_oslo(entry->delivery_start);
    payload["period_end_oslo"] = format_oslo(entry->delivery_end);
  }

  if (normalized.statistic == "price" || normalized.statistic == "min" ||
      normalized.statistic == "max" || normalized.statistic == "average" ||
      normalized.statistic == "all") {
    payload["requested_statistic"] = normalized.statistic;
    return {true, {}, std::move(payload)};
  }

  return {false, "Tool error: statistic must be price, min, max, average, or all.", Json::object()};
}

std::string NordPoolService::oslo_date(std::chrono::system_clock::time_point now,
                                       int day_offset) {
  const auto *zone = std::chrono::locate_zone("Europe/Oslo");
  const std::chrono::zoned_time zoned{zone, now + std::chrono::hours{24 * day_offset}};
  return std::format("{:%Y-%m-%d}", zoned);
}
