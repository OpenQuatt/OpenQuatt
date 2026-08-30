from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
YAML = (ROOT / "openquatt" / "oq_thermal_actuator.yaml").read_text()
RUNTIME = (
    ROOT / "openquatt" / "includes" / "control" / "oq_thermal_actuator_runtime.h"
).read_text()


class ThermalActuatorRuntimeContractTest(unittest.TestCase):
    def test_yaml_is_a_compact_runtime_contract(self) -> None:
        self.assertLessEqual(len(YAML.splitlines()), 50)
        self.assertEqual(YAML.count("oq_thermal_actuator_runtime::runtime().tick("), 1)
        for implementation_detail in ("make_call()", "oq_incident_manager", "pick_allowed_level"):
            self.assertNotIn(implementation_detail, YAML)

    def test_safety_gates_keep_their_fail_closed_order(self) -> None:
        markers = (
            "const auto incident_guard",
            "const auto retained",
            "global_minimum_off_time_blocks_start(",
            "const int minimum_off_s",
            "const int expected_mode",
            "cycle.frequency.pick_allowed_level(",
            "apply_start_gate_before_active_write(",
            "apply_stop_notification_before_safe_write(",
            "this->write_level(is_hp1, command.physical_level",
        )
        positions = [RUNTIME.index(marker) for marker in markers]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("incident_guard.bypass_runtime_and_defrost_holds", RUNTIME)
        self.assertIn("cycle.cooling.confirmation_pending || cycle.cooling_stop_armed", RUNTIME)

    def test_lifecycle_state_is_owned_by_the_runtime(self) -> None:
        for state in (
            "last_defrost_seen_",
            "retained_levels_",
            "last_safe_stop_write_ms_",
            "last_manual_guard_status_",
        ):
            self.assertIn(state, RUNTIME)
            self.assertNotIn(state, YAML)

    def test_module_and_new_regressions_do_not_increase_lines(self) -> None:
        paths = (
            "openquatt/oq_thermal_actuator.yaml",
            "openquatt/includes/control/oq_thermal_actuator_logic.h",
            "openquatt/includes/control/oq_thermal_actuator_runtime.h",
            "tests/host/thermal_actuator_logic_test.cpp",
            "scripts/tests/test_thermal_actuator_runtime_contract.py",
        )
        self.assertLessEqual(sum(len((ROOT / path).read_text().splitlines()) for path in paths), 1011)


if __name__ == "__main__":
    unittest.main()
