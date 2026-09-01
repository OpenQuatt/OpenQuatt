#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_thermal_limits {

inline float clamp(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

struct Config {
  float default_max_c = 60.0f;
  float soft_band_c = 3.0f;
  float trip_offset_c = 5.0f;
  float factor_at_max = 0.25f;
  uint32_t cm3_trip_hold_ms = 30000UL;
};

struct Inputs {
  uint32_t now_ms = 0;
  float supply_c = NAN;
  float configured_max_c = NAN;
  int control_mode = 0;
};

struct State {
  float limit_factor = 1.0f;
  bool boiler_inhibit = false;
  bool trip = false;
  bool hard_trip = false;
  bool trip_timer_running = false;
  uint32_t trip_started_ms = 0;
};

struct Result {
  float max_c = 60.0f;
  float soft_start_c = 57.0f;
  float trip_c = 65.0f;
};

inline Result update(State& state, const Inputs& inputs, const Config& config = {}) {
  Result result;
  result.max_c = isnan(inputs.configured_max_c) ? config.default_max_c : inputs.configured_max_c;
  result.max_c = clamp(result.max_c, 25.0f, 75.0f);
  result.soft_start_c = result.max_c - config.soft_band_c;
  result.trip_c = clamp(result.max_c + config.trip_offset_c, result.max_c + 1.0f, 85.0f);
  if (isnan(state.limit_factor)) state.limit_factor = 1.0f;

  if (isnan(inputs.supply_c)) {
    state.limit_factor = (state.trip || state.hard_trip) ? 0.0f : 1.0f;
    return result;
  }

  if (!state.boiler_inhibit && inputs.supply_c >= result.max_c) state.boiler_inhibit = true;
  if (state.boiler_inhibit && inputs.supply_c <= result.soft_start_c) state.boiler_inhibit = false;
  if (!state.trip && inputs.supply_c >= result.trip_c) state.trip = true;
  if (state.trip && inputs.supply_c <= result.max_c) state.trip = false;

  if (inputs.supply_c <= result.soft_start_c) {
    state.limit_factor = 1.0f;
  } else if (inputs.supply_c < result.max_c && config.soft_band_c > 0.0f) {
    const float segment = clamp((inputs.supply_c - result.soft_start_c) / config.soft_band_c, 0.0f, 1.0f);
    state.limit_factor = 1.0f + (config.factor_at_max - 1.0f) * segment;
  } else if (inputs.supply_c < result.trip_c) {
    const float span_c = result.trip_c - result.max_c;
    const float segment = span_c > 0.0f ? clamp((inputs.supply_c - result.max_c) / span_c, 0.0f, 1.0f) : 1.0f;
    state.limit_factor = config.factor_at_max * (1.0f - segment);
  } else {
    state.limit_factor = 0.0f;
  }

  if (state.hard_trip) {
    if (inputs.supply_c <= result.max_c) {
      state.hard_trip = false;
      state.trip_timer_running = false;
    }
  } else if (inputs.supply_c >= result.trip_c) {
    if (inputs.control_mode == 3) {
      if (!state.trip_timer_running) {
        state.trip_timer_running = true;
        state.trip_started_ms = inputs.now_ms;
      }
      state.hard_trip = static_cast<uint32_t>(inputs.now_ms - state.trip_started_ms) >= config.cm3_trip_hold_ms;
    } else {
      state.trip_started_ms = inputs.now_ms;
      state.trip_timer_running = true;
      state.hard_trip = true;
    }
  } else {
    state.trip_timer_running = false;
  }

  if (state.hard_trip) state.limit_factor = 0.0f;
  state.limit_factor = clamp(state.limit_factor, 0.0f, 1.0f);
  return result;
}

}  // namespace oq_thermal_limits
