#include <NordPoolService.h>

#include <cassert>
#include <chrono>
#include <iostream>

namespace {

Json sample_payload() {
  return Json::parse(R"({
    "deliveryDateCET": "2026-06-16",
    "updatedAt": "2026-06-15T11:32:26Z",
    "deliveryAreas": ["NO2"],
    "market": "DayAhead",
    "multiAreaEntries": [
      {"deliveryStart":"2026-06-15T22:00:00Z","deliveryEnd":"2026-06-15T22:15:00Z","entryPerArea":{"NO2":100.0}},
      {"deliveryStart":"2026-06-15T22:15:00Z","deliveryEnd":"2026-06-15T22:30:00Z","entryPerArea":{"NO2":120.0}},
      {"deliveryStart":"2026-06-15T22:30:00Z","deliveryEnd":"2026-06-15T22:45:00Z","entryPerArea":{"NO2":80.0}}
    ],
    "currency": "EUR",
    "areaStates": [{"state":"Final","areas":["NO2"]}],
    "areaAverages": [{"areaCode":"NO2","price":101.23}]
  })");
}

std::chrono::system_clock::time_point utc(int year, unsigned month, unsigned day,
                                          int hour, int minute) {
  return std::chrono::sys_days{std::chrono::year{year} /
                               std::chrono::month{month} /
                               std::chrono::day{day}} +
         std::chrono::hours{hour} + std::chrono::minutes{minute};
}

void test_price_lookup_by_oslo_time() {
  NordPoolService::Request request{.area = "no2",
                                   .date = "2026-06-16",
                                   .time = "00:20",
                                   .statistic = "all"};

  const auto result = NordPoolService::lookup_from_json(
      sample_payload(), request, utc(2026, 6, 15, 22, 10));

  assert(result.ok);
  assert(result.payload["area"] == "NO2");
  assert(result.payload["price"] == 120.0);
  assert(result.payload["min_price"] == 80.0);
  assert(result.payload["max_price"] == 120.0);
  assert(result.payload["average_price"] == 101.23);
  assert(result.payload["currency"] == "EUR");
  assert(result.payload["unit"] == "EUR/MWh");
  assert(result.payload["status"] == "Final");
}

void test_average_without_time() {
  NordPoolService::Request request{.area = "NO2",
                                   .date = "2026-06-16",
                                   .time = std::nullopt,
                                   .statistic = "average"};

  const auto result = NordPoolService::lookup_from_json(
      sample_payload(), request, utc(2026, 6, 15, 22, 10));

  assert(result.ok);
  assert(!result.payload.contains("price"));
  assert(result.payload["average_price"] == 101.23);
}

void test_now_uses_supplied_clock() {
  NordPoolService::Request request{.area = "NO2",
                                   .date = "2026-06-16",
                                   .time = "now",
                                   .statistic = "price"};

  const auto result = NordPoolService::lookup_from_json(
      sample_payload(), request, utc(2026, 6, 15, 22, 35));

  assert(result.ok);
  assert(result.payload["price"] == 80.0);
}

} // namespace

int main() {
  test_price_lookup_by_oslo_time();
  test_average_without_time();
  test_now_uses_supplied_clock();
  std::cout << "NordPoolService tests passed\n";
  return 0;
}
