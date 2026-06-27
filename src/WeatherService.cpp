#include <WeatherService.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <format>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

namespace {

std::string lowercase_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
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

std::string format_local(std::chrono::system_clock::time_point value) {
  const auto *zone = std::chrono::locate_zone("Europe/Oslo");
  const std::chrono::zoned_time zoned{zone, value};
  return std::format("{:%Y-%m-%d %H:%M %Z}", zoned);
}

std::string format_local_date(std::chrono::system_clock::time_point value) {
  const auto *zone = std::chrono::locate_zone("Europe/Oslo");
  const std::chrono::zoned_time zoned{zone, value};
  return std::format("{:%Y-%m-%d}", zoned);
}

std::string resolve_date(const std::string &date,
                         std::chrono::system_clock::time_point now) {
  const std::string lowered = lowercase_ascii(date.empty() ? "today" : date);
  if (lowered == "today") {
    return WeatherService::local_date(now);
  }
  if (lowered == "tomorrow") {
    return WeatherService::local_date(now, 1);
  }
  if (lowered == "yesterday") {
    return WeatherService::local_date(now, -1);
  }
  return date;
}

std::optional<std::chrono::system_clock::time_point>
local_time_to_sys(const std::string &date, const std::string &time) {
  if (!is_valid_date(date)) {
    return std::nullopt;
  }
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
                         std::chrono::year{std::stoi(date.substr(0, 4))} /
                         std::chrono::month{static_cast<unsigned>(std::stoi(date.substr(5, 2)))} /
                         std::chrono::day{static_cast<unsigned>(std::stoi(date.substr(8, 2)))}} +
                     std::chrono::hours{hour} + std::chrono::minutes{minute};
  const auto *zone = std::chrono::locate_zone("Europe/Oslo");
  return zone->to_sys(local, std::chrono::choose::earliest);
}

std::optional<std::chrono::system_clock::time_point>
target_time_for_request(const WeatherService::Request &request,
                        const std::string &date,
                        std::chrono::system_clock::time_point now) {
  if (!request.time.has_value() || request.time->empty()) {
    return std::nullopt;
  }
  const std::string lowered = lowercase_ascii(*request.time);
  if (lowered == "now") {
    return now;
  }
  if (lowered == "morning") {
    return local_time_to_sys(date, "09:00");
  }
  if (lowered == "afternoon") {
    return local_time_to_sys(date, "15:00");
  }
  if (lowered == "evening") {
    return local_time_to_sys(date, "19:00");
  }
  if (lowered == "night") {
    return local_time_to_sys(date, "23:00");
  }
  return local_time_to_sys(date, *request.time);
}

Json period_json(const WeatherService::ForecastPeriod &period) {
  Json item = Json::object();
  item["time_utc"] = format_utc(period.time);
  item["time_local"] = format_local(period.time);
  item["temperature_c"] = period.temperature;
  if (period.precipitation.has_value()) {
    item["precipitation_mm"] = *period.precipitation;
  }
  if (period.wind_speed.has_value()) {
    item["wind_speed_mps"] = *period.wind_speed;
  }
  if (period.humidity.has_value()) {
    item["relative_humidity_percent"] = *period.humidity;
  }
  if (!period.symbol_code.empty()) {
    item["symbol_code"] = period.symbol_code;
  }
  return item;
}

} // namespace

WeatherService::WeatherService(const GeocodingService &geocoding_service)
    : geocoding_service(geocoding_service),
      yr_client("api.met.no", "nissefar/0.1 weather forecast") {}

std::string WeatherService::local_date(std::chrono::system_clock::time_point now,
                                       int day_offset) {
  const auto *zone = std::chrono::locate_zone("Europe/Oslo");
  const std::chrono::zoned_time zoned{zone, now};
  const auto local_day = std::chrono::floor<std::chrono::days>(zoned.get_local_time()) +
                         std::chrono::days{day_offset};
  return std::format("{:%Y-%m-%d}", local_day);
}

std::vector<WeatherService::ForecastPeriod>
WeatherService::parse_periods(const Json &data) {
  std::vector<ForecastPeriod> periods;
  if (!data.contains("properties") || !data["properties"].contains("timeseries") ||
      !data["properties"]["timeseries"].is_array()) {
    return periods;
  }

  for (const auto &item : data["properties"]["timeseries"]) {
    if (!item.contains("time") || !item["time"].is_string() ||
        !item.contains("data") || !item["data"].contains("instant") ||
        !item["data"]["instant"].contains("details")) {
      continue;
    }
    const auto time = parse_utc_timestamp(item["time"].get<std::string>());
    if (!time.has_value()) {
      continue;
    }
    const auto &details = item["data"]["instant"]["details"];
    if (!details.contains("air_temperature") ||
        !details["air_temperature"].is_number()) {
      continue;
    }

    ForecastPeriod period{.time = *time,
                          .temperature = details["air_temperature"].get<double>()};
    if (details.contains("wind_speed") && details["wind_speed"].is_number()) {
      period.wind_speed = details["wind_speed"].get<double>();
    }
    if (details.contains("relative_humidity") &&
        details["relative_humidity"].is_number()) {
      period.humidity = details["relative_humidity"].get<double>();
    }

    for (const std::string key : {"next_1_hours", "next_6_hours", "next_12_hours"}) {
      if (!item["data"].contains(key)) {
        continue;
      }
      const auto &forecast = item["data"][key];
      if (!period.precipitation.has_value() && forecast.contains("details") &&
          forecast["details"].contains("precipitation_amount") &&
          forecast["details"]["precipitation_amount"].is_number()) {
        period.precipitation = forecast["details"]["precipitation_amount"].get<double>();
      }
      if (period.symbol_code.empty() && forecast.contains("summary") &&
          forecast["summary"].contains("symbol_code") &&
          forecast["summary"]["symbol_code"].is_string()) {
        period.symbol_code = forecast["summary"]["symbol_code"].get<std::string>();
      }
      if (period.precipitation.has_value() && !period.symbol_code.empty()) {
        break;
      }
    }

    periods.push_back(std::move(period));
  }

  std::ranges::sort(periods, {}, &ForecastPeriod::time);
  return periods;
}

WeatherService::LookupResult WeatherService::lookup_from_json(
    const Json &data, const Request &request,
    const GeocodingService::Location &location,
    std::chrono::system_clock::time_point now) {
  const std::string date = resolve_date(request.date, now);
  if (!is_valid_date(date)) {
    return {.ok = false,
            .error = "Tool error: date must be today, tomorrow, yesterday, or yyyy-MM-dd."};
  }

  const auto periods = parse_periods(data);
  std::vector<ForecastPeriod> day_periods;
  for (const auto &period : periods) {
    if (format_local_date(period.time) == date) {
      day_periods.push_back(period);
    }
  }
  if (day_periods.empty()) {
    return {.ok = false,
            .error = std::format("Tool error: no forecast periods found for {}.", date)};
  }

  double min_temperature = std::numeric_limits<double>::infinity();
  double max_temperature = -std::numeric_limits<double>::infinity();
  double precipitation_total = 0.0;
  double max_wind = 0.0;
  std::map<std::string, int> symbol_counts;
  for (const auto &period : day_periods) {
    min_temperature = std::min(min_temperature, period.temperature);
    max_temperature = std::max(max_temperature, period.temperature);
    precipitation_total += period.precipitation.value_or(0.0);
    max_wind = std::max(max_wind, period.wind_speed.value_or(0.0));
    if (!period.symbol_code.empty()) {
      ++symbol_counts[period.symbol_code];
    }
  }

  std::string dominant_symbol;
  int dominant_count = 0;
  for (const auto &[symbol, count] : symbol_counts) {
    if (count > dominant_count) {
      dominant_symbol = symbol;
      dominant_count = count;
    }
  }

  Json payload = Json::object();
  payload["source"] = "yr_locationforecast";
  payload["timezone"] = "Europe/Oslo";
  payload["date"] = date;
  payload["location"] = location.display_name;
  payload["latitude"] = location.latitude;
  payload["longitude"] = location.longitude;
  payload["period_count"] = day_periods.size();
  payload["min_temperature_c"] = min_temperature;
  payload["max_temperature_c"] = max_temperature;
  payload["precipitation_total_mm"] = precipitation_total;
  payload["max_wind_speed_mps"] = max_wind;
  if (!dominant_symbol.empty()) {
    payload["dominant_symbol_code"] = dominant_symbol;
  }
  if (data.contains("properties") && data["properties"].contains("meta") &&
      data["properties"]["meta"].contains("updated_at") &&
      data["properties"]["meta"]["updated_at"].is_string()) {
    payload["updated_at"] = data["properties"]["meta"]["updated_at"].get<std::string>();
  }

  const auto target_time = target_time_for_request(request, date, now);
  if (target_time.has_value()) {
    auto closest = std::ranges::min_element(
        day_periods, {}, [&](const ForecastPeriod &period) {
          const auto diff = period.time > *target_time ? period.time - *target_time
                                                       : *target_time - period.time;
          return std::chrono::duration_cast<std::chrono::seconds>(diff).count();
        });
    if (closest != day_periods.end()) {
      payload["selected_period"] = period_json(*closest);
    }
  }

  const std::string detail = lowercase_ascii(request.detail.empty() ? "summary" : request.detail);
  if (detail == "hourly" || detail == "table") {
    payload["periods"] = Json::array();
    for (const auto &period : day_periods) {
      payload["periods"].push_back(period_json(period));
    }
  }

  return {.ok = true, .payload = std::move(payload)};
}

WeatherService::LookupResult WeatherService::lookup(const Request &request) const {
  GeocodingService::Location location;
  if (!request.location.empty()) {
    const auto geocoded = geocoding_service.lookup(request.location);
    if (!geocoded.ok || !geocoded.location.has_value()) {
      return {.ok = false, .error = geocoded.error};
    }
    location = *geocoded.location;
  } else if (request.latitude.has_value() && request.longitude.has_value()) {
    location = {.query = request.location,
                .display_name = request.location.empty() ? "custom coordinates"
                                                        : request.location,
                .latitude = *request.latitude,
                .longitude = *request.longitude};
  } else {
    return {.ok = false, .error = "Tool error: missing required argument 'location'."};
  }

  const std::string path = std::format("/weatherapi/locationforecast/2.0/compact?lat={:.5f}&lon={:.5f}",
                                      location.latitude, location.longitude);
  const auto result = yr_client.get(path, {{"Accept", "application/json"}});
  if (!result.ok()) {
    return {.ok = false,
            .error = std::format("Tool error: weather request failed: {}",
                                 result.error)};
  }
  if (result.response->status != 200) {
    return {.ok = false,
            .error = std::format("Tool error: weather API returned HTTP {}.",
                                 result.response->status)};
  }
  if (!result.response->json.has_value()) {
    return {.ok = false, .error = "Tool error: weather API returned no JSON."};
  }

  return lookup_from_json(*result.response->json, request, location,
                          std::chrono::system_clock::now());
}
