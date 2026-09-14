#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_heating_curve_logic.h"
namespace {
using namespace oq_curve;
bool near(float actual, float expected) { return fabsf(actual - expected) < 0.001f; }
ControlProfileTuning tuning() { return control_profile("Balanced"); }
DemandInput input() { return {1000U, 0.5f, 35.0f, 34.0f, 20.0f, 20.5f, false, false, 0, 20}; }
DemandState active(int regime = 2) { return {true, 0, 0, false, false, regime}; }

void test_restart_matrix() {
  struct Case {
    bool below, deep, locked, fresh;
    float room;
    bool restart, room_block, lock_block;
    RestartReason reason;
  };
  const Case cases[] = {
      {true, false, false, true, 23.4f, false, true, false, RESTART_NONE},
      {true, true, true, true, 23.4f, false, true, false, RESTART_NONE},
      {false, false, true, true, 20.0f, true, false, false, RESTART_ROOM_DEMAND},
      {true, false, false, false, 23.4f, true, false, false, RESTART_WATER_BAND},
      {true, false, true, false, 19.0f, false, false, true, RESTART_NONE},
      {true, true, true, true, 20.5f, true, false, false, RESTART_WATER_BAND},
      {false, false, false, true, 23.4f, false, false, false, RESTART_NONE},
      {true, false, false, true, NAN, true, false, false, RESTART_WATER_BAND},
      {true, false, false, true, INFINITY, true, false, false, RESTART_WATER_BAND},
  };
  for (const auto& test : cases) {
    const auto out = evaluate_restart(test.below, test.deep, test.locked, test.fresh, test.room, 20.5f, 0.3f, 0.05f);
    assert(out.restart == test.restart && out.blocked_by_room == test.room_block &&
           out.blocked_by_off_lock == test.lock_block && out.reason == test.reason);
  }
}
void test_reset_and_power_cap() {
  float continuous = 12.0f;
  int demand = 8, pre = 7, regime = 2;
  bool heat = true, inhibit = true, room_block = true;
  uint32_t arm = 100, off = 200;
  reset_control_state(continuous, demand, pre, heat, arm, off, inhibit, room_block, regime);
  assert(isnan(continuous) && demand == 0 && pre == 0 && !heat && arm == 0 && off == 0 && !inhibit && !room_block &&
         regime == 0);
  const float capped = power_capped_demand_u(18.0f, 8, 20);
  assert(capped == 0.4f && lroundf(capped * 10.0f) == 4);
  assert(phase_target_power_w(true, capped, 10000.0f, 18000.0f) == 4000.0f);
  assert(phase_target_power_w(false, capped, 10000.0f, 18000.0f) == 7200.0f);
  assert(power_capped_demand_u(6.0f, 8, 20) == 0.3f && power_capped_demand_u(18.0f, 0, 20) == 0);
  assert(power_capped_demand_u(NAN, 8, 20) == 0 && power_capped_demand_u(INFINITY, 8, 20) == 0);
}
void test_oil_return_hold_and_rollover() {
  auto out = update_oil_return_hold(100U, true, 0, 180000U);
  assert(out.mask_active && out.hold_until_ms == 180100U);
  assert(update_oil_return_hold(180099U, false, out.hold_until_ms, 180000U).mask_active);
  out = update_oil_return_hold(180100U, false, out.hold_until_ms, 180000U);
  assert(!out.mask_active && out.hold_until_ms == 0);
  out = update_oil_return_hold(UINT32_MAX - 179999U, true, 0, 180000U);
  assert(out.mask_active && out.hold_until_ms == 1U);
  assert(update_oil_return_hold(0U, false, out.hold_until_ms, 180000U).mask_active);
  assert(!update_oil_return_hold(1U, false, out.hold_until_ms, 180000U).mask_active);
  assert(!update_oil_return_hold(UINT32_MAX - 10U, false, 0, 180000U).mask_active);
}
void test_invalid_inputs_fail_closed() {
  const float bad[] = {NAN, INFINITY, -INFINITY};
  float DemandInput::* fields[] = {&DemandInput::pid_output, &DemandInput::supply_target_c, &DemandInput::supply_c};
  for (float value : bad) {
    for (auto field : fields) {
      auto in = input();
      in.*field = value;
      const auto out = decide_demand(in, tuning(), active(1));
      assert(!out.valid && isnan(out.demand_continuous) && out.demand == 0 && !out.next.heat_request_active);
    }
  }
  auto invalid_tuning = tuning();
  invalid_tuning.recovery_enter_c = NAN;
  assert(!decide_demand(input(), invalid_tuning, active()).valid);
  auto invalid_max = input();
  invalid_max.demand_max = 0;
  assert(!decide_demand(invalid_max, tuning(), active()).valid);
}
void test_stop_confirmation_and_rollover() {
  auto in = input();
  in.pid_output = 0.05f;
  in.supply_c = 36.0f;
  auto out = decide_demand(in, tuning(), active());
  assert(out.valid && out.next.heat_request_active && out.next.stop_arm_ms == 1000U);
  auto armed = out.next;
  in.now_ms = 360999U;
  assert(decide_demand(in, tuning(), armed).next.heat_request_active);
  in.now_ms = 361000U;
  out = decide_demand(in, tuning(), armed);
  assert(!out.next.heat_request_active && out.next.off_since_ms == 361000U && out.stop_reason == STOP_NORMAL);
  armed = active();
  armed.stop_arm_ms = UINT32_MAX - 1000U;
  in.now_ms = 358999U;
  assert(!decide_demand(in, tuning(), armed).next.heat_request_active);
  in.oil_return_mask_active = true;
  assert(decide_demand(in, tuning(), active()).next.stop_arm_ms == 0);
  in.oil_return_mask_active = false;
  in.pid_output = 0;
  in.supply_c = 35.3f;
  armed = active();
  armed.stop_arm_ms = 1000U;
  in.now_ms = 91000U;
  out = decide_demand(in, tuning(), armed);
  assert(!out.next.heat_request_active && out.stop_reason == STOP_LOW_LOAD);
}
void test_restart_lock_and_regimes() {
  assert(control_profile("Comfort").off_reentry_min_ms == 300000U);
  assert(control_profile("Balanced").off_reentry_min_ms == 480000U);
  assert(control_profile("Stable").off_reentry_min_ms == 600000U);
  auto in = input();
  DemandState off{false, 0, 1000U, false, false, 0};
  in.now_ms = 2000U;
  auto out = decide_demand(in, tuning(), off);
  assert(!out.next.heat_request_active && out.next.restart_inhibit_active);
  off.off_since_ms = UINT32_MAX - 1000U;
  in.now_ms = 1000U;
  out = decide_demand(in, tuning(), off);
  assert(!out.next.heat_request_active && out.next.restart_inhibit_active);
  off.off_since_ms = 0;
  out = decide_demand(in, tuning(), off);
  assert(out.next.heat_request_active && out.next.regime_code == 1 && out.demand >= 2);
  in.supply_c = 35.0f;
  in.room_data_fresh = true;
  in.room_c = 20.0f;
  out = decide_demand(in, tuning(), {});
  assert(out.next.heat_request_active && out.restart_reason == RESTART_ROOM_DEMAND);
  in.supply_c = 33.5f;
  in.room_c = 23.0f;
  out = decide_demand(in, tuning(), {});
  assert(!out.next.heat_request_active && out.next.restart_blocked_by_room);
  in = input();
  in.pid_output = 0.2f;
  in.applied_total_level = 7;
  out = decide_demand(in, tuning(), active(0));
  assert(out.next.regime_code == 1 && out.demand == 8 && near(out.demand_continuous, 8.0f));
  in.supply_c = 34.85f;
  out = decide_demand(in, tuning(), active(1));
  assert(out.next.regime_code == 2 && out.demand == 3);
  in.supply_c = 34.0f;
  in.pid_output = 0.5f;
  out = decide_demand(in, tuning(), active(2));
  assert(out.next.regime_code == 1);
  in.oil_return_mask_active = true;
  assert(decide_demand(in, tuning(), active(2)).next.regime_code == 2);
  in.oil_return_mask_active = false;
  in.room_data_fresh = true;
  in.room_c = 21.0f;
  assert(decide_demand(in, tuning(), active(2)).next.regime_code == 2);
}
void test_maintain_caps() {
  struct Case {
    float error_c;
    int expected;
  };
  const Case cases[] = {{-0.1f, 1}, {0.05f, 2}, {0.15f, 3}, {0.30f, 4},
                        {0.45f, 5}, {0.60f, 6}, {0.80f, 8}, {1.00f, 20}};
  for (const auto& test : cases) {
    auto in = input();
    in.pid_output = 1.0f;
    in.supply_c = in.supply_target_c - test.error_c;
    in.oil_return_mask_active = true;
    const auto out = decide_demand(in, tuning(), active(2));
    assert(out.valid && out.next.regime_code == 2 && out.demand == test.expected &&
           near(out.demand_continuous, static_cast<float>(test.expected)));
  }
}
void test_outside_filter_target_and_cadence_rollover() {
  auto ema = update_outside_ema(1000U, 10.0f, 100.0f, {});
  assert(ema.next.initialized && near(ema.value_c, 10.0f));
  ema = update_outside_ema(11000U, 0.0f, 100.0f, ema.next);
  assert(near(ema.value_c, 9.090909f));
  OutsideEmaState rollover{10.0f, true, UINT32_MAX - 4999U};
  ema = update_outside_ema(5000U, 0.0f, 100.0f, rollover);
  assert(near(ema.value_c, 9.090909f));
  assert(!update_outside_ema(1000U, NAN, 100.0f, rollover).next.initialized);

  const std::array<CurvePoint, 6> points{{
      {-20.0f, 50.0f},
      {-10.0f, 45.0f},
      {0.0f, 40.0f},
      {5.0f, 37.5f},
      {10.0f, 35.0f},
      {15.0f, 30.0f},
  }};
  assert(near(supply_target(-5.0f, 40.0f, points, NAN, NAN, tuning(), 70.0f), 42.5f));
  assert(near(supply_target(-5.0f, 40.0f, points, 22.0f, 20.0f, tuning(), 70.0f), 40.5f));
  assert(near(supply_target(-5.0f, 40.0f, points, 22.0f, 20.0f, tuning(), 39.0f), 39.0f));
  assert(near(supply_target(NAN, 40.0f, points, NAN, NAN, tuning(), 70.0f), 40.0f));

  assert(cadence_due(100U, 0U, 30000U));
  assert(!cadence_due(1000U, UINT32_MAX - 1000U, 3000U));
  assert(cadence_due(2000U, UINT32_MAX - 1000U, 3000U));
  assert(elapsed_window_active(1000U, UINT32_MAX - 1000U, 3000U));
  assert(!elapsed_window_active(2000U, UINT32_MAX - 1000U, 3000U));
}
}  // namespace
int main() {
  test_restart_matrix();
  test_reset_and_power_cap();
  test_oil_return_hold_and_rollover();
  test_invalid_inputs_fail_closed();
  test_stop_confirmation_and_rollover();
  test_restart_lock_and_regimes();
  test_maintain_caps();
  test_outside_filter_target_and_cadence_rollover();
  return 0;
}
