from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[2]
YAML, RUNTIME, CORE, DISPATCH, SAFETY_POLICY, ACTUATOR_RUNTIME, ACTUATOR_YAML, SUBSTITUTIONS = (
    (ROOT / path).read_text() for path in ("openquatt/oq_cooling_strategy.yaml", "openquatt/includes/control/oq_cooling_runtime.h", "openquatt/includes/control/oq_cooling_demand_logic.h", "openquatt/includes/control/oq_cooling_dispatch_logic.h", "openquatt/includes/control/oq_cooling_safety_policy.h", "openquatt/includes/control/oq_thermal_actuator_runtime.h", "openquatt/oq_thermal_actuator.yaml", "openquatt/oq_substitutions_common.yaml"))
class CoolingRecoveryDelegationContractTest(unittest.TestCase):
    def test_yaml_delegates_and_live_safety_paths_stay_fail_closed(self) -> None:
        for marker in ("demand_runtime().tick", "DemandInput input", "input.sensor_valid = supply_valid && target_valid",
                       "dispatch_tick(dispatch)", "DispatchInput dispatch", "publish_limiter_event_"): self.assertIn(marker, RUNTIME)
        for marker in ("oq_cooling_runtime::runtime().demand_tick", "oq_cooling_runtime::runtime().dispatch_tick"):
            self.assertIn(marker, YAML)
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

    def test_dispatch_verdict_is_copied_unconditionally(self) -> None:
        # Inhibited owners ride with start_blocked == false; gating the copy
        # on it would silently drop STARTUP_INHIBIT (issue #642).
        self.assertIn("id(oq_cooling_start_status_d_reason) = routing.start_status_reason;", RUNTIME)
        self.assertIn("id(oq_cooling_start_status_d_remaining_s) = routing.start_status_remaining_s;", RUNTIME)
        self.assertNotIn("if (routing.start_blocked)", RUNTIME)
