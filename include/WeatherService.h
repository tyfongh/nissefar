#ifndef WEATHERSERVICE_H
#define WEATHERSERVICE_H

#include <GeocodingService.h>
#include <HttpClient.h>
#include <Json.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

class WeatherService {
public:
  struct Request {
    std::string location;
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::string date{"today"};
    std::optional<std::string> time;
    std::string detail{"summary"};
  };

  struct ForecastPeriod {
    std::chrono::system_clock::time_point time;
    double temperature{};
    std::optional<double> precipitation;
    std::optional<double> wind_speed;
    std::optional<double> humidity;
    std::string symbol_code;
  };

  struct LookupResult {
    bool ok{false};
    std::string error;
    Json payload;
  };

  explicit WeatherService(const GeocodingService &geocoding_service);

  LookupResult lookup(const Request &request) const;

  static LookupResult lookup_from_json(
      const Json &data, const Request &request,
      const GeocodingService::Location &location,
      std::chrono::system_clock::time_point now);
  static std::vector<ForecastPeriod> parse_periods(const Json &data);
  static std::string local_date(std::chrono::system_clock::time_point now,
                                int day_offset = 0);

private:
  const GeocodingService &geocoding_service;
  http_json::Client yr_client;
};

#endif // WEATHERSERVICE_H
