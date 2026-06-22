#include <SpotPriceService.h>

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

std::string format_local(std::chrono::system_clock::time_point value,
                         const std::string &timezone) {
  const auto *zone = std::chrono::locate_zone(timezone);
  const std::chrono::zoned_time zoned{zone, value};
  return std::format("{:%Y-%m-%d %H:%M %Z}", zoned);
}

std::optional<std::chrono::system_clock::time_point>
local_time_to_sys(const std::string &date, const std::string &time,
                  const std::string &timezone) {
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
  const auto *zone = std::chrono::locate_zone(timezone);
  return zone->to_sys(local, std::chrono::choose::earliest);
}

std::optional<std::chrono::system_clock::time_point>
target_time_for_request(const SpotPriceService::Request &request,
                        std::chrono::system_clock::time_point now,
                        const std::string &timezone) {
  if (!request.time.has_value() || request.time->empty() ||
      lowercase_ascii(*request.time) == "now") {
    return now;
  }
  return local_time_to_sys(request.date, *request.time, timezone);
}

std::vector<SpotPriceService::PriceEntry>
nord_pool_entries_for_area(const Json &data, const std::string &area) {
  std::vector<SpotPriceService::PriceEntry> entries;
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

std::optional<Json> find_ote_line(const Json &data, const std::string &title) {
  if (!data.contains("data") || !data["data"].contains("dataLine") ||
      !data["data"]["dataLine"].is_array()) {
    return std::nullopt;
  }
  for (const auto &line : data["data"]["dataLine"]) {
    if (line.contains("title") && line["title"].is_string() &&
        line["title"].get<std::string>() == title) {
      return line;
    }
  }
  return std::nullopt;
}

std::vector<SpotPriceService::PriceEntry>
ote_entries(const Json &data, const std::string &date,
            const std::string &time_resolution) {
  std::vector<SpotPriceService::PriceEntry> entries;
  const auto line_title = time_resolution == "PT60M"
                              ? "60min price reference (EUR/MWh)"
                              : "15min price (EUR/MWh)";
  const auto line = find_ote_line(data, line_title);
  if (!line.has_value() || !line->contains("point") || !(*line)["point"].is_array()) {
    return entries;
  }

  const auto step = time_resolution == "PT60M" ? std::chrono::minutes{60}
                                                : std::chrono::minutes{15};
  const auto *zone = std::chrono::locate_zone("Europe/Prague");
  for (const auto &point : (*line)["point"]) {
    if (!point.contains("x") || !point.contains("y") || !point["y"].is_number()) {
      continue;
    }

    int index = 0;
    if (point["x"].is_string()) {
      index = std::stoi(point["x"].get<std::string>());
    } else if (point["x"].is_number_integer()) {
      index = point["x"].get<int>();
    } else {
      continue;
    }
    if (index < 1) {
      continue;
    }

    const auto start_local = std::chrono::local_days{
                                 std::chrono::year{std::stoi(date.substr(0, 4))} /
                                 std::chrono::month{static_cast<unsigned>(std::stoi(date.substr(5, 2)))} /
                                 std::chrono::day{static_cast<unsigned>(std::stoi(date.substr(8, 2)))}} +
                             step * (index - 1);
    const auto start = zone->to_sys(start_local, std::chrono::choose::earliest);
    const auto end = zone->to_sys(start_local + step, std::chrono::choose::earliest);
    entries.push_back({start, end, point["y"].get<double>()});
  }
  return entries;
}

std::vector<SpotPriceService::PriceEntry>
spotovaelektrina_entries(const Json &data, const std::string &date,
                         const std::string &array_name) {
  std::vector<SpotPriceService::PriceEntry> entries;
  if (!data.contains(array_name) || !data[array_name].is_array()) {
    return entries;
  }

  const auto *zone = std::chrono::locate_zone("Europe/Prague");
  const auto day = std::chrono::local_days{
      std::chrono::year{std::stoi(date.substr(0, 4))} /
      std::chrono::month{static_cast<unsigned>(std::stoi(date.substr(5, 2)))} /
      std::chrono::day{static_cast<unsigned>(std::stoi(date.substr(8, 2)))}};

  for (const auto &point : data[array_name]) {
    if (!point.contains("hour") || !point.contains("minute") ||
        !point.contains("priceEur") || !point["hour"].is_number_integer() ||
        !point["minute"].is_number_integer() || !point["priceEur"].is_number()) {
      continue;
    }

    const int hour = point["hour"].get<int>();
    const int minute = point["minute"].get<int>();
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      continue;
    }

    const auto start_local = day + std::chrono::hours{hour} +
                             std::chrono::minutes{minute};
    const auto start = zone->to_sys(start_local, std::chrono::choose::earliest);
    const auto end = zone->to_sys(start_local + std::chrono::minutes{15},
                                  std::chrono::choose::earliest);
    entries.push_back({start, end, point["priceEur"].get<double>()});
  }

  return entries;
}

std::optional<SpotPriceService::PriceEntry>
find_entry_at(const std::vector<SpotPriceService::PriceEntry> &entries,
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

Json build_payload(const SpotPriceService::Request &request,
                   const std::vector<SpotPriceService::PriceEntry> &entries,
                   std::chrono::system_clock::time_point now,
                   const std::string &source, const std::string &market,
                   const std::string &timezone,
                   std::optional<double> average_override = std::nullopt) {
  double min_price = std::numeric_limits<double>::infinity();
  double max_price = -std::numeric_limits<double>::infinity();
  double sum = 0.0;
  for (const auto &entry : entries) {
    min_price = std::min(min_price, entry.price);
    max_price = std::max(max_price, entry.price);
    sum += entry.price;
  }

  const double computed_average = sum / static_cast<double>(entries.size());
  const double average_price = average_override.value_or(round_2(computed_average));

  Json payload = Json::object();
  payload["area"] = request.area;
  payload["source"] = source;
  payload["date"] = request.date;
  payload["currency"] = "EUR";
  payload["unit"] = "EUR/MWh";
  payload["market"] = market;
  payload["timezone"] = timezone;
  payload["min_price"] = round_2(min_price);
  payload["max_price"] = round_2(max_price);
  payload["average_price"] = round_2(average_price);
  payload["period_count"] = entries.size();

  if (request.statistic == "price" || request.statistic == "all") {
    const auto target = target_time_for_request(request, now, timezone);
    if (!target.has_value()) {
      payload["error"] = "Tool error: time must be HH:mm, now, or omitted.";
      return payload;
    }
    const auto entry = find_entry_at(entries, *target);
    if (!entry.has_value()) {
      payload["error"] = "Tool error: requested time was outside the returned delivery periods.";
      return payload;
    }

    payload["price"] = round_2(entry->price);
    payload["period_start_utc"] = format_utc(entry->delivery_start);
    payload["period_end_utc"] = format_utc(entry->delivery_end);
    payload["period_start_local"] = format_local(entry->delivery_start, timezone);
    payload["period_end_local"] = format_local(entry->delivery_end, timezone);
    if (timezone == "Europe/Oslo") {
      payload["period_start_oslo"] = payload["period_start_local"];
      payload["period_end_oslo"] = payload["period_end_local"];
    } else if (timezone == "Europe/Prague") {
      payload["period_start_prague"] = payload["period_start_local"];
      payload["period_end_prague"] = payload["period_end_local"];
    }
  }

  return payload;
}

} // namespace

SpotPriceService::SpotPriceService()
    : nord_pool_client("dataportal-api.nordpoolgroup.com", "nissefar/0.1", 15),
      ote_client("www.ote-cr.cz", "nissefar/0.1", 15),
      spotovaelektrina_client("spotovaelektrina.cz", "nissefar/0.1", 15) {}

SpotPriceService::LookupResult SpotPriceService::lookup(const Request &request) const {
  Request normalized = request;
  normalized.area = uppercase_ascii(normalized.area);
  normalized.statistic = lowercase_ascii(normalized.statistic.empty() ? "all" : normalized.statistic);
  normalized.source = lowercase_ascii(normalized.source.empty() ? "auto" : normalized.source);
  normalized.time_resolution = uppercase_ascii(normalized.time_resolution.empty() ? "PT15M" : normalized.time_resolution);

  if (normalized.source != "auto" && normalized.source != "nordpool" &&
      normalized.source != "nord_pool" && normalized.source != "ote" &&
      normalized.source != "spotovaelektrina" && normalized.source != "cz") {
    return {false, "Tool error: source must be auto, nordpool, ote, spotovaelektrina, or cz.", Json::object()};
  }
  if (normalized.statistic != "price" && normalized.statistic != "min" &&
      normalized.statistic != "max" && normalized.statistic != "average" &&
      normalized.statistic != "all") {
    return {false, "Tool error: statistic must be price, min, max, average, or all.", Json::object()};
  }

  const bool wants_ote = normalized.source == "ote" ||
                         normalized.source == "spotovaelektrina" ||
                         normalized.source == "cz" ||
                         (normalized.source == "auto" &&
                          (normalized.area == "CZ" || normalized.area == "OTE"));
  const std::string timezone = wants_ote ? "Europe/Prague" : "Europe/Oslo";
  const auto now = std::chrono::system_clock::now();

  if (normalized.date == "today") {
    normalized.date = local_date(now, timezone);
  } else if (normalized.date == "tomorrow") {
    normalized.date = local_date(now, timezone, 1);
  } else if (normalized.date == "yesterday") {
    normalized.date = local_date(now, timezone, -1);
  }

  if (!is_valid_area(normalized.area)) {
    return {false, "Tool error: invalid or missing area code.", Json::object()};
  }
  if (!is_valid_date(normalized.date)) {
    return {false, "Tool error: date must be today, tomorrow, yesterday, or yyyy-MM-dd.", Json::object()};
  }

  if (wants_ote) {
    if (normalized.area != "CZ" && normalized.area != "OTE") {
      return {false, "Tool error: OTE only supports area CZ.", Json::object()};
    }
    normalized.area = "CZ";
    if (normalized.time_resolution != "PT15M" && normalized.time_resolution != "PT60M") {
      return {false, "Tool error: OTE time_resolution must be PT15M or PT60M.", Json::object()};
    }

    const bool is_today = normalized.date == local_date(now, "Europe/Prague");
    const bool is_tomorrow = normalized.date == local_date(now, "Europe/Prague", 1);
    if (normalized.time_resolution == "PT15M" && (is_today || is_tomorrow)) {
      auto response = spotovaelektrina_client.get(
          "/api/v1/price/get-prices-json-qh", {{"Accept", "application/json"}});
      if (!response.ok()) {
        return {false, std::format("Tool error: SpotovaElektrina request failed: {}", response.error), Json::object()};
      }
      if (response.response->status < 200 || response.response->status >= 300) {
        return {false, std::format("Tool error: SpotovaElektrina returned HTTP {}.", response.response->status), Json::object()};
      }
      if (!response.response->json.has_value()) {
        return {false, "Tool error: SpotovaElektrina response did not contain JSON.", Json::object()};
      }
      return lookup_spotovaelektrina_from_json(*response.response->json, normalized,
                                               now);
    }

    const std::string path =
        std::format("/pw-data/chart-data/01?report_date={}&time_resolution={}&language=en",
                    query_escape(normalized.date), query_escape(normalized.time_resolution));
    auto response = ote_client.get(path, {{"Accept", "application/json"}});
    if (!response.ok()) {
      return {false, std::format("Tool error: OTE request failed: {}", response.error), Json::object()};
    }
    if (response.response->status < 200 || response.response->status >= 300) {
      return {false, std::format("Tool error: OTE returned HTTP {}.", response.response->status), Json::object()};
    }
    if (!response.response->json.has_value()) {
      return {false, "Tool error: OTE response did not contain JSON.", Json::object()};
    }
    return lookup_ote_from_json(*response.response->json, normalized, now);
  }

  if (normalized.source == "ote") {
    return {false, "Tool error: OTE only supports area CZ.", Json::object()};
  }

  const std::string path =
      std::format("/api/DayAheadPrices?date={}&market=DayAhead&deliveryArea={}&currency=EUR",
                  query_escape(normalized.date), query_escape(normalized.area));
  auto response = nord_pool_client.get(path, {{"Accept", "application/json"}});
  if (!response.ok()) {
    return {false, std::format("Tool error: Nord Pool request failed: {}", response.error), Json::object()};
  }
  if (response.response->status < 200 || response.response->status >= 300) {
    return {false, std::format("Tool error: Nord Pool returned HTTP {}.", response.response->status), Json::object()};
  }
  if (!response.response->json.has_value()) {
    return {false, "Tool error: Nord Pool response did not contain JSON.", Json::object()};
  }

  return lookup_nord_pool_from_json(*response.response->json, normalized, now);
}

SpotPriceService::LookupResult SpotPriceService::lookup_nord_pool_from_json(
    const Json &data, const Request &request,
    std::chrono::system_clock::time_point now) {
  Request normalized = request;
  normalized.area = uppercase_ascii(normalized.area);
  normalized.statistic = lowercase_ascii(normalized.statistic.empty() ? "all" : normalized.statistic);

  const auto entries = nord_pool_entries_for_area(data, normalized.area);
  if (entries.empty()) {
    return {false, std::format("Tool error: no price entries found for area {}.", normalized.area), Json::object()};
  }

  normalized.date = data.value("deliveryDateCET", normalized.date);
  Json payload = build_payload(normalized, entries, now, "nordpool", data.value("market", "DayAhead"),
                               "Europe/Oslo", area_average_from_payload(data, normalized.area));
  if (payload.contains("error")) {
    return {false, payload["error"].get<std::string>(), Json::object()};
  }
  payload["status"] = area_state(data, normalized.area);
  payload["updated_at"] = data.value("updatedAt", "");
  payload["requested_statistic"] = normalized.statistic;
  return {true, {}, std::move(payload)};
}

SpotPriceService::LookupResult SpotPriceService::lookup_ote_from_json(
    const Json &data, const Request &request,
    std::chrono::system_clock::time_point now) {
  Request normalized = request;
  normalized.area = "CZ";
  normalized.statistic = lowercase_ascii(normalized.statistic.empty() ? "all" : normalized.statistic);
  normalized.time_resolution = uppercase_ascii(normalized.time_resolution.empty() ? "PT15M" : normalized.time_resolution);

  if (!is_valid_date(normalized.date)) {
    return {false, "Tool error: date must be today, tomorrow, yesterday, or yyyy-MM-dd.", Json::object()};
  }
  if (normalized.time_resolution != "PT15M" && normalized.time_resolution != "PT60M") {
    return {false, "Tool error: OTE time_resolution must be PT15M or PT60M.", Json::object()};
  }

  const auto entries = ote_entries(data, normalized.date, normalized.time_resolution);
  if (entries.empty()) {
    return {false, "Tool error: no OTE price entries found for CZ.", Json::object()};
  }

  Json payload = build_payload(normalized, entries, now, "ote", "DayAhead", "Europe/Prague");
  if (payload.contains("error")) {
    return {false, payload["error"].get<std::string>(), Json::object()};
  }
  payload["time_resolution"] = normalized.time_resolution;
  payload["requested_statistic"] = normalized.statistic;
  if (data.contains("graph") && data["graph"].contains("title") && data["graph"]["title"].is_string()) {
    payload["title"] = data["graph"]["title"].get<std::string>();
  }
  return {true, {}, std::move(payload)};
}

SpotPriceService::LookupResult SpotPriceService::lookup_spotovaelektrina_from_json(
    const Json &data, const Request &request,
    std::chrono::system_clock::time_point now) {
  Request normalized = request;
  normalized.area = "CZ";
  normalized.statistic = lowercase_ascii(normalized.statistic.empty() ? "all" : normalized.statistic);
  normalized.time_resolution = "PT15M";

  const std::string today = local_date(now, "Europe/Prague");
  const std::string tomorrow = local_date(now, "Europe/Prague", 1);
  std::string array_name;
  if (normalized.date == today) {
    array_name = "hoursToday";
  } else if (normalized.date == tomorrow) {
    array_name = "hoursTomorrow";
  } else {
    return {false, "Tool error: SpotovaElektrina only provides CZ prices for today and tomorrow.", Json::object()};
  }

  const auto entries = spotovaelektrina_entries(data, normalized.date, array_name);
  if (entries.empty()) {
    return {false, "Tool error: no SpotovaElektrina price entries found for CZ.", Json::object()};
  }

  Json payload = build_payload(normalized, entries, now, "spotovaelektrina",
                               "DayAhead", "Europe/Prague");
  if (payload.contains("error")) {
    return {false, payload["error"].get<std::string>(), Json::object()};
  }
  payload["time_resolution"] = "PT15M";
  payload["requested_statistic"] = normalized.statistic;
  payload["provider_url"] = "https://spotovaelektrina.cz/api/v1/price/get-prices-json-qh";
  return {true, {}, std::move(payload)};
}

std::string SpotPriceService::local_date(std::chrono::system_clock::time_point now,
                                         const std::string &timezone,
                                         int day_offset) {
  const auto *zone = std::chrono::locate_zone(timezone);
  const std::chrono::zoned_time zoned{zone, now + std::chrono::hours{24 * day_offset}};
  return std::format("{:%Y-%m-%d}", zoned);
}
