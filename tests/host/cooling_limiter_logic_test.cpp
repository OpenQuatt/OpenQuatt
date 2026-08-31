#include <assert.h>

#include "../../openquatt/includes/control/oq_cooling_limiter_logic.h"

using namespace oq_cooling;

LimiterInput safe_conditions(uint32_t now_ms = 1000) {
  LimiterInput in;
  in.now_ms = now_ms;
  in.demand_max = in.capacity_demand_max = 3;
  in.previous_limited_demand = 1;
  in.buffer_gap_c = in.filtered_gap_c = 1.0f;
  return in;
}

void test_existing_water_and_minimum_off_contracts() {
  WaterCycleState cycle{true, 0.0f, WATER_STOP_NONE};
  assert(record_pi_zero_stop(1, 0, 0.05f, cycle));
  assert(!cycle.active && cycle.stop_reason_code == WATER_STOP_LIMITER && cycle.stop_buffer_gap_c == 0.05f);
  for (float gap : {0.20f, 0.60f, 1.04f}) assert(!water_restart_gap_recovered(cycle, gap, 1.0f));
  assert(water_restart_gap_recovered(cycle, 1.05f, 1.0f));
  for (WaterCycleState state : {WaterCycleState{}, {false, 0, WATER_STOP_PROJECTED_FLOOR}})
    assert(water_restart_gap_recovered(state, -1.0f, 1.0f));
  WaterCycleState dew{true, 0, WATER_STOP_DEW};
  assert(record_pi_zero_stop(1, 0, 0.1f, dew) && !dew.active && dew.stop_reason_code == WATER_STOP_DEW &&
         dew.stop_buffer_gap_c == 0.1f);
  WaterCycleState inactive, not_started{true, 0, WATER_STOP_NONE};
  assert(!record_pi_zero_stop(1, 0, 0, inactive) && !inactive.active);
  assert(!record_pi_zero_stop(0, 0, 0, not_started) && not_started.active);
  WaterCycleState still_running{true, 0, WATER_STOP_NONE};
  assert(!record_pi_zero_stop(1, 1, 0, still_running) && still_running.active);
  constexpr uint32_t off = 600000, stop = 900000;
  assert(global_minimum_off_time_remaining_ms(false, 1000, false, 0, false, off) == 0);
  assert(global_minimum_off_time_remaining_ms(true, 300000, false, 0, false, off) == 300000);
  assert(global_minimum_off_time_remaining_ms(true, off, false, 0, true, off) == 0);
  assert(global_minimum_off_time_remaining_ms(true, stop + 120000, true, stop, true, off) == 480000);
  assert(global_minimum_off_time_remaining_ms(true, stop + off, true, stop, true, off) == 0);
  assert(global_minimum_off_time_remaining_ms(true, 2000, true, UINT32_MAX - 3000, true, 10000) == 4999);
  assert(global_minimum_off_time_blocks_start(1, false, false, 0));
  assert(global_minimum_off_time_blocks_start(1, false, false, -1));
  assert(global_minimum_off_time_blocks_start(0, true, false, 0));
  assert(global_minimum_off_time_blocks_start(0, false, true, 0));
  assert(!global_minimum_off_time_blocks_start(1, true, true, 1));
  assert(!global_minimum_off_time_blocks_start(0, false, false, 0));
  assert(cooling_stop_is_planned(true, 3, 0) && !cooling_stop_is_planned(false, 3, 0) &&
         !cooling_stop_is_planned(true, 0, 0) && !cooling_stop_is_planned(true, 3, 2));
  assert(apply_hp2_before_hp1_for_cooling_handover(false, true) &&
         !apply_hp2_before_hp1_for_cooling_handover(true, false) &&
         !apply_hp2_before_hp1_for_cooling_handover(true, true) &&
         !apply_hp2_before_hp1_for_cooling_handover(false, false));
  uint32_t confirmed_at = 1234;
  bool seen = false;
  assert(!record_confirmed_cooling_stop(false, true, 2000, confirmed_at, seen) && confirmed_at == 1234 && !seen);
  assert(!record_confirmed_cooling_stop(true, false, 3000, confirmed_at, seen) && confirmed_at == 1234 && !seen);
  assert(record_confirmed_cooling_stop(true, true, 4000, confirmed_at, seen) && seen && confirmed_at == 4000);
  assert(cooling_minimum_off_stop_is_pending(true, false, WATER_STOP_LIMITER, true));
  assert(!cooling_minimum_off_stop_is_pending(true, false, WATER_STOP_REQUEST_CLEARED, true));
  assert(!cooling_minimum_off_stop_is_pending(true, false, WATER_STOP_LIMITER, false));
  assert(!cooling_minimum_off_stop_is_pending(true, true, WATER_STOP_LIMITER, true));
  assert(!cooling_minimum_off_stop_is_pending(false, false, WATER_STOP_LIMITER, true));
}
enum RestartFlags { MIN_OFF = 1, PENDING = 2, DEW = 4, FALLBACK = 8, OIL = 16, BRAKE = 32 };
WaterRestartDecision restart_case(float filtered, int stop_reason, int flags = 0, float dew_gap = NAN,
                                  float projected = 1, float rate = 0, int allowed = 1,
                                  int limiter_reason = REASON_FULL, float stop_gap = 0.05f) {
  LimiterInput in{
      1000, 3,        3,    1,       false, (bool)(flags & DEW), (bool)(flags & FALLBACK), (bool)(flags & OIL), false,
      1,    filtered, rate, dew_gap, 1};
  LimiterState limiter_state{false, (bool)(flags & BRAKE)};
  return evaluate_water_restart(flags & MIN_OFF, flags & PENDING, 1.0f, in, {allowed, limiter_reason, projected}, {},
                                limiter_state, {false, stop_gap, stop_reason});
}

void test_stop_reason_and_water_restart_matrix() {
  const int cases[][6] = {{false, false, false, false, false, WATER_STOP_NONE},
                          {true, true, true, true, true, WATER_STOP_REQUEST_CLEARED},
                          {true, false, true, true, true, WATER_STOP_FALLBACK},
                          {false, false, true, true, true, WATER_STOP_FLOW_PERMISSION_LOST},
                          {false, false, false, false, true, WATER_STOP_CORE_PERMISSION_LOST}};
  for (const auto& c : cases) assert(inactive_stop_reason(c[0], c[1], c[2], c[3], c[4]) == c[5]);
  assert(!restart_case(1.049f, WATER_STOP_LIMITER).state.active);
  assert(restart_case(1.05f, WATER_STOP_LIMITER).state.active);
  assert(!restart_case(1.05f, WATER_STOP_LIMITER, MIN_OFF | PENDING).state.active);
  assert(restart_case(-10, WATER_STOP_LIMITER, MIN_OFF).state.active);
  assert(!restart_case(10, WATER_STOP_NONE, DEW, 0.65f).state.active);
  assert(restart_case(10, WATER_STOP_NONE, DEW, 0.66f).state.active);
  assert(!restart_case(1, WATER_STOP_LIMITER, FALLBACK, NAN, 1, 0, 1, REASON_FULL, 0).state.active);
  assert(restart_case(1.001f, WATER_STOP_LIMITER, FALLBACK, NAN, 1, 0, 1, REASON_FULL, 0).state.active);
  assert(!restart_case(1, WATER_STOP_PROJECTED_FLOOR, BRAKE, NAN, 0.44f, -0.01f).state.active);
  assert(restart_case(1, WATER_STOP_PROJECTED_FLOOR, BRAKE, NAN, 0.44f, 0).state.active);
  assert(restart_case(-10, WATER_STOP_LIMITER, OIL | DEW, 0).state.active);
  assert(!restart_case(10, WATER_STOP_REQUEST_CLEARED, 0, NAN, 1, 0, 0).state.active);
  const auto escalated = restart_case(0.2f, WATER_STOP_PROJECTED_FLOOR, 0, NAN, 1, 0, 1, REASON_DEW_STOP, 0);
  assert(!escalated.state.active && escalated.reset_integral && escalated.state.stop_reason_code == WATER_STOP_DEW);
}

void test_oil_return_and_limiter_safety() {
  LimiterTuning limiter_tuning;
  OilReturnTuning tuning{1000, 100, 100, 0.30f, 0.15f, -0.05f};
  LimiterInput in = safe_conditions();
  in.dew_mode = true;
  in.dew_gap_c = 1.0f;
  in.filtered_gap_c = 0.60f;
  auto run = [&](OilReturnState& state, uint32_t now, bool control, bool oil) {
    in.now_ms = now;
    return update_oil_return({now, control, oil}, in, tuning, limiter_tuning, state);
  };
  OilReturnState state;
  assert(run(state, 100, true, true).mask_active && state.hold_since_ms == 100);
  assert(run(state, 200, true, false).mask_active && run(state, 299, true, false).mask_active);
  auto recovered = run(state, 300, true, false);
  assert(!recovered.mask_active && recovered.recovery_cap_active && recovered.reset_integral_and_dwell);
  assert(run(state, 399, true, false).recovery_cap_active && !run(state, 400, true, false).recovery_cap_active);
  struct RecoveryCase {
    bool fallback, releases;
    float dew, filtered, rate, horizon;
  };
  const RecoveryCase cases[] = {
      {false, true, .651f, 1, 0, 3},      {false, false, .649f, 1, 0, 3},       {true, true, NAN, .601f, .1f, 3},
      {true, false, NAN, .599f, .1f, 3},  {false, true, 1, .601f, 0, 3},        {false, false, 1, .599f, 0, 3},
      {false, true, 1, .001f, .099f, 10}, {false, false, 1, -.001f, .099f, 10}, {false, true, 1, -.099f, .24f, 3},
      {false, false, 1, -.101f, .24f, 3}, {false, true, 1, -.09f, .101f, 10},   {false, false, 1, -.09f, .099f, 10},
      {false, true, 1, 1, -.049f, 3},     {false, false, 1, 1, -.051f, 3}};
  for (const auto& c : cases) {
    in = {1000, 3, 3, 1, false, !c.fallback, c.fallback, false, false, 1, c.filtered, c.rate, c.dew, 0};
    limiter_tuning.projection_horizon_min = c.horizon;
    state = {};
    assert(run(state, 100, true, true).mask_active && run(state, 200, true, false).mask_active);
    const auto decision = run(state, 300, true, false);
    assert(decision.recovery_cap_active == c.releases && decision.mask_active != c.releases);
  }
  state = {};
  run(state, 500, true, true);
  run(state, 600, true, false);
  in.gap_rate_c_per_min = -0.10f;
  run(state, 650, true, false);
  assert(state.recovery_stable_since_ms == 0);
  in.gap_rate_c_per_min = 0;
  assert(run(state, 700, true, false).mask_active && run(state, 799, true, false).mask_active);
  assert(run(state, 800, true, false).recovery_cap_active);
  state = {};
  run(state, 1000, true, true);
  assert(!run(state, 2000, false, false).mask_active && state.hold_expired_pending);
  assert(run(state, 2100, true, false).recovery_cap_active);
  in.dew_mode = false;
  state = {};
  run(state, 3000, true, true);
  recovered = run(state, 3001, true, false);
  assert(!recovered.mask_active && !recovered.recovery_cap_active && state.hold_since_ms == 0);
  in.dew_mode = true;
  state = {};
  assert(run(state, UINT32_MAX - 999, true, true).mask_active);
  recovered = run(state, 0, true, false);
  assert(!recovered.mask_active && recovered.recovery_cap_active);
  // A millis()==0 start maps to UINT32_MAX: expiry may be 1 ms early, never more.
  state = {};
  assert(run(state, 0, true, true).mask_active && state.hold_since_ms == UINT32_MAX);
  assert(run(state, 998, true, false).mask_active && !run(state, 999, true, false).mask_active);
  limiter_tuning.limiter_stop_confirm_ms = 30;
  LimiterState limiter_state;
  in = safe_conditions();
  auto out = update_limiter(in, limiter_tuning, limiter_state);
  assert(out.allowed_max == 3 && out.reason_code == REASON_FULL);
  in.fallback_mode = true;
  in.filtered_gap_c = 0;
  out = update_limiter(in, limiter_tuning, limiter_state);
  assert(out.allowed_max == 0 && out.reason_code == REASON_FALLBACK_FLOOR);
  in = safe_conditions();
  in.dew_mode = in.oil_return_mask_active = true;
  in.dew_gap_c = 0;
  out = update_limiter(in, limiter_tuning, limiter_state);
  assert(out.allowed_max == 1 && out.reason_code == REASON_OIL_RETURN_HOLD);
  in.oil_return_mask_active = false;
  in.oil_return_recovery_cap_active = true;
  in.now_ms = 9;
  limiter_state.dew_stop_candidate_since_ms = UINT32_MAX - 20;
  out = update_limiter(in, limiter_tuning, limiter_state);
  assert(out.allowed_max == 0 && out.reason_code == REASON_DEW_STOP);
  limiter_tuning.capacity_min_hold_ms = limiter_tuning.capacity_recovery_stable_ms = 30;
  in = safe_conditions(UINT32_MAX - 20);
  in.dew_mode = true;
  in.dew_gap_c = 2;
  limiter_state = {false, false, 0, 0, 1, UINT32_MAX - 50};
  assert(update_limiter(in, limiter_tuning, limiter_state).allowed_max == 1);
  in.now_ms = 9;
  assert(update_limiter(in, limiter_tuning, limiter_state).allowed_max == 2);
}
int main() {
  test_existing_water_and_minimum_off_contracts();
  test_stop_reason_and_water_restart_matrix();
  test_oil_return_and_limiter_safety();
  return 0;
}
