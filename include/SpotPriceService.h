#ifndef SPOTPRICESERVICE_H
#define SPOTPRICESERVICE_H

#include <HttpClient.h>
#include <Json.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

class SpotPriceService {
public:
  struct Request {
    std::string area;
    std::string date;
    std::optional<std::string> time;
    std::string statistic{"all"};
    std::string source{"auto"};
    std::string time_resolution{"PT15M"};
  };

  struct PriceEntry {
    std::chrono::system_clock::time_point delivery_start;
    std::chrono::system_clock::time_point delivery_end;
    double price{};
  };

  struct LookupResult {
    bool ok{false};
    std::string error;
    Json payload;
  };

  SpotPriceService();

  LookupResult lookup(const Request &request) const;

  static LookupResult lookup_nord_pool_from_json(
      const Json &data, const Request &request,
      std::chrono::system_clock::time_point now);
  static LookupResult lookup_ote_from_json(
      const Json &data, const Request &request,
      std::chrono::system_clock::time_point now);
  static LookupResult lookup_spotovaelektrina_from_json(
      const Json &data, const Request &request,
      std::chrono::system_clock::time_point now);
  static std::string local_date(std::chrono::system_clock::time_point now,
                                const std::string &timezone,
                                int day_offset = 0);

private:
  http_json::Client nord_pool_client;
  http_json::Client ote_client;
  http_json::Client spotovaelektrina_client;
};

#endif // SPOTPRICESERVICE_H
