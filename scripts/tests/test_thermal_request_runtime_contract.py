from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
YAML = (ROOT / "openquatt/oq_thermal_request_control.yaml").read_text()
LOGIC = (ROOT / "openquatt/includes/control/oq_thermal_request_logic.h").read_text()
RUNTIME = (ROOT / "openquatt/includes/control/oq_thermal_request_runtime.h").read_text()
HOST_TEST = (ROOT / "tests/host/thermal_request_logic_test.cpp").read_text()


class ThermalRequestRuntimeContractTest(unittest.TestCase):
    def test_yaml_is_a_compact_runtime_contract(self) -> None:
        self.assertIn("oq_thermal_request_runtime::runtime().tick", YAML)
        for implementation_marker in (
            "oq_frequency_runtime::capture()",
            "manual_mode_conflict",
            "min_runtime_hold_required(",
            "limit_slew",
        ):
            self.assertNotIn(implementation_marker, YAML)
        self.assertLessEqual(len(YAML.splitlines()), 560)

    def test_runtime_owns_arbitration_and_fail_closed_guards(self) -> None:
        for marker in (
            "run_manual_(",
            "run_inactive_(",
            "run_automatic_(",
            "id(oq_water_temp_hard_trip_active)",
            "id(oq_lowflow_fault_active)",
            "update_startup_event_(",
            "apply_minimum_runtime_(",
            "id(oq_actuator_hp1_req)",
            "id(oq_actuator_hp2_req)",
        ):
            self.assertIn(marker, RUNTIME)

    def test_pure_policy_has_host_regressions_and_reduces_total_lines(self) -> None:
        for marker in (
            "arbitrate_manual_request(",
            "select_strategy_request(",
            "limit_level_slew(",
            "limit_duo_to_one_change(",
            "deadline_pending(",
            "finite_value_at_least(",
            "minimum_runtime_seconds(",
        ):
            self.assertIn(marker, LOGIC)
            self.assertIn(marker, HOST_TEST)
        total = sum(len(source.splitlines()) for source in (YAML, LOGIC, RUNTIME, HOST_TEST))
        self.assertLessEqual(total, 1618)


if __name__ == "__main__":
    unittest.main()
