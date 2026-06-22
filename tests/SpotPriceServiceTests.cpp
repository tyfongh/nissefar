#include <SpotPriceService.h>

#include <cassert>
#include <chrono>
#include <iostream>

namespace {

Json nord_pool_payload() {
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

Json ote_payload() {
  return Json::parse(R"json({
    "data": {
      "dataLine": [
        {"title":"Volume (MWh)","point":[{"x":"1","y":1000.0}]},
        {"title":"15min price (EUR/MWh)","point":[
          {"x":"1","y":154.13},
          {"x":"2","y":148.62},
          {"x":"3","y":143.64},
          {"x":"4","y":138.90}
        ]},
        {"title":"60min price reference (EUR/MWh)","point":[
          {"x":"1","y":146.32}
        ]}
      ]
    },
    "graph": {"title":"Day-Ahead Market Results - 23.06.2026"}
  })json");
}

Json spotovaelektrina_payload() {
  return Json::parse(R"json({
    "hoursToday": [
      {"hour":0,"minute":0,"priceEur":100.0,"priceCZK":2420,"level":"low"},
      {"hour":0,"minute":15,"priceEur":120.0,"priceCZK":2904,"level":"medium"},
      {"hour":0,"minute":30,"priceEur":80.0,"priceCZK":1936,"level":"low"}
    ],
    "hoursTomorrow": [
      {"hour":0,"minute":0,"priceEur":154.13,"priceCZK":3730,"level":"low"},
      {"hour":0,"minute":15,"priceEur":148.62,"priceCZK":3597,"level":"low"},
      {"hour":0,"minute":30,"priceEur":143.64,"priceCZK":3477,"level":"low"}
    ]
  })json");
}

std::chrono::system_clock::time_point utc(int year, unsigned month, unsigned day,
                                          int hour, int minute) {
  return std::chrono::sys_days{std::chrono::year{year} /
                               std::chrono::month{month} /
                               std::chrono::day{day}} +
         std::chrono::hours{hour} + std::chrono::minutes{minute};
}

void test_nord_pool_price_lookup_by_oslo_time() {
  SpotPriceService::Request request{.area = "no2",
                                    .date = "2026-06-16",
                                    .time = "00:20",
                                    .statistic = "all"};

  const auto result = SpotPriceService::lookup_nord_pool_from_json(
      nord_pool_payload(), request, utc(2026, 6, 15, 22, 10));

  assert(result.ok);
  assert(result.payload["area"] == "NO2");
  assert(result.payload["source"] == "nordpool");
  assert(result.payload["price"] == 120.0);
  assert(result.payload["min_price"] == 80.0);
  assert(result.payload["max_price"] == 120.0);
  assert(result.payload["average_price"] == 101.23);
  assert(result.payload["currency"] == "EUR");
  assert(result.payload["unit"] == "EUR/MWh");
  assert(result.payload["status"] == "Final");
}

void test_nord_pool_average_without_time() {
  SpotPriceService::Request request{.area = "NO2",
                                    .date = "2026-06-16",
                                    .time = std::nullopt,
                                    .statistic = "average"};

  const auto result = SpotPriceService::lookup_nord_pool_from_json(
      nord_pool_payload(), request, utc(2026, 6, 15, 22, 10));

  assert(result.ok);
  assert(!result.payload.contains("price"));
  assert(result.payload["average_price"] == 101.23);
}

void test_nord_pool_tomorrow_all_without_time_skips_price() {
  SpotPriceService::Request request{.area = "NO2",
                                    .date = "2026-06-16",
                                    .time = "",
                                    .statistic = "all"};

  const auto result = SpotPriceService::lookup_nord_pool_from_json(
      nord_pool_payload(), request, utc(2026, 6, 15, 12, 0));

  assert(result.ok);
  assert(result.payload["min_price"] == 80.0);
  assert(result.payload["max_price"] == 120.0);
  assert(result.payload["average_price"] == 101.23);
  assert(!result.payload.contains("price"));
}

void test_ote_price_lookup_by_prague_time() {
  SpotPriceService::Request request{.area = "CZ",
                                    .date = "2026-06-23",
                                    .time = "00:20",
                                    .statistic = "all",
                                    .source = "ote",
                                    .time_resolution = "PT15M"};

  const auto result = SpotPriceService::lookup_ote_from_json(
      ote_payload(), request, utc(2026, 6, 22, 22, 5));

  assert(result.ok);
  assert(result.payload["area"] == "CZ");
  assert(result.payload["source"] == "ote");
  assert(result.payload["timezone"] == "Europe/Prague");
  assert(result.payload["price"] == 148.62);
  assert(result.payload["min_price"] == 138.90);
  assert(result.payload["max_price"] == 154.13);
  assert(result.payload["average_price"] == 146.32);
  assert(result.payload["period_count"] == 4);
  assert(result.payload["time_resolution"] == "PT15M");
}

void test_ote_60min_reference() {
  SpotPriceService::Request request{.area = "CZ",
                                    .date = "2026-06-23",
                                    .time = "00:20",
                                    .statistic = "price",
                                    .source = "ote",
                                    .time_resolution = "PT60M"};

  const auto result = SpotPriceService::lookup_ote_from_json(
      ote_payload(), request, utc(2026, 6, 22, 22, 5));

  assert(result.ok);
  assert(result.payload["price"] == 146.32);
  assert(result.payload["period_count"] == 1);
}

void test_spotovaelektrina_today_lookup() {
  SpotPriceService::Request request{.area = "CZ",
                                    .date = "2026-06-22",
                                    .time = "00:20",
                                    .statistic = "all",
                                    .source = "spotovaelektrina",
                                    .time_resolution = "PT15M"};

  const auto result = SpotPriceService::lookup_spotovaelektrina_from_json(
      spotovaelektrina_payload(), request, utc(2026, 6, 22, 12, 0));

  assert(result.ok);
  assert(result.payload["area"] == "CZ");
  assert(result.payload["source"] == "spotovaelektrina");
  assert(result.payload["timezone"] == "Europe/Prague");
  assert(result.payload["price"] == 120.0);
  assert(result.payload["min_price"] == 80.0);
  assert(result.payload["max_price"] == 120.0);
  assert(result.payload["average_price"] == 100.0);
  assert(result.payload["period_count"] == 3);
  assert(result.payload["time_resolution"] == "PT15M");
}

void test_spotovaelektrina_tomorrow_lookup() {
  SpotPriceService::Request request{.area = "CZ",
                                    .date = "2026-06-23",
                                    .time = "00:35",
                                    .statistic = "price",
                                    .source = "spotovaelektrina",
                                    .time_resolution = "PT15M"};

  const auto result = SpotPriceService::lookup_spotovaelektrina_from_json(
      spotovaelektrina_payload(), request, utc(2026, 6, 22, 12, 0));

  assert(result.ok);
  assert(result.payload["price"] == 143.64);
  assert(result.payload["date"] == "2026-06-23");
}

void test_spotovaelektrina_tomorrow_all_without_time_skips_price() {
  SpotPriceService::Request request{.area = "CZ",
                                    .date = "2026-06-23",
                                    .time = "",
                                    .statistic = "all",
                                    .source = "spotovaelektrina",
                                    .time_resolution = "PT15M"};

  const auto result = SpotPriceService::lookup_spotovaelektrina_from_json(
      spotovaelektrina_payload(), request, utc(2026, 6, 22, 12, 0));

  assert(result.ok);
  assert(result.payload["min_price"] == 143.64);
  assert(result.payload["max_price"] == 154.13);
  assert(result.payload["average_price"] == 148.8);
  assert(!result.payload.contains("price"));
}

void test_spotovaelektrina_rejects_other_dates() {
  SpotPriceService::Request request{.area = "CZ",
                                    .date = "2026-06-21",
                                    .time = "00:20",
                                    .statistic = "price",
                                    .source = "spotovaelektrina",
                                    .time_resolution = "PT15M"};

  const auto result = SpotPriceService::lookup_spotovaelektrina_from_json(
      spotovaelektrina_payload(), request, utc(2026, 6, 22, 12, 0));

  assert(!result.ok);
}

void test_local_date_supports_tomorrow_offset() {
  const auto now = utc(2026, 6, 22, 12, 0);
  assert(SpotPriceService::local_date(now, "Europe/Prague", 1) == "2026-06-23");
  assert(SpotPriceService::local_date(now, "Europe/Oslo", 1) == "2026-06-23");
}

} // namespace

int main() {
  test_nord_pool_price_lookup_by_oslo_time();
  test_nord_pool_average_without_time();
  test_nord_pool_tomorrow_all_without_time_skips_price();
  test_ote_price_lookup_by_prague_time();
  test_ote_60min_reference();
  test_spotovaelektrina_today_lookup();
  test_spotovaelektrina_tomorrow_lookup();
  test_spotovaelektrina_tomorrow_all_without_time_skips_price();
  test_spotovaelektrina_rejects_other_dates();
  test_local_date_supports_tomorrow_offset();
  std::cout << "SpotPriceService tests passed\n";
  return 0;
}
