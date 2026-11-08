#include <cassert>
#include <cmath>
#include <cstring>

#include "components/openquatt_weather/OpenQuattWeatherLogic.h"
#include "openquatt/includes/learning/oq_ph_forecast_logic.h"

namespace {
using namespace oq_power_house::forecast;

Observation observation(uint32_t elapsed_s = 0) {
  Observation o;
  o.monotonic_ms = 1000ULL + elapsed_s * 1000ULL;
  o.epoch = 1800000000U + elapsed_s;
  o.context = 1;
  o.valid = o.model_ready = true;
  o.model_samples = 100;
  o.room_c = 20;
  o.outside_c = 5;
  o.heat_w = 3000;
  o.h_w_per_k = 200;
  o.c_wh_per_k = 6000;
  return o;
}

void weather_contract() {
  oq_weather::Series series;
  series.fetched_epoch = 1800000000U;
  series.fetched_ms = 1000;
  series.generation = 1;
  series.latitude = 52.37;
  series.longitude = 4.90;
  for (size_t i = 0; i < oq_weather::kHours; ++i) {
    series.time[i] = series.fetched_epoch + i * 3600U;
    series.temperature[i] = 5 + i * 0.1;
  }
  assert(oq_weather::valid_series(series, 52.37, 4.90, series.fetched_epoch));
  assert(oq_weather::usable(series, series.fetched_epoch + 10800, 10801000));
  assert(!oq_weather::usable(series, series.fetched_epoch + 10801, 10802000));
  assert(!oq_weather::usable(series, series.fetched_epoch + 3600, 1000));
  assert(!oq_weather::valid_series(series, 51.0, 4.90, series.fetched_epoch));
  assert(!oq_weather::valid_series(series, NAN, 4.90, series.fetched_epoch));
  float value = NAN;
  assert(oq_weather::temperature_at(series, series.time[0] + 1800, value));
  assert(std::fabs(value - 5.05f) < 0.0001);
  assert(!oq_weather::temperature_at(series, series.time[35] + 1, value));
  series.temperature[4] = NAN;
  assert(!oq_weather::valid_series(series, 52.37, 4.90, series.fetched_epoch));
  series.temperature[4] = 5;
  series.time[4] += 1;
  assert(!oq_weather::valid_series(series, 52.37, 4.90, series.fetched_epoch));
}

void frozen_model_and_decomposition() {
  State state;
  float weather[kForcingSteps + 1];
  for (auto& temperature : weather) temperature = 8;
  auto o = observation();
  observe(state, o);
  assert(issue(state, weather, 4));
  assert(!issue(state, weather, 5));
  const auto frozen = state.pending[0];
  // Actual heat changes after issuance; the ideal house remains at 20 C.
  // Later model fits and changed caller buffers cannot alter the old ticket.
  for (auto& temperature : weather) temperature = -10;
  for (uint32_t elapsed = 10; elapsed <= 10800; elapsed += 10) {
    o = observation(elapsed);
    o.h_w_per_k = 400;
    o.c_wh_per_k = 10000;
    o.model_samples = 200;
    observe(state, o);
  }
  assert(!state.pending[0].active);
  assert(state.pending[0].h == frozen.h && state.pending[0].weather[0] == 8);
  for (size_t i = 0; i < 3; ++i) {
    const auto& errors = state.errors[i];
    assert(errors.issued.count == 1);
    assert(errors.actual_inputs.mae() < 1e-10);
    assert(errors.persistence.mae() < 1e-10);
    assert(errors.weather_contribution.mae() > 0);
    assert(errors.heat_assumption.mae() < 1e-9);
    assert(std::fabs(errors.issued.signed_sum - errors.actual_inputs.signed_sum -
                     errors.weather_contribution.signed_sum - errors.heat_assumption.signed_sum) < 1e-10);
  }
}

void heating_assumption_is_not_a_model_error() {
  State state;
  float weather[kForcingSteps + 1];
  for (auto& v : weather) v = 5;
  auto o = observation();
  o.heat_w = 4000;
  observe(state, o);
  assert(issue(state, weather, 1));
  double room = o.room_c;
  for (uint32_t elapsed = 10; elapsed <= 10800; elapsed += 10) {
    const auto previous = o;
    o = observation(elapsed);
    o.heat_w = 3000;
    room = advance(room, 5, (previous.heat_w + o.heat_w) / 2, 200, 6000, 10.0 / 3600);
    o.room_c = room;
    // No fitting on future measurements is needed to score this ticket.
    o.model_ready = false;
    observe(state, o);
  }
  for (const auto& errors : state.errors) {
    assert(errors.issued.count == 1);
    assert(errors.actual_inputs.mae() < 1e-10);
    assert(errors.weather_contribution.mae() < 1e-10);
    assert(errors.heat_assumption.mae() > 0.05);
  }
}

void gates_gaps_and_restart() {
  State state;
  float weather[kForcingSteps + 1]{};
  auto o = observation();
  o.model_ready = false;
  observe(state, o);
  assert(!issue(state, weather, 1));
  o.model_ready = true;
  observe(state, o);  // Duplicate receipts do not replace an observation.
  assert(!issue(state, weather, 1));
  o = observation(10);
  observe(state, o);
  weather[2] = NAN;
  assert(!issue(state, weather, 1));
  weather[2] = 0;
  assert(issue(state, weather, 1));
  observe(state, observation(41));
  assert(state.discarded_count == 1 && !state.pending[0].active);
  assert(state.errors[0].issued.count == 0);
  assert(std::strcmp(state.status, "measurement_gap_or_clock_jump") == 0);
  o = observation(51);
  observe(state, o);
  assert(issue(state, weather, 1));
  o = observation(61);
  o.epoch += 60;
  observe(state, o);
  assert(state.discarded_count == 2);
  o = observation(71);
  o.context = 2;
  observe(state, o);
  assert(state.context == 2 && state.errors[0].issued.count == 0);
  assert(issue(state, weather, 1));
  o = observation(81);
  o.context = 2;
  o.model_samples = 0;
  o.model_ready = false;
  state.errors[0].issued.add(1);
  observe(state, o);
  assert(!state.pending[0].active && state.errors[0].issued.count == 0);
  o = observation(91);
  o.context = 2;
  observe(state, o);
  assert(issue(state, weather, 1));
  o = {};
  observe(state, o);
  assert(!state.pending[0].active && std::isnan(state.latest[0]));
  State rebooted;
  assert(rebooted.issued_count == 0 && std::isnan(rebooted.latest[0]));
}

void bounded_queue_and_boundary_interpolation() {
  State state;
  float weather[kForcingSteps + 1];
  for (auto& v : weather) v = 5;
  // Sample/issue every 13 seconds, so maturity falls between measurements.
  for (uint32_t elapsed = 0; elapsed < 4 * 3600; elapsed += 13) {
    observe(state, observation(elapsed));
    issue(state, weather, 1);
  }
  assert(state.issued_count >= 15);
  assert(state.errors[0].issued.count > state.errors[2].issued.count);
  for (const auto& errors : state.errors) assert(errors.actual_inputs.mae() < 1e-10);
  size_t count = 0;
  for (const auto& ticket : state.pending) count += ticket.active;
  assert(count <= kPendingSlots);
}
}  // namespace

int main() {
  weather_contract();
  frozen_model_and_decomposition();
  heating_assumption_is_not_a_model_error();
  gates_gaps_and_restart();
  bounded_queue_and_boundary_interpolation();
}
