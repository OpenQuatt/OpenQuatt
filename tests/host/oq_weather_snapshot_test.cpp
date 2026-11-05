#include <cassert>
#include <cstring>
#include "components/openquatt_weather/WeatherForecastSnapshot.h"

using namespace esphome::openquatt_weather;

ForecastSnapshot snapshot() {
  ForecastSnapshot value;
  value.published_ms = value.fetched_ms = 1000;
  value.now_epoch = value.fetched_epoch = value.start_epoch = 1800000000;
  value.enabled = value.valid = true;
  value.generation = 1;
  value.current = {1800000000, 10.5f, 85, 8, true};
  return value;
}

ForecastSnapshot issued_snapshot() {
  auto value = snapshot();
  value.configuration = 4;
  auto& scenario = value.scenario;
  scenario.valid = true;
  scenario.reason = ScenarioReason::READY;
  scenario.horizon_valid_mask = 7;
  scenario.observation_ms = value.published_ms;
  scenario.observation_epoch = value.now_epoch;
  auto& issue = scenario.issued;
  issue.monotonic_ms = value.published_ms;
  issue.epoch = value.now_epoch;
  issue.configuration = value.configuration;
  issue.weather_generation = 7;
  issue.weather_fetched_epoch = value.fetched_epoch;
  issue.weather_start_epoch = value.start_epoch;
  issue.model_context = 1;
  issue.model_samples = 155;
  issue.room_start_c = issue.held_heat_w = 0;
  issue.h_w_per_k = 220;
  issue.c_wh_per_k = 8000;
  for (auto& temperature : issue.temperature_c) temperature = 0;
  return value;
}

int main() {
  auto value = snapshot();
  value.now_epoch += 1800;
  value.published_ms += 1800000;
  auto exact = value;
  age_forecast_snapshot(exact, value.published_ms);
  assert(exact.current.valid);
  age_forecast_snapshot(value, value.published_ms + 1);
  assert(value.valid && !value.current.valid);  // No subsecond-phase rejuvenation.

  value = snapshot();
  value.published_ms += 10800000;
  value.now_epoch += 10800;
  age_forecast_snapshot(value, value.published_ms);
  assert(value.valid && !value.current.valid);
  age_forecast_snapshot(value, value.published_ms + 1);
  assert(!value.valid);

  // A legal +20 s wall-clock skew makes the UTC receipt expire before its
  // monotonic age. HTTP must apply both limits, just like the main-loop getter.
  value = snapshot();
  value.now_epoch += oq_weather::kMaxAgeS;
  value.published_ms += (oq_weather::kMaxAgeS - 20U) * 1000ULL;
  oq_weather::Series series;
  series.generation = value.generation;
  series.fetched_epoch = value.fetched_epoch;
  series.fetched_ms = value.fetched_ms;
  series.latitude = 52.09f;
  series.longitude = 6.15f;
  for (size_t i = 0; i < oq_weather::kHours; ++i) {
    series.time[i] = value.start_epoch + static_cast<uint32_t>(i) * 3600U;
    series.temperature[i] = 10;
    series.wind_mps[i] = 2;
  }
  assert(oq_weather::usable(series, value.now_epoch, value.published_ms));
  exact = value;
  age_forecast_snapshot(exact, value.published_ms);
  assert(exact.valid);
  for (const uint64_t elapsed_ms : {1ULL, 1000ULL}) {
    exact = value;
    assert(value.published_ms + elapsed_ms - value.fetched_ms < oq_weather::kMaxAgeS * 1000ULL);
    age_forecast_snapshot(exact, value.published_ms + elapsed_ms);
    assert(!exact.valid && !exact.current.valid);
  }

  for (auto now : {0ULL, 16001ULL}) {
    value = snapshot();
    age_forecast_snapshot(value, now);
    assert(!value.valid && !value.current.valid && value.now_epoch == 0);
  }
  value = snapshot();
  value.now_epoch = UINT32_MAX;
  age_forecast_snapshot(value, value.published_ms + 1);
  assert(!value.valid && value.now_epoch == 0);

  ForecastSnapshot cache, request;
  ForecastSnapshotAccess access;
  assert(!access.available() && !access.begin_request(1000));
  access.bind(&cache, &request);
  access.publish([](auto& dest) {
    dest = snapshot();
    std::strcpy(dest.configured_location, "Brummen");
  });
  assert(access.begin_request(1000));
  access.publish([](auto& dest) {
    dest.generation = 2;
    std::strcpy(dest.configured_location, "Utrecht");
  });
  assert(request.generation == 1 && std::strcmp(request.configured_location, "Brummen") == 0);
  assert(!access.begin_request(1000));
  access.end_request();
  assert(access.begin_request(1000));
  assert(request.generation == 2 && std::strcmp(request.configured_location, "Utrecht") == 0);
  access.end_request();
  value = issued_snapshot();
  age_forecast_snapshot(value, value.published_ms);
  assert(value.scenario.valid && value.scenario.horizon_valid_mask == 7);
  assert(value.scenario.issued.room_start_c == 0 && value.scenario.issued.held_heat_w == 0);
  // A newer accepted series in the same configuration cannot rewrite or hide
  // a legitimate earlier ticket just because its receipt predates that fetch.
  value.now_epoch += 10;
  value.published_ms += 10000;
  value.fetched_ms = value.published_ms;
  value.fetched_epoch = value.now_epoch;
  value.generation = 8;
  value.scenario.observation_ms = value.published_ms;
  value.scenario.observation_epoch = value.now_epoch;
  age_forecast_snapshot(value, value.published_ms);
  assert(value.scenario.valid && value.scenario.issued.weather_generation == 7);
  assert(value.scenario.issued.epoch < value.fetched_epoch);
  assert(value.scenario.issued.configuration == value.configuration);
  ++value.configuration;
  age_forecast_snapshot(value, value.published_ms);
  assert(!value.scenario.valid && value.scenario.horizon_valid_mask == 0);
  assert(value.scenario.reason == ScenarioReason::CONFIGURATION_CHANGED);
  --value.configuration;
  age_forecast_snapshot(value, value.published_ms);
  assert(!value.scenario.valid);  // Aging can never revive an invalid publication.
  for (const uint32_t seconds : {1799U, 10799U}) {
    value = issued_snapshot();
    value.now_epoch += seconds;
    value.published_ms += seconds * 1000ULL;
    value.scenario.observation_ms = value.published_ms;
    value.scenario.observation_epoch = value.now_epoch;
    age_forecast_snapshot(value, value.published_ms + 1U);
    if (seconds == 1799U)
      assert(value.scenario.valid && value.scenario.horizon_valid_mask == 6);
    else
      assert(!value.scenario.valid && value.scenario.reason == ScenarioReason::EXPIRED);
  }
  value = issued_snapshot();
  value.now_epoch += 3;
  age_forecast_snapshot(value, value.published_ms);
  assert(!value.scenario.valid && value.scenario.reason == ScenarioReason::CLOCK_INVALID);
  value = issued_snapshot();
  age_forecast_snapshot(value, value.published_ms + 15001);
  assert(!value.scenario.valid && value.scenario.reason == ScenarioReason::SNAPSHOT_STALE);
}
