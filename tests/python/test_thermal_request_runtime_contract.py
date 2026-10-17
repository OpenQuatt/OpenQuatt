from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
YAML = (ROOT / "openquatt/oq_thermal_request_control.yaml").read_text()
RUNTIME = (ROOT / "openquatt/includes/control/oq_thermal_request_runtime.h").read_text()


class ThermalRequestRuntimeContractTest(unittest.TestCase):
    def test_yaml_delegates_to_runtime(self) -> None:
        self.assertIn("oq_thermal_request_runtime::runtime().tick", YAML)
        for implementation_marker in (
            "oq_frequency_runtime::capture()",
            "manual_mode_conflict",
            "min_runtime_hold_required(",
            "limit_slew",
        ):
            self.assertNotIn(implementation_marker, YAML)

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



if __name__ == "__main__":
    unittest.main()
