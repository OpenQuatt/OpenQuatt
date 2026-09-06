#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/learning/oq_ph_thermal_aggregate.h"

using namespace oq_power_house::learning;

static LearningSnapshot sample(uint64_t elapsed_ms) {
  LearningSnapshot value;
  value.monotonic_ms = 1000 + elapsed_ms;
  value.epoch_s = 1800000000 + static_cast<uint32_t>(elapsed_ms / 1000);
  value.source_generation = value.physical_context_generation = value.control_generation = 1;
  value.room_c = 20.0f + static_cast<float>(elapsed_ms) / 3600000.0f;
  value.setpoint_c = 20.0f;
  value.outside_c = 5.0f;
  value.heat_to_water_w = 3000.0f;
  value.heat_uncertainty_w = 50.0f;
  value.mean_water_c = 35.0f;
  return value;
}

int main() {
  QualityConfig quality;
  ThermalWindowConfig config;
  ThermalWindowAccumulator state;
  ThermalWindowResult result;
  // An hour with irregular, fully covered observations produces two adjacent
  // windows. A 1 K/h room change is informative here, though batch rejects it.
  for (uint64_t time = 0; time <= 3600000; time += time % 60000 == 0 ? 10000 : 50000) {
    result = observe_thermal_snapshot(state, sample(time), quality, config);
    if (time == 1800000) {
      assert(result.has_interval);
      assert(fabs(result.interval.mean_indoor_c - 20.25) < 1e-5);
      assert(fabs(result.interval.mean_heat_w - 3000.0) < 1e-8);
      assert(result.interval.indoor_end_c > result.interval.indoor_start_c);
      assert(!result.interval.unmodeled_gain_bound_valid);
    } else if (time == 3600000) {
      assert(result.has_interval);
      assert(result.interval.start_monotonic_ms == 1801000);
      assert(fabs(result.interval.mean_indoor_c - 20.75) < 1e-5);
    } else {
      assert(!result.has_interval);
    }
  }

  // Never mask a batch-invalid CSV row into an eligible dynamic measurement.
  auto invalid = sample(3610000);
  invalid.invalid_reasons = INVALID_SETPOINT_RECOVERY;
  result = observe_thermal_snapshot(state, invalid, quality, config);
  assert(result.status == ThermalWindowStatus::INVALID_MEASUREMENT && !state.active);
  observe_thermal_snapshot(state, sample(3620000), quality, config);
  result = observe_thermal_snapshot(state, sample(3690000), quality, config);
  assert(result.status == ThermalWindowStatus::TIME_DISCONTINUITY && !result.has_interval);
  assert(state.first.monotonic_ms == 3691000);

  auto changed = sample(3700000);
  changed.control_generation = 2;
  result = observe_thermal_snapshot(state, changed, quality, config);
  assert(result.status == ThermalWindowStatus::CONTEXT_CHANGED && !result.has_interval);
  config.unmodeled_gain_bound_valid = true;
  config.unmodeled_gain_bound_w = 100;
  changed = sample(3710000);
  changed.control_generation = 2;
  result = observe_thermal_snapshot(state, changed, quality, config);
  assert(result.status == ThermalWindowStatus::CONTEXT_CHANGED);

  // Small UTC drift per pair may not accumulate into an accepted window.
  state = {};
  for (uint64_t i = 0; i < 10; ++i) {
    auto drifting = sample(i * 60000);
    drifting.epoch_s += static_cast<uint32_t>(i);
    result = observe_thermal_snapshot(state, drifting, quality, config);
    if (i == 3) assert(result.status == ThermalWindowStatus::TIME_DISCONTINUITY);
    assert(!result.has_interval);
  }

  // Signed off-period heat is not replaced by zero.
  state = {};
  for (uint64_t time = 0; time <= 1800000; time += 60000) {
    auto cooling = sample(time);
    cooling.heat_to_water_w = -200.0f;
    result = observe_thermal_snapshot(state, cooling, quality, config);
  }
  assert(result.has_interval && result.interval.mean_heat_w == -200.0);
  assert(result.interval.unmodeled_gain_bound_valid && result.interval.unmodeled_gain_bound_w == 100.0);
  quality.max_interval_ms = 0;
  result = observe_thermal_snapshot(state, sample(1810000), quality, config);
  assert(result.status == ThermalWindowStatus::INVALID_CONFIGURATION && !state.active);
  return 0;
}
