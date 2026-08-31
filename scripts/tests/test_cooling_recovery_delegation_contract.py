from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[2]
YAML, CORE, DISPATCH, SAFETY_POLICY, ACTUATOR_RUNTIME, ACTUATOR_YAML, SUBSTITUTIONS, DEFROST_TEST = (
    (ROOT / path).read_text() for path in ("openquatt/oq_cooling_strategy.yaml", "openquatt/includes/control/oq_cooling_demand_logic.h", "openquatt/includes/control/oq_cooling_dispatch_logic.h", "openquatt/includes/control/oq_cooling_safety_policy.h", "openquatt/includes/control/oq_thermal_actuator_runtime.h", "openquatt/oq_thermal_actuator.yaml", "openquatt/oq_substitutions_common.yaml", "tests/host/oq_odu_compressor_levels_test.cpp"))
class CoolingRecoveryDelegationContractTest(unittest.TestCase):
    def test_yaml_delegates_and_live_safety_paths_stay_fail_closed(self) -> None:
        for marker in ("demand_runtime().tick", "DemandInput input", "input.sensor_valid = supply_valid && target_valid",
                       "dispatch_tick(dispatch)", "DispatchInput dispatch", "publish_cooling_limiter_event"): self.assertIn(marker, YAML)
        for marker in ("update_oil_return(", "evaluate_water_restart(", "oq_cooling_pid_integral", "oq_cooling_last_demand_change_ms",
                       "hp_can_take_cooling_start", "recent_owner", "id(oq_cooling_min_off_stop_pending) = false"): self.assertNotIn(marker, YAML)
        for marker in ("update_gap_filter(", "apply_demand_dwell(", "update_demand(", "effective_minimum_off_stop_pending", "isfinite(value)"): self.assertIn(marker, CORE)
        for marker in ("update_dispatch(", "recent_activity_owner(", "hp_minimum_off_blocks_start("): self.assertIn(marker, DISPATCH)
        self.assertIn("select_dew_point(", SAFETY_POLICY)
        self.assertNotIn("id(oq_cooling_min_off_stop_pending) = false", YAML)
        self.assertIn("id(oq_cooling_min_off_stop_pending) = false", ACTUATOR_RUNTIME)
        for marker in ("applied_cooling_[2]", "cooling_stop_confirmation_blocks_start(",
                       "next_applied_cooling("): self.assertIn(marker, ACTUATOR_RUNTIME)
        self.assertIn("(int) ${oq_cooling_minimum_off_min_s}", ACTUATOR_YAML)
        self.assertIn('oq_cooling_minimum_off_min_s: "240"', SUBSTITUTIONS)
        for marker in ("resolve_retained_level(true, true", "no_cooling_hold.control_level == 0", "no_cooling_hold.physical_level == 0"): self.assertIn(marker, DEFROST_TEST)
        paths = ("openquatt/oq_cooling_strategy.yaml", "openquatt/oq_cooling_safety.yaml", "openquatt/includes/control/oq_cooling_limiter_logic.h", "openquatt/includes/control/oq_cooling_demand_logic.h", "openquatt/includes/control/oq_cooling_dispatch_logic.h", "openquatt/includes/control/oq_cooling_safety_logic.h",
                 "openquatt/includes/control/oq_cooling_safety_policy.h", "tests/host/cooling_limiter_logic_test.cpp", "tests/host/cooling_demand_logic_test.cpp", "tests/host/cooling_dispatch_logic_test.cpp", "tests/host/cooling_safety_policy_test.cpp", "tests/host/thermal_request_logic_test.cpp")
        files = [ROOT / path for path in paths] + [Path(__file__)]
        self.assertLessEqual(sum(len(path.read_text().splitlines()) for path in files), 2911)
