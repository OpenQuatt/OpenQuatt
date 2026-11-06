"""Execute the production curve hold block with a fake monotonic clock."""

from pathlib import Path
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]


class HeatingCurveDuoHoldRuntimeTest(unittest.TestCase):
    def test_first_urgent_delayed_reset_and_wrapping_ticks(self) -> None:
        runtime = (ROOT / "openquatt/includes/control/oq_heating_curve_runtime.h").read_text()
        end = runtime.index(";", runtime.index("    const bool dual_enabled =")) + 1
        block = runtime[runtime.index("    const int dual_on_hold_min ="):end]
        scale_start = runtime.index("    const bool both_applied =")
        scale_end = runtime.index(";", runtime.index("    const float target_power_w =", scale_start)) + 1
        scale = runtime[scale_start:scale_end]
        reset = re.search(r"  void reset_dual_holds_\(\).*?\n  }", runtime, re.S).group()
        eligibility_start = runtime.index("    const bool hold_allowed =")
        eligibility_last = "    if (!hold_allowed) this->reset_dual_holds_();"
        eligibility_end = runtime.index(eligibility_last, eligibility_start) + len(eligibility_last)
        eligibility = runtime[eligibility_start:eligibility_end]
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include "openquatt/includes/control/oq_heating_curve_duo_floor_logic.h"
#include "openquatt/includes/control/oq_heating_curve_duo_hold_logic.h"
#include "openquatt/includes/control/oq_heating_curve_logic.h"
#include "openquatt/includes/control/oq_thermal_request_logic.h"
#define id(x) x
struct Number { float state=1; } oq_dual_hp_enable_hold_min, oq_dual_hp_disable_hold_min;
bool oq_dual_hp_enabled=false;
int oq_control_mode_code=2;
float oq_dual_hp_enable_hold_elapsed_accum_min=0;
float oq_dual_hp_disable_hold_elapsed_accum_min=0;
float oq_dual_hp_emergency_hold_elapsed_accum_min=0;
uint32_t hp1_last_start_ms=0, hp2_last_start_ms=0;
struct Available { bool available_for_start=true; };
struct Harness {
  oq_curve::DuoHoldState duo_holds_;
  bool demand_active=true, heat_phase=false, demand_rundown=false;
  bool lead_is_hp1=true, hp1_valve_defrost=false, hp2_valve_defrost=false, hp1_defrost=false;
  bool large_gap=true, extra_power_available=true, disable=false;
  int hp1_fixed_level=0, hp2_fixed_level=0, previous_hp1=8, previous_hp2=0;
  int owner_max=8;
  float dispatch_cap=1.0f, continuous_cap=1.0f, high_gap_c=4.0f;
  float applied_duo_power_w=NAN, selected_duo_power_w=0;
  struct Tuning {
    int dual_startup_grace_s=0, dual_emergency_hold_min=1;
    float dual_emergency_temp_err_c=1.5f, dual_disable_temp_err_max_c=0.5f;
  } tuning;
  // RESET_METHOD
  bool tick(uint32_t now_ms) {
    // ELIGIBILITY_BLOCK
    const int single_owner=1, hp1_pair_max=1, hp2_pair_max=1;
    const struct { int demand_max_f=20; } config;
    const float owner_capacity_w=10000, duo_floor_w=3000, duo_capacity_w=15000;
    const float dispatch_u=disable ? 0.4f : dispatch_cap;
    const float demand_u=std::min(dispatch_u,continuous_cap);
    const float temperature_error_c=disable ? -1.0f : (large_gap ? high_gap_c : 0.3f);
    const bool defrost_active=hp1_defrost || hp1_valve_defrost || hp2_valve_defrost;
    // POWER_SCALE_BLOCK
    const Available hp1_candidate, hp2_candidate;
    const float single_w=std::min(target_power_w, owner_capacity_w);
    const oq_curve::DispatchCandidate best_single{true, 8, 0, single_w, target_power_w-single_w, 1, 0};
    const auto candidate=[&](int first, int second, float target_w) {
      const bool is_applied_pair=first==previous_hp1 && second==previous_hp2 && std::isfinite(applied_duo_power_w);
      const float power_w=is_applied_pair ? applied_duo_power_w : (extra_power_available ? target_w : owner_capacity_w);
      return oq_curve::DispatchCandidate{true, 7, 7, power_w, std::fabs(power_w-target_w), 2, 0};
    };
    const auto level_power_w=[](bool, int) { return 10000.0f; };
    // TIMING_BLOCK
    selected_duo_power_w = dual_enabled ? best_duo.power_w : 0.0f;
    if (dual_enabled && heat_phase && large_gap && !defrost_active && dispatch_u>=0.95f)
      assert(best_duo.power_w>owner_capacity_w);
    return dual_enabled;
  }
};
void reset_globals() {
  oq_control_mode_code=2;
  oq_request::reset_dual_hold_state(oq_dual_hp_enabled, oq_dual_hp_enable_hold_elapsed_accum_min,
      oq_dual_hp_disable_hold_elapsed_accum_min, oq_dual_hp_emergency_hold_elapsed_accum_min);
}
void no_credit_on_first_or_urgent_tick() {
  reset_globals(); Harness h;
  h.tick(1000);
  assert(oq_dual_hp_enable_hold_elapsed_accum_min == 0.0f);
  h.tick(6000);  // urgent dispatch five seconds later
  assert(!oq_dual_hp_enabled);
  assert(oq_dual_hp_enable_hold_elapsed_accum_min < 0.084f);
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
  for (uint32_t t=11000; t<61000; t+=5000) h.tick(t);
  assert(!oq_dual_hp_enabled);
  h.tick(61000);
  assert(oq_dual_hp_enabled);
  assert(oq_dual_hp_enable_hold_elapsed_accum_min == 1.0f);
}
void delayed_ticks_credit_elapsed_time_and_zero_is_valid() {
  reset_globals(); Harness h;
  h.tick(0);
  h.tick(59999);
  assert(!oq_dual_hp_enabled);
  h.tick(60000);
  assert(oq_dual_hp_enabled);
  reset_globals(); h=Harness{};
  h.tick(1000);
  h.tick(91000); // scheduler paused longer than the configured dispatch interval
  assert(oq_dual_hp_enabled);
  assert(oq_dual_hp_enable_hold_elapsed_accum_min == 1.5f);
}
void lost_conditions_restart_the_normal_holds() {
  reset_globals(); Harness h;
  h.tick(1000); h.tick(56000);
  h.extra_power_available=false; h.tick(57000);
  assert(oq_dual_hp_enable_hold_elapsed_accum_min == 0.0f);
  h.extra_power_available=true; h.tick(58000); h.tick(117999);
  assert(!oq_dual_hp_enabled);
  h.tick(118000); assert(oq_dual_hp_enabled);
  h.disable=true;
  h.tick(120000); h.tick(175000);
  assert(oq_dual_hp_enabled);
  h.disable=false; h.tick(176000);
  assert(oq_dual_hp_disable_hold_elapsed_accum_min == 0.0f);
  h.disable=true; h.tick(177000); h.tick(236999);
  assert(oq_dual_hp_enabled);
  h.tick(237000); assert(!oq_dual_hp_enabled);
}
void recovery_needs_extra_power_and_demand_drop_resets_the_hold() {
  reset_globals(); Harness h;
  h.heat_phase=true;
  h.extra_power_available=false; h.tick(1000); h.tick(31000);
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
  h.extra_power_available=true; h.tick(32000); h.tick(62000);
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.5f);
  h.demand_active=false; h.tick(63000);
  assert(!oq_dual_hp_enabled);
  assert(oq_dual_hp_enable_hold_elapsed_accum_min == 0.0f);
  assert(oq_dual_hp_disable_hold_elapsed_accum_min == 0.0f);
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
  h.demand_active=true; h.tick(64000); h.tick(123999);
  assert(!oq_dual_hp_enabled);
  h.tick(124000); assert(oq_dual_hp_enabled);
}
void recovery_latches_without_waiting_for_the_normal_enable_hold() {
  reset_globals(); Harness h;
  oq_dual_hp_enable_hold_min.state=3;
  h.heat_phase=true;
  assert(!h.tick(1000));
  assert(!h.tick(60999));
  assert(h.tick(61000));
  assert(oq_dual_hp_enabled);
  h.previous_hp1=h.previous_hp2=7; // the Duo split takes the owner below its single limit
  assert(h.tick(62000));
  h.large_gap=false;
  assert(h.tick(63000));
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
  h.heat_phase=false;
  h.disable=true;
  assert(h.tick(64000));
  assert(!h.tick(124000)); // the normal Duo off hold still applies
  oq_dual_hp_enable_hold_min.state=1;
}
void simultaneous_defrost_still_retains_duo() {
  reset_globals(); Harness h;
  h.previous_hp2=8;
  h.hp1_valve_defrost=h.hp2_valve_defrost=true;
  assert(h.tick(63000));
}
void short_defrost_does_not_stage_a_second_pump() {
  for (bool valve : {false, true}) {
    reset_globals(); Harness h;
    h.heat_phase=true;
    h.large_gap=false;
    assert(!h.tick(1000)); // no extra capacity needed
    h.large_gap=true;
    assert(!h.tick(2000)); // a dip before defrost is reported
    assert(!h.tick(57000)); // almost a full hold
    h.hp1_valve_defrost=valve;
    h.hp1_defrost=!valve;
    assert(!h.tick(58000));
    assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
    assert(!h.tick(118000)); // enough elapsed time, but still defrosting
    h.hp1_valve_defrost=h.hp1_defrost=false;
    assert(!h.tick(119000)); // short recovery after defrost
    assert(!h.tick(149000));
    h.large_gap=false;
    assert(!h.tick(150000)); // the water recovers on one pump
    assert(!h.tick(211000));
  }
}
void recovery_waits_for_the_owner_and_respects_the_demand_cap() {
  reset_globals(); Harness h;
  h.heat_phase=true;
  h.previous_hp1=7;
  assert(!h.tick(1000));
  assert(!h.tick(62000)); // owner is still ramping toward level 8
  h.previous_hp1=8;
  h.dispatch_cap=0.90f;
  assert(!h.tick(63000));
  assert(!h.tick(124000)); // effective demand is power-capped
  h.dispatch_cap=0.95f;
  h.owner_max=9;
  assert(!h.tick(125000));
  assert(!h.tick(185000)); // the best single candidate is below the permitted maximum
  h.owner_max=8;
  h.continuous_cap=0.93f;
  assert(!h.tick(186000));
  assert(!h.tick(246000)); // discrete demand rounded up to 19/20
  h.continuous_cap=0.95f;
  assert(!h.tick(247000));
  assert(h.tick(307000));
}
void recovery_can_start_at_the_profile_temperature_threshold() {
  reset_globals();
  Harness h;
  h.heat_phase = true;
  h.high_gap_c = h.tuning.dual_emergency_temp_err_c;
  assert(!h.tick(1000));
  assert(!h.tick(60999));
  assert(h.tick(61000));
}
void defrost_clears_a_latched_start_of_an_idle_peer() {
  reset_globals();
  Harness h;
  h.heat_phase = true;
  oq_dual_hp_enabled = true;  // The actuator has yet to start HP2.
  h.hp1_defrost = true;
  assert(!h.tick(1000));
  assert(!oq_dual_hp_enabled);
  h.hp1_defrost = false;
  h.large_gap = false;
  assert(!h.tick(31000));
  assert(!h.tick(91000));
}
void running_duo_can_use_full_capacity_near_the_recovery_exit() {
  reset_globals();
  Harness h;
  h.heat_phase = true;
  h.large_gap = false;  // 0.30 °C below target, just outside the Balanced exit band.
  h.previous_hp1 = h.previous_hp2 = 7;
  oq_dual_hp_enabled = true;
  assert(h.tick(1000));
  assert(h.selected_duo_power_w == 15000.0f);
}
void running_duo_waits_for_maintain_before_shedding() {
  reset_globals();
  Harness h;
  h.heat_phase = true;
  h.large_gap = false;
  h.dispatch_cap = 0.60f;
  h.previous_hp1 = h.previous_hp2 = 7;
  oq_dual_hp_enabled = true;
  assert(h.tick(1000));
  assert(h.tick(61000));
  assert(oq_dual_hp_disable_hold_elapsed_accum_min == 0.0f);
  h.heat_phase = false;
  h.dispatch_cap = 0.50f;
  assert(h.tick(62000));
  assert(!h.tick(122000));
}
void duo_off_hold_waits_for_the_applied_pair_to_modulate_down() {
  reset_globals();
  Harness h;
  h.previous_hp1 = h.previous_hp2 = 7;
  h.disable = true;
  h.applied_duo_power_w = 11000.0f;
  oq_dual_hp_enabled = true;
  assert(h.tick(1000));
  assert(h.tick(61000));
  assert(oq_dual_hp_disable_hold_elapsed_accum_min == 0.0f);
  h.disable = false;
  h.large_gap = false;
  h.dispatch_cap = 0.30f;
  h.applied_duo_power_w = 5900.0f;  // Below the cap-band top despite a lower current demand step.
  assert(h.tick(62000));
  assert(h.tick(121999));
  assert(!h.tick(122000));
}
void maintain_keeps_duo_at_target_then_sheds_once() {
  reset_globals();
  Harness h;
  oq_dual_hp_enabled = true;
  oq_dual_hp_disable_hold_min.state = 5;
  h.large_gap = false;
  h.previous_hp1 = h.previous_hp2 = 7;
  oq_curve::DemandState demand_state;
  demand_state.heat_request_active = true;
  demand_state.regime_code = 2;
  const auto tick = [&](uint32_t now, float pid, float gap) {
    oq_curve::DemandInput in;
    in.now_ms = now;
    in.pid_output = pid;
    in.supply_target_c = 37.0f;
    in.supply_c = in.supply_target_c - gap;
    in.demand_max = 20;
    in.duo_running = h.previous_hp1 > 0 && h.previous_hp2 > 0;
    const auto demand = oq_curve::decide_demand(in, oq_curve::control_profile("Balanced"), demand_state);
    assert(demand.valid && demand.next.regime_code == 2);
    demand_state = demand.next;
    h.dispatch_cap = demand.demand_continuous / 20.0f;
    const bool duo = h.tick(now);
    h.previous_hp2 = duo ? 7 : 0;
    return duo;
  };
  for (uint32_t now = 1000; now <= 601000; now += 30000)
    assert(tick(now, 0.70f, 0.0f));  // A flat setpoint must not pre-arm the off hold.
  for (uint32_t now = 631000; now <= 931000; now += 30000)
    assert(tick(now, 0.70f, now % 60000 < 30000 ? -0.04f : 0.04f));
  constexpr uint32_t low_demand_ms = 961000;
  for (uint32_t elapsed = 0; elapsed < 300000; elapsed += 30000)
    assert(tick(low_demand_ms + elapsed, 0.30f, 0.0f));
  assert(!tick(low_demand_ms + 300000, 0.30f, 0.0f));
  for (uint32_t elapsed = 330000; elapsed <= 3900000; elapsed += 30000)
    assert(!tick(low_demand_ms + elapsed, 0.30f, elapsed % 60000 == 0 ? -0.04f : 0.04f));
  oq_dual_hp_disable_hold_min.state = 1;
}
void inactive_and_manual_modes_cannot_preload_holds() {
  for (int mode : {0, 1, 4, 100}) {
    reset_globals(); Harness h;
    h.tick(1000); h.tick(56000);
    oq_control_mode_code=mode; h.tick(57000);
    assert(!oq_dual_hp_enabled);
    assert(oq_dual_hp_enable_hold_elapsed_accum_min == 0.0f);
    assert(oq_dual_hp_disable_hold_elapsed_accum_min == 0.0f);
    assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
    oq_control_mode_code=3; h.tick(58000); h.tick(117999);
    assert(!oq_dual_hp_enabled);
    h.tick(118000); assert(oq_dual_hp_enabled);
  }
}
void wrap_and_saturation() {
  reset_globals(); Harness h;
  constexpr uint32_t start=UINT32_MAX-30000;
  h.tick(start); h.tick(start+59999); assert(!oq_dual_hp_enabled);
  h.tick(start+60000); assert(oq_dual_hp_enabled);
  oq_curve::ContinuousHold long_hold;
  assert(!long_hold.ready(0));
  assert(long_hold.update(true, 0) == 0);
  assert(long_hold.update(true, UINT32_MAX-1) == UINT32_MAX-1);
  assert(long_hold.update(true, 1) == UINT32_MAX);
  assert(long_hold.update(true, 2) == UINT32_MAX);
  assert(long_hold.update(false, 3) == 0);
  assert(!long_hold.ready(1));
}
int main() {
  no_credit_on_first_or_urgent_tick();
  delayed_ticks_credit_elapsed_time_and_zero_is_valid();
  lost_conditions_restart_the_normal_holds();
  recovery_needs_extra_power_and_demand_drop_resets_the_hold();
  recovery_latches_without_waiting_for_the_normal_enable_hold();
  simultaneous_defrost_still_retains_duo();
  short_defrost_does_not_stage_a_second_pump();
  recovery_waits_for_the_owner_and_respects_the_demand_cap();
  recovery_can_start_at_the_profile_temperature_threshold();
  defrost_clears_a_latched_start_of_an_idle_peer();
  running_duo_can_use_full_capacity_near_the_recovery_exit();
  running_duo_waits_for_maintain_before_shedding();
  duo_off_hold_waits_for_the_applied_pair_to_modulate_down();
  maintain_keeps_duo_at_target_then_sheds_once();
  inactive_and_manual_modes_cannot_preload_holds();
  wrap_and_saturation();
}
'''.replace("    // POWER_SCALE_BLOCK", scale).replace("    // TIMING_BLOCK", block).replace("  // RESET_METHOD", reset).replace("    // ELIGIBILITY_BLOCK", eligibility)
        with tempfile.TemporaryDirectory(prefix="oq-curve-hold-") as directory:
            source = Path(directory) / "curve_duo_hold.cpp"
            binary = Path(directory) / "curve_duo_hold"
            source.write_text(harness)
            run_process(compiler_command("-I", str(ROOT), str(source), "-o", str(binary)),
                        check=True, cwd=ROOT, timeout=COMPILE_TIMEOUT_SECONDS)
            run_process([str(binary)], check=True, timeout=RUN_TIMEOUT_SECONDS)


if __name__ == "__main__":
    unittest.main()
