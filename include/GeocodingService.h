#ifndef GEOCODINGSERVICE_H
#define GEOCODINGSERVICE_H

#include <HttpClient.h>
#include <Json.h>

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>

class GeocodingService {
public:
  struct Location {
    std::string query;
    std::string display_name;
    double latitude{};
    double longitude{};
  };

  struct LookupResult {
    bool ok{false};
    std::string error;
    std::optional<Location> location;
  };

  GeocodingService();

  LookupResult lookup(const std::string &query) const;

  static LookupResult lookup_from_json(const Json &data, const std::string &query);
  static std::optional<Location> builtin_location(const std::string &query);

private:
  http_json::Client nominatim_client;
  mutable std::mutex mutex;
  mutable std::map<std::string, Location> cache;
  mutable std::chrono::steady_clock::time_point last_nominatim_request{};
};

#endif // GEOCODINGSERVICE_H
