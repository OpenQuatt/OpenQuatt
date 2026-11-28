#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_thermal_limits {

constexpr uint64_t kPostDhwStartMaxAgeMs = 30000;
constexpr uint64_t kPostDhwMaxAgeMs = 90000;
constexpr float kPostDhwSystemCeilingC = 70.0f;
constexpr float kPostDhwHeatPumpCeilingC = 55.0f;

inline bool post_dhw_window_open(uint64_t now_ms, uint64_t dhw_end_ms, bool active) {
  const uint64_t max_age = active ? kPostDhwMaxAgeMs : kPostDhwStartMaxAgeMs;
  return dhw_end_ms != 0 && now_ms >= dhw_end_ms && now_ms - dhw_end_ms < max_age;
}

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
  uint64_t dhw_end_ms = 0;
  float heat_pump_water_c = NAN;
  bool supply_observed = true;
  bool heat_pump_water_observed = true;
  bool running_heat_pumps = true;
};

struct State {
  float limit_factor = 1.0f;
  bool boiler_inhibit = false;
  bool trip = false;
  bool hard_trip = false;
  bool trip_timer_running = false;
  uint32_t trip_started_ms = 0;
  uint64_t used_dhw_end_ms = 0;
  bool post_dhw_active = false;
  bool awaiting_supply = false;
  bool awaiting_heat_pump_water = false;

  bool heat_pump_hard_trip() const { return hard_trip || awaiting_heat_pump_water; }
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

  const bool was_post_dhw = state.post_dhw_active;
  const bool hot_supply = inputs.supply_c > result.soft_start_c;
  const bool new_pulse =
      inputs.supply_c >= result.max_c && inputs.supply_c - inputs.heat_pump_water_c >= config.trip_offset_c;
  const bool current_edge = inputs.dhw_end_ms != 0 && (was_post_dhw ? inputs.dhw_end_ms == state.used_dhw_end_ms
                                                                    : inputs.dhw_end_ms != state.used_dhw_end_ms);
  const bool post_dhw_relevant = was_post_dhw || (current_edge && inputs.supply_observed && inputs.running_heat_pumps &&
                                                  (new_pulse || inputs.supply_c >= kPostDhwSystemCeilingC));
  const float heat_pump_trip_c = fminf(result.trip_c, kPostDhwHeatPumpCeilingC);
  const float heat_pump_release_c = fminf(result.max_c, kPostDhwHeatPumpCeilingC - config.trip_offset_c);

  // A fallback sensor cannot attest that the hot water at the system probe cleared.
  state.awaiting_supply =
      ((was_post_dhw || state.awaiting_heat_pump_water) && !inputs.supply_observed) ||
      (post_dhw_relevant && inputs.supply_c >= kPostDhwSystemCeilingC) ||
      (state.awaiting_supply &&
       (!inputs.supply_observed || inputs.supply_c > fminf(result.max_c, kPostDhwSystemCeilingC - 1.0f)));
  state.awaiting_heat_pump_water =
      (post_dhw_relevant && inputs.heat_pump_water_c >= heat_pump_trip_c) ||
      (was_post_dhw && hot_supply && !inputs.heat_pump_water_observed) ||
      (state.awaiting_heat_pump_water && (!inputs.heat_pump_water_observed || !isfinite(inputs.heat_pump_water_c) ||
                                          inputs.heat_pump_water_c > heat_pump_release_c));
  state.post_dhw_active = current_edge && hot_supply && (was_post_dhw || new_pulse) && inputs.supply_observed &&
                          inputs.running_heat_pumps && inputs.heat_pump_water_observed &&
                          isfinite(inputs.heat_pump_water_c) && inputs.heat_pump_water_c < result.max_c &&
                          inputs.heat_pump_water_c < heat_pump_trip_c && !state.awaiting_supply &&
                          !state.awaiting_heat_pump_water && !state.trip && !state.hard_trip;
  if (state.post_dhw_active) state.used_dhw_end_ms = inputs.dhw_end_ms;
  const bool force_stop =
      state.awaiting_supply || (was_post_dhw && !state.post_dhw_active && inputs.supply_c >= result.trip_c);
  if (force_stop) {
    state.boiler_inhibit = true;
    state.trip = true;
    state.hard_trip = true;
    state.limit_factor = 0.0f;
    return result;
  }

  if (!state.boiler_inhibit && inputs.supply_c >= result.max_c) state.boiler_inhibit = true;
  if (state.boiler_inhibit && inputs.supply_c <= result.soft_start_c) state.boiler_inhibit = false;

  const float supply_c = state.post_dhw_active ? inputs.heat_pump_water_c : inputs.supply_c;
  if (isnan(supply_c)) {
    state.limit_factor = (state.trip || state.hard_trip) ? 0.0f : 1.0f;
    return result;
  }

  if (!state.trip && supply_c >= result.trip_c) state.trip = true;
  if (state.trip && supply_c <= result.max_c) state.trip = false;

  if (supply_c <= result.soft_start_c) {
    state.limit_factor = 1.0f;
  } else if (supply_c < result.max_c && config.soft_band_c > 0.0f) {
    const float segment = clamp((supply_c - result.soft_start_c) / config.soft_band_c, 0.0f, 1.0f);
    state.limit_factor = 1.0f + (config.factor_at_max - 1.0f) * segment;
  } else if (supply_c < result.trip_c) {
    const float span_c = result.trip_c - result.max_c;
    const float segment = span_c > 0.0f ? clamp((supply_c - result.max_c) / span_c, 0.0f, 1.0f) : 1.0f;
    state.limit_factor = config.factor_at_max * (1.0f - segment);
  } else {
    state.limit_factor = 0.0f;
  }

  if (state.hard_trip) {
    if (supply_c <= result.max_c) {
      state.hard_trip = false;
      state.trip_timer_running = false;
    }
  } else if (supply_c >= result.trip_c) {
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

  // Power House also uses this factor for house demand, which CM4 still needs.
  // An ODU-only latch blocks compressor writes through heat_pump_hard_trip().
  if (state.hard_trip) state.limit_factor = 0.0f;
  state.limit_factor = clamp(state.limit_factor, 0.0f, 1.0f);
  return result;
}

}  // namespace oq_thermal_limits
