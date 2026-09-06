#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_power_house_demand_logic.h"

// Frozen copy of the baseline decide_demand implementation at 0282c2e9.
// Keep this oracle independent of the implementation under test while the
// Power House demand refactor is in progress.
namespace baseline_0282c2e9 {

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

float clamp_power(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

float modelled_house_power_w(float zero_power_temp_c, float cold_temp_c, float outside_temp_c, float rated_power_w) {
  if (!isfinite(zero_power_temp_c) || !isfinite(cold_temp_c) || !isfinite(outside_temp_c) || !isfinite(rated_power_w) ||
      rated_power_w <= 0.0f || !(zero_power_temp_c > cold_temp_c))
    return NAN;
  const float load = clamp_power((zero_power_temp_c - outside_temp_c) / (zero_power_temp_c - cold_temp_c), 0.0f, 1.0f);
  return rated_power_w * load;
}

DemandDecision decide_demand(const DemandInput& in, const DemandTuning& tuning, const DemandState& state) {
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
  float feedforward_w = modelled_w;
  bool external = false;
  if (in.external_valid && isfinite(in.external_w) && isfinite(in.rated_w) && in.rated_w > 0.0f) {
    feedforward_w = clamp_power(in.external_w, 0.0f, in.rated_w);
    external = true;
  }
  if (!isfinite(modelled_w) || !isfinite(feedforward_w)) return out;
  const float below_c = clamp_power(tuning.comfort_below_c, 0.0f, 2.0f);
  const float above_c = clamp_power(tuning.comfort_above_c, 0.0f, 2.0f);
  const float low_base_c = in.setpoint_c - below_c;
  const float high_base_c = in.setpoint_c + above_c;
  const float mid_base_c = low_base_c + 0.5f * (high_base_c - low_base_c);
  const float memory_max_c = clamp_power(0.05f + 0.50f * above_c, 0.08f, 0.20f);
  float memory_c = isfinite(state.comfort_memory_c) ? state.comfort_memory_c : 0.0f;
  memory_c = clamp_power(memory_c, 0.0f, memory_max_c);
  float dt_s = 0.0f;
  float last_w = feedforward_w;
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
  const float raw_w = clamp_power(feedforward_w + tuning.reaction_w_per_k * error_c, 0.0f, in.rated_w);
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
  out.external = external;
  out.valid = true;
  out.next = {out.requested_w, out.next.last_ms, memory_c};
  return out;
}

}  // namespace baseline_0282c2e9

namespace {

using ActualInput = oq_power_house::DemandInput;
using ActualTuning = oq_power_house::DemandTuning;
using ActualState = oq_power_house::DemandState;

void assert_close(float actual, float expected, const char* field) {
  if (isnan(actual) || isnan(expected)) {
    assert(isnan(actual) && isnan(expected));
    return;
  }
  const float tolerance = 1.0e-5f * (fabsf(expected) > 1.0f ? fabsf(expected) : 1.0f);
  if (fabsf(actual - expected) > tolerance) {
    (void)field;
    assert(false);
  }
}

void compare_case(const ActualInput& input, const ActualTuning& tuning, const ActualState& state) {
  const auto actual = oq_power_house::decide_demand(input, tuning, state);
  const baseline_0282c2e9::DemandInput frozen_input{
      input.now_ms, input.outside_c,  input.cold_c,     input.zero_power_c,       input.rated_w,
      input.room_c, input.setpoint_c, input.external_w, input.water_limit_factor, input.external_valid};
  const baseline_0282c2e9::DemandTuning frozen_tuning{
      tuning.temperature_guard_c, tuning.reaction_w_per_k, tuning.comfort_below_c, tuning.comfort_above_c,
      tuning.rise_time_min,       tuning.fall_time_min,    tuning.demand_max};
  const baseline_0282c2e9::DemandState frozen_state{state.last_w, state.last_ms, state.comfort_memory_c};
  const auto expected = baseline_0282c2e9::decide_demand(frozen_input, frozen_tuning, frozen_state);
  assert(actual.valid == expected.valid);
  assert(actual.external == expected.external);
  assert(actual.raw_demand == expected.raw_demand);
  assert(actual.next.last_ms == expected.next.last_ms);
  assert_close(actual.requested_w, expected.requested_w, "requested_w");
  assert_close(actual.next.last_w, expected.next.last_w, "next.last_w");
  assert_close(actual.next.comfort_memory_c, expected.next.comfort_memory_c, "comfort_memory_c");
}

ActualTuning tuning() { return {0.15f, 1800.0f, 0.35f, 0.55f, 8.0f, 5.0f, 10}; }

ActualInput input() { return {123456u, 2.0f, -10.0f, 16.0f, 7000.0f, 19.5f, 20.0f, NAN, 1.0f, false}; }

void deterministic_parameter_and_state_sweep() {
  const float outside[] = {-30.0f, -10.0f, -2.0f, 5.0f, 16.0f, 30.0f};
  const float room[] = {17.0f, 19.65f, 20.0f, 20.4f, 22.5f};
  const float water[] = {0.0f, 0.49f, 0.5f, 0.999f, 1.0f, 1.5f};
  const float memory[] = {NAN, -1.0f, 0.0f, 0.08f, 0.2f, 1.0f};
  const float external[] = {NAN, -500.0f, 0.0f, 3500.0f, 9000.0f};
  for (float tout : outside)
    for (float room_c : room)
      for (float water_factor : water)
        for (float memory_c : memory)
          for (float external_w : external) {
            auto in = input();
            in.outside_c = tout;
            in.room_c = room_c;
            in.water_limit_factor = water_factor;
            in.external_w = external_w;
            in.external_valid = true;
            auto state = ActualState{3500.0f, 120000u, memory_c};
            compare_case(in, tuning(), state);
          }
}

void invalid_inputs_and_rollover() {
  const float nan = NAN;
  const float inf = HUGE_VALF;
  auto in = input();
  float* values[] = {&in.outside_c, &in.cold_c,     &in.zero_power_c,      &in.rated_w,
                     &in.room_c,    &in.setpoint_c, &in.water_limit_factor};
  for (float* value : values) {
    *value = nan;
    compare_case(in, tuning(), ActualState{0.0f, 0u, 0.0f});
    *value = inf;
    compare_case(in, tuning(), ActualState{0.0f, 0u, 0.0f});
    *value = 1.0f;
  }
  in = input();
  in.now_ms = 20u;
  auto state = ActualState{6500.0f, UINT32_MAX - 100u, 0.15f};
  compare_case(in, tuning(), state);
}

void normalisation_half_boundaries_and_slew() {
  auto in = input();
  auto state = ActualState{0.0f, 0u, 0.0f};
  const int demand_max_values[] = {1, 2, 3, 10, 17};
  const float water_values[] = {0.4999f, 0.5f, 0.5001f};
  for (int demand_max : demand_max_values)
    for (float water : water_values) {
      auto t = tuning();
      t.demand_max = demand_max;
      in.water_limit_factor = water;
      compare_case(in, t, state);
    }
  const float previous_values[] = {0.0f, 3500.0f, 7000.0f, NAN};
  const uint32_t elapsed_values[] = {1u, 119999u, 120000u, 3600000u};
  for (float previous : previous_values)
    for (uint32_t elapsed : elapsed_values) {
      in = input();
      in.now_ms = 500000u + elapsed;
      state = {previous, 500000u, 0.18f};
      compare_case(in, tuning(), state);
    }
}

}  // namespace

int main() {
  deterministic_parameter_and_state_sweep();
  invalid_inputs_and_rollover();
  normalisation_half_boundaries_and_slew();
  return 0;
}
