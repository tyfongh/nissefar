#include <GeocodingService.h>
#include <WeatherService.h>

#include <cassert>
#include <chrono>
#include <iostream>

namespace {

Json geocoding_payload() {
  return Json::parse(R"json([
    {
      "place_id": 123,
      "lat": "59.9138688",
      "lon": "10.7522454",
      "display_name": "Oslo, Norway",
      "importance": 0.8
    }
  ])json");
}

Json weather_payload() {
  return Json::parse(R"json({
    "type": "Feature",
    "properties": {
      "meta": {"updated_at": "2026-06-22T08:00:00Z"},
      "timeseries": [
        {
          "time": "2026-06-22T07:00:00Z",
          "data": {
            "instant": {"details": {"air_temperature": 16.0, "wind_speed": 2.0, "relative_humidity": 70.0}},
            "next_1_hours": {"summary": {"symbol_code": "partlycloudy_day"}, "details": {"precipitation_amount": 0.0}}
          }
        },
        {
          "time": "2026-06-23T07:00:00Z",
          "data": {
            "instant": {"details": {"air_temperature": 12.5, "wind_speed": 3.4, "relative_humidity": 82.0}},
            "next_1_hours": {"summary": {"symbol_code": "rain"}, "details": {"precipitation_amount": 1.2}}
          }
        },
        {
          "time": "2026-06-23T13:00:00Z",
          "data": {
            "instant": {"details": {"air_temperature": 18.0, "wind_speed": 5.0, "relative_humidity": 63.0}},
            "next_1_hours": {"summary": {"symbol_code": "cloudy"}, "details": {"precipitation_amount": 0.4}}
          }
        },
        {
          "time": "2026-06-23T17:00:00Z",
          "data": {
            "instant": {"details": {"air_temperature": 15.0, "wind_speed": 4.2, "relative_humidity": 76.0}},
            "next_6_hours": {"summary": {"symbol_code": "rain"}, "details": {"precipitation_amount": 2.0}}
          }
        }
      ]
    }
  })json");
}

std::chrono::system_clock::time_point utc(int year, unsigned month, unsigned day,
                                          int hour, int minute) {
  return std::chrono::sys_days{std::chrono::year{year} /
                               std::chrono::month{month} /
                               std::chrono::day{day}} +
         std::chrono::hours{hour} + std::chrono::minutes{minute};
}

void test_geocoding_parser() {
  const auto result = GeocodingService::lookup_from_json(geocoding_payload(), "Oslo");
  assert(result.ok);
  assert(result.location.has_value());
  assert(result.location->display_name == "Oslo, Norway");
  assert(result.location->latitude > 59.9);
  assert(result.location->longitude > 10.7);
}

void test_builtin_location() {
  const auto location = GeocodingService::builtin_location("OSLO");
  assert(location.has_value());
  assert(location->display_name == "Oslo, Norway");
}

void test_weather_tomorrow_summary() {
  WeatherService::Request request{.location = "Oslo",
                                  .date = "tomorrow",
                                  .detail = "summary"};
  GeocodingService::Location location{"Oslo", "Oslo, Norway", 59.9139, 10.7522};

  const auto result = WeatherService::lookup_from_json(
      weather_payload(), request, location, utc(2026, 6, 22, 10, 0));

  assert(result.ok);
  assert(result.payload["date"] == "2026-06-23");
  assert(result.payload["location"] == "Oslo, Norway");
  assert(result.payload["min_temperature_c"] == 12.5);
  assert(result.payload["max_temperature_c"] == 18.0);
  assert(result.payload["precipitation_total_mm"] == 3.6);
  assert(result.payload["max_wind_speed_mps"] == 5.0);
  assert(result.payload["dominant_symbol_code"] == "rain");
  assert(!result.payload.contains("periods"));
}

void test_weather_selected_time_and_hourly() {
  WeatherService::Request request{.location = "Oslo",
                                  .date = "2026-06-23",
                                  .time = "afternoon",
                                  .detail = "hourly"};
  GeocodingService::Location location{"Oslo", "Oslo, Norway", 59.9139, 10.7522};

  const auto result = WeatherService::lookup_from_json(
      weather_payload(), request, location, utc(2026, 6, 22, 10, 0));

  assert(result.ok);
  assert(result.payload.contains("selected_period"));
  assert(result.payload["selected_period"]["temperature_c"] == 18.0);
  assert(result.payload.contains("periods"));
  assert(result.payload["periods"].is_array());
  assert(result.payload["periods"].size() == 3);
}

void test_unknown_geocoding_result() {
  const auto result = GeocodingService::lookup_from_json(Json::array(), "Nowhere");
  assert(!result.ok);
}

} // namespace

int main() {
  test_geocoding_parser();
  test_builtin_location();
  test_weather_tomorrow_summary();
  test_weather_selected_time_and_hourly();
  test_unknown_geocoding_result();
  std::cout << "WeatherServiceTests passed\n";
  return 0;
}
