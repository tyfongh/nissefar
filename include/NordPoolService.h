#ifndef NORDPOOLSERVICE_H
#define NORDPOOLSERVICE_H

#include <HttpClient.h>
#include <Json.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

class NordPoolService {
public:
  struct Request {
    std::string area;
    std::string date;
    std::optional<std::string> time;
    std::string statistic{"all"};
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

  NordPoolService();

  LookupResult lookup(const Request &request) const;

  static LookupResult lookup_from_json(const Json &data, const Request &request,
                                       std::chrono::system_clock::time_point now);
  static std::string oslo_date(std::chrono::system_clock::time_point now,
                               int day_offset = 0);

private:
  http_json::Client client;
};

#endif // NORDPOOLSERVICE_H
