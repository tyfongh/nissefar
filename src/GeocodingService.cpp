#include <GeocodingService.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <iomanip>
#include <sstream>
#include <thread>

namespace {

std::string lowercase_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string trim_whitespace(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string normalize_query(const std::string &query) {
  return lowercase_ascii(trim_whitespace(query));
}

std::string url_encode(std::string_view value) {
  std::ostringstream out;
  out << std::uppercase << std::hex;
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out << static_cast<char>(ch);
    } else if (ch == ' ') {
      out << '+';
    } else {
      out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return out.str();
}

std::optional<double> parse_double_string(const Json &value) {
  if (value.is_number()) {
    return value.get<double>();
  }
  if (!value.is_string()) {
    return std::nullopt;
  }
  const std::string text = value.get<std::string>();
  double parsed{};
  const auto *begin = text.data();
  const auto *end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

} // namespace

GeocodingService::GeocodingService()
    : nominatim_client("nominatim.openstreetmap.org",
                       "nissefar/0.1 weather geocoder") {}

std::optional<GeocodingService::Location>
GeocodingService::builtin_location(const std::string &query) {
  const std::string normalized = normalize_query(query);
  static const std::map<std::string, Location> locations = {
      {"oslo", {"oslo", "Oslo, Norway", 59.9139, 10.7522}},
      {"bergen", {"bergen", "Bergen, Norway", 60.39299, 5.32415}},
      {"trondheim", {"trondheim", "Trondheim, Norway", 63.43049, 10.39506}},
      {"stavanger", {"stavanger", "Stavanger, Norway", 58.96998, 5.73311}},
      {"prague", {"prague", "Prague, Czechia", 50.07554, 14.43780}},
      {"praha", {"praha", "Prague, Czechia", 50.07554, 14.43780}},
      {"copenhagen", {"copenhagen", "Copenhagen, Denmark", 55.67610, 12.56834}},
      {"stockholm", {"stockholm", "Stockholm, Sweden", 59.32932, 18.06858}},
      {"helsinki", {"helsinki", "Helsinki, Finland", 60.16986, 24.93838}}};

  if (const auto it = locations.find(normalized); it != locations.end()) {
    return it->second;
  }
  return std::nullopt;
}

GeocodingService::LookupResult
GeocodingService::lookup_from_json(const Json &data, const std::string &query) {
  if (!data.is_array() || data.empty()) {
    return {.ok = false,
            .error = std::format("Tool error: location '{}' was not found.", query)};
  }

  const auto &first = data.front();
  if (!first.contains("lat") || !first.contains("lon")) {
    return {.ok = false,
            .error = "Tool error: geocoding response did not contain coordinates."};
  }

  const auto latitude = parse_double_string(first["lat"]);
  const auto longitude = parse_double_string(first["lon"]);
  if (!latitude.has_value() || !longitude.has_value()) {
    return {.ok = false,
            .error = "Tool error: geocoding response contained invalid coordinates."};
  }

  Location location{.query = query,
                    .display_name = first.value("display_name", query),
                    .latitude = *latitude,
                    .longitude = *longitude};
  return {.ok = true, .location = std::move(location)};
}

GeocodingService::LookupResult GeocodingService::lookup(const std::string &query) const {
  const std::string normalized = normalize_query(query);
  if (normalized.empty()) {
    return {.ok = false, .error = "Tool error: missing required argument 'location'."};
  }

  if (auto builtin = builtin_location(normalized); builtin.has_value()) {
    return {.ok = true, .location = *builtin};
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    if (const auto it = cache.find(normalized); it != cache.end()) {
      return {.ok = true, .location = it->second};
    }
  }

  {
    std::unique_lock<std::mutex> lock(mutex);
    if (const auto it = cache.find(normalized); it != cache.end()) {
      return {.ok = true, .location = it->second};
    }
    const auto now = std::chrono::steady_clock::now();
    constexpr auto minimum_interval = std::chrono::seconds{1};
    if (last_nominatim_request.time_since_epoch().count() != 0 &&
        now - last_nominatim_request < minimum_interval) {
      const auto sleep_for = minimum_interval - (now - last_nominatim_request);
      lock.unlock();
      std::this_thread::sleep_for(sleep_for);
      lock.lock();
    }
    last_nominatim_request = std::chrono::steady_clock::now();
  }

  const std::string path = std::format(
      "/search?q={}&format=jsonv2&limit=1&addressdetails=1", url_encode(normalized));
  const auto result = nominatim_client.get(path, {{"Accept", "application/json"}});
  if (!result.ok()) {
    return {.ok = false,
            .error = std::format("Tool error: geocoding request failed: {}",
                                 result.error)};
  }
  if (result.response->status != 200) {
    return {.ok = false,
            .error = std::format("Tool error: geocoding returned HTTP {}.",
                                 result.response->status)};
  }
  if (!result.response->json.has_value()) {
    return {.ok = false, .error = "Tool error: geocoding returned no JSON."};
  }

  auto parsed = lookup_from_json(*result.response->json, query);
  if (parsed.ok && parsed.location.has_value()) {
    std::lock_guard<std::mutex> lock(mutex);
    cache[normalized] = *parsed.location;
  }
  return parsed;
}
