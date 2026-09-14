#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_power_house {
constexpr uint8_t kDemandSourceNone = 0;
constexpr uint8_t kDemandSourceHaInput = 1;
constexpr uint8_t kDemandSourceApiInput = 2;
struct Feedforward {
  float house_power_w = 0.0f;
  bool external = false;
};
struct CadenceDecision {
  bool due = false;
  float dt_s = 0.0f;
};
struct DemandInput {
  uint32_t now_ms = 0;
  float outside_c = NAN, cold_c = NAN, zero_power_c = NAN, rated_w = NAN;
  float room_c = NAN, setpoint_c = NAN, external_w = NAN;
  float water_limit_factor = NAN;
  bool external_valid = false;
};
struct DemandTuning {
  float temperature_guard_c = 0.0f, reaction_w_per_k = NAN;
  float comfort_below_c = NAN, comfort_above_c = NAN;
  float rise_time_min = NAN, fall_time_min = NAN;
  int demand_max = 0;
};
struct DemandState {
  float last_w = 0.0f;
  uint32_t last_ms = 0;
  float comfort_memory_c = 0.0f;
};
struct DemandDecision {
  DemandState next;
  float requested_w = 0.0f;
  int raw_demand = 0;
  bool external = false;
  bool valid = false;
};
inline float clamp_power(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}
inline float modelled_house_power_w(float zero_power_temp_c, float cold_temp_c, float outside_temp_c,
                                    float rated_power_w) {
  if (!isfinite(zero_power_temp_c) || !isfinite(cold_temp_c) || !isfinite(outside_temp_c) || !isfinite(rated_power_w) ||
      rated_power_w <= 0.0f || !(zero_power_temp_c > cold_temp_c)) {
    return NAN;
  }
  const float load = clamp_power((zero_power_temp_c - outside_temp_c) / (zero_power_temp_c - cold_temp_c), 0.0f, 1.0f);
  return rated_power_w * load;
}
inline Feedforward select_feedforward(float modelled_power_w, float external_power_w, bool external_valid,
                                      float rated_power_w) {
  Feedforward result{modelled_power_w, false};
  if (!external_valid || !isfinite(external_power_w) || !isfinite(rated_power_w) || rated_power_w <= 0.0f) {
    return result;
  }
  result.house_power_w = clamp_power(external_power_w, 0.0f, rated_power_w);
  result.external = true;
  return result;
}
inline bool hold_cached_demand(uint8_t cached_source, uint8_t current_source, float cached_power_w, uint32_t cached_ms,
                               uint32_t now_ms, uint32_t hold_ms) {
  if (hold_ms == 0 || cached_ms == 0 || !isfinite(cached_power_w)) return false;
  if (cached_source == kDemandSourceNone || cached_source != current_source) return false;
  return static_cast<uint32_t>(now_ms - cached_ms) < hold_ms;
}
// Unsigned elapsed time keeps the cadence valid across one millis() rollover.
inline CadenceDecision decide_cadence(uint32_t now_ms, uint32_t last_ms, uint32_t target_ms) {
  if (target_ms == 0) return {};
  if (last_ms == 0) return {true, static_cast<float>(target_ms) / 1000.0f};
  const uint32_t elapsed_ms = static_cast<uint32_t>(now_ms - last_ms);
  if (elapsed_ms < target_ms) return {};
  return {true, static_cast<float>(elapsed_ms) / 1000.0f};
}
inline DemandDecision decide_demand(const DemandInput& in, const DemandTuning& tuning, const DemandState& state) {
  DemandDecision out;
  out.next.last_ms = in.now_ms == 0 ? UINT32_MAX : in.now_ms;
  const bool valid =
      isfinite(in.outside_c) && isfinite(in.cold_c) && isfinite(in.zero_power_c) && isfinite(in.rated_w) &&
      in.rated_w > 0.0f && isfinite(in.room_c) && isfinite(in.setpoint_c) && isfinite(in.water_limit_factor) &&
      isfinite(tuning.temperature_guard_c) && tuning.temperature_guard_c >= 0.0f && isfinite(tuning.reaction_w_per_k) &&
      tuning.reaction_w_per_k >= 0.0f && isfinite(tuning.comfort_below_c) && isfinite(tuning.comfort_above_c) &&
      isfinite(tuning.rise_time_min) && isfinite(tuning.fall_time_min) && tuning.demand_max > 0 &&
      in.zero_power_c > in.cold_c + tuning.temperature_guard_c;
  if (!valid) return out;
  const float modelled_w = modelled_house_power_w(in.zero_power_c, in.cold_c, in.outside_c, in.rated_w);
  const Feedforward feedforward = select_feedforward(modelled_w, in.external_w, in.external_valid, in.rated_w);
  if (!isfinite(modelled_w) || !isfinite(feedforward.house_power_w)) return out;
  const float below_c = clamp_power(tuning.comfort_below_c, 0.0f, 2.0f);
  const float above_c = clamp_power(tuning.comfort_above_c, 0.0f, 2.0f);
  const float low_base_c = in.setpoint_c - below_c;
  const float high_base_c = in.setpoint_c + above_c;
  const float mid_base_c = low_base_c + 0.5f * (high_base_c - low_base_c);
  const float memory_max_c = clamp_power(0.05f + 0.50f * above_c, 0.08f, 0.20f);
  float memory_c = isfinite(state.comfort_memory_c) ? state.comfort_memory_c : 0.0f;
  memory_c = clamp_power(memory_c, 0.0f, memory_max_c);
  float dt_s = 0.0f;
  float last_w = feedforward.house_power_w;
  if (state.last_ms != 0) {
    dt_s = static_cast<float>(static_cast<uint32_t>(in.now_ms - state.last_ms)) / 1000.0f;
    if (isfinite(state.last_w)) last_w = clamp_power(state.last_w, 0.0f, in.rated_w);
  }
  if (in.room_c < low_base_c) {
    const float undershoot = clamp_power((low_base_c - in.room_c) / 0.45f, 0.0f, 1.0f);
    const float build_per_min = memory_max_c / 90.0f + (memory_max_c / 24.0f - memory_max_c / 90.0f) * undershoot;
    memory_c += build_per_min * dt_s / 60.0f;
  } else if (in.room_c > mid_base_c) {
    const float decay_minutes = in.room_c <= high_base_c ? 40.0f : 12.0f;
    memory_c -= (memory_max_c / decay_minutes) * dt_s / 60.0f;
  }
  memory_c = clamp_power(memory_c, 0.0f, memory_max_c);
  const float effective_setpoint_c = in.setpoint_c + memory_c;
  const float low_c = effective_setpoint_c - below_c;
  float error_c = 0.0f;
  if (in.room_c < low_c)
    error_c = low_c - in.room_c;
  else if (in.room_c > in.setpoint_c)
    error_c = in.setpoint_c - in.room_c;
  const float raw_w = clamp_power(feedforward.house_power_w + tuning.reaction_w_per_k * error_c, 0.0f, in.rated_w);
  if (!isfinite(error_c) || !isfinite(raw_w)) return out;
  const float rise_min = clamp_power(tuning.rise_time_min, 2.0f, 20.0f);
  const float fall_min = clamp_power(tuning.fall_time_min, 1.0f, 10.0f);
  float limited_w = raw_w;
  if (dt_s > 0.0f && raw_w > last_w)
    limited_w = fminf(raw_w, last_w + in.rated_w * dt_s / (rise_min * 60.0f));
  else if (dt_s > 0.0f && raw_w < last_w)
    limited_w = fmaxf(raw_w, last_w - in.rated_w * dt_s / (fall_min * 60.0f));
  out.requested_w = limited_w * clamp_power(in.water_limit_factor, 0.0f, 1.0f);
  out.raw_demand = static_cast<int>(lroundf(tuning.demand_max * (out.requested_w / in.rated_w)));
  if (out.raw_demand < 0) out.raw_demand = 0;
  if (out.raw_demand > tuning.demand_max) out.raw_demand = tuning.demand_max;
  out.external = feedforward.external;
  out.valid = true;
  out.next = {out.requested_w, out.next.last_ms, memory_c};
  return out;
}
}  // namespace oq_power_house
