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
struct Candidate { bool valid=true; int hp1_level=8, hp2_level=8; float power_w=10000, error_w=5000; };
struct Available { bool available_for_start=true; };
struct Harness {
  oq_curve::DuoHoldState duo_holds_;
  bool demand_active=true, heat_phase=true, demand_rundown=false;
  bool lead_is_hp1=true, hp1_valve_defrost=false, hp2_valve_defrost=false;
  bool emergency=true, enable=true, disable=false;
  int hp1_fixed_level=0, hp2_fixed_level=0;
  struct Config { uint32_t loop_ms=30000; } config;
  struct Tuning {
    int dual_startup_grace_s=0, dual_emergency_hold_min=1;
    float dual_emergency_temp_err_c=3, dual_disable_temp_err_max_c=0;
  } tuning;
  // RESET_METHOD
  bool tick(uint32_t now_ms) {
    // ELIGIBILITY_BLOCK
    const int single_owner=1, previous_hp1=8, previous_hp2=8;
    const float owner_capacity_w=10000, duo_floor_w=3000;
    const float dispatch_u=disable ? 0.4f : 1.0f;
    const float temperature_error_c=emergency ? 4.0f : (disable ? -1.0f : 1.0f);
    const Available hp1_candidate, hp2_candidate;
    Candidate best_single, best_duo;
    best_duo.error_w=enable ? 0.0f : 6000.0f;
    const auto level_power_w=[](bool, int) { return 10000.0f; };
    // TIMING_BLOCK
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
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
  h.tick(6000);  // urgent dispatch five seconds later
  assert(!oq_dual_hp_enabled);
  assert(oq_dual_hp_enable_hold_elapsed_accum_min < 0.084f);
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min < 0.084f);
  for (uint32_t t=11000; t<61000; t+=5000) h.tick(t);
  assert(!oq_dual_hp_enabled);
  h.tick(61000);
  assert(oq_dual_hp_enabled);
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 1.0f);
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
void lost_conditions_restart_every_hold() {
  reset_globals(); Harness h;
  h.tick(1000); h.tick(56000);
  h.enable=false; h.emergency=false; h.tick(57000);
  assert(oq_dual_hp_enable_hold_elapsed_accum_min == 0.0f);
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
  h.enable=true; h.emergency=true; h.tick(58000); h.tick(117999);
  assert(!oq_dual_hp_enabled);
  h.tick(118000); assert(oq_dual_hp_enabled);
  h.enable=false; h.emergency=false; h.disable=true;
  h.tick(120000); h.tick(175000);
  assert(oq_dual_hp_enabled);
  h.disable=false; h.enable=true; h.tick(176000);
  assert(oq_dual_hp_disable_hold_elapsed_accum_min == 0.0f);
  h.disable=true; h.enable=false; h.tick(177000); h.tick(236999);
  assert(oq_dual_hp_enabled);
  h.tick(237000); assert(!oq_dual_hp_enabled);
}
void emergency_is_independent_and_demand_drop_resets() {
  reset_globals(); Harness h;
  h.enable=false; h.tick(1000); h.tick(31000);
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
  h.enable=true; h.tick(32000); h.tick(62000);
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
void emergency_and_defrost_keep_their_distinct_gates() {
  reset_globals(); Harness h;
  oq_dual_hp_enable_hold_min.state=3;
  assert(!h.tick(1000));
  assert(!h.tick(60999));
  assert(h.tick(61000)); // emergency override after its full minute, before the normal three-minute hold
  assert(!oq_dual_hp_enabled);
  h.emergency=false;
  assert(!h.tick(62000));
  assert(oq_dual_hp_emergency_hold_elapsed_accum_min == 0.0f);
  h.hp1_valve_defrost=h.hp2_valve_defrost=true;
  assert(h.tick(63000)); // simultaneous defrost remains immediate
  oq_dual_hp_enable_hold_min.state=1;
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
  lost_conditions_restart_every_hold();
  emergency_is_independent_and_demand_drop_resets();
  emergency_and_defrost_keep_their_distinct_gates();
  inactive_and_manual_modes_cannot_preload_holds();
  wrap_and_saturation();
}
'''.replace("    // TIMING_BLOCK", block).replace("  // RESET_METHOD", reset).replace("    // ELIGIBILITY_BLOCK", eligibility)
        with tempfile.TemporaryDirectory(prefix="oq-curve-hold-") as directory:
            source = Path(directory) / "curve_duo_hold.cpp"
            binary = Path(directory) / "curve_duo_hold"
            source.write_text(harness)
            run_process(compiler_command("-I", str(ROOT), str(source), "-o", str(binary)),
                        check=True, cwd=ROOT, timeout=COMPILE_TIMEOUT_SECONDS)
            run_process([str(binary)], check=True, timeout=RUN_TIMEOUT_SECONDS)


if __name__ == "__main__":
    unittest.main()
