from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
YAML = (ROOT / "openquatt" / "oq_thermal_actuator.yaml").read_text()
RUNTIME = (ROOT / "openquatt" / "includes" / "control" / "oq_thermal_actuator_runtime.h").read_text()


class ThermalActuatorRuntimeContractTest(unittest.TestCase):
    def test_yaml_is_a_compact_runtime_contract(self) -> None:
        self.assertLessEqual(len(YAML.splitlines()), 50)
        self.assertEqual(YAML.count("oq_thermal_actuator_runtime::runtime().tick("), 1)
        for detail in ("make_call()", "oq_incident_manager", "pick_allowed_level"):
            self.assertNotIn(detail, YAML)

    def test_runtime_delegates_ordered_safety_gates_to_the_tested_core(self) -> None:
        markers = ("const auto incident_guard", "const auto retained", "const uint32_t hp_rest_remaining_ms =",
                   "oq_thermal_actuator::decide_preflight(", "cycle.frequency.pick_allowed_level(",
                   "oq_thermal_actuator::valid_level_command(", "apply_start_gate_before_active_write(",
                   "apply_stop_notification_before_safe_write(", "this->write_level(is_hp1, command.physical_level")
        positions = [RUNTIME.index(marker) for marker in markers]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(RUNTIME.count("oq_thermal_actuator::decide_preflight("), 1)

    def test_lifecycle_state_is_owned_by_the_runtime(self) -> None:
        for state in ("last_defrost_seen_", "retained_levels_", "last_safe_stop_write_ms_", "last_manual_guard_status_"):
            self.assertIn(state, RUNTIME)
            self.assertNotIn(state, YAML)

    def test_every_active_modbus_write_invalidates_restart_credit_first(self) -> None:
        level_write = RUNTIME.index("call.perform();", RUNTIME.index("void write_level"))
        self.assertLess(RUNTIME.index("invalidate_restart_credit(1U)", RUNTIME.index("void write_level")), level_write)
        hp2_level_write = RUNTIME.index("call.perform();", level_write + 1)
        self.assertLess(RUNTIME.index("invalidate_restart_credit(2U)", level_write), hp2_level_write)
        mode_helper = RUNTIME.index("void write_mode_option_")
        hp1_mode_write = RUNTIME.index("call.perform();", mode_helper)
        self.assertLess(RUNTIME.index("invalidate_restart_credit(1U)", mode_helper), hp1_mode_write)
        hp2_mode_write = RUNTIME.index("call.perform();", hp1_mode_write + 1)
        self.assertLess(RUNTIME.index("invalidate_restart_credit(2U)", hp1_mode_write), hp2_mode_write)

    def test_complete_runtime_stack_stays_bounded(self) -> None:
        paths = ("openquatt/oq_thermal_actuator.yaml", "openquatt/includes/control/oq_thermal_actuator_logic.h",
                 "openquatt/includes/control/oq_thermal_actuator_runtime.h", "tests/host/thermal_actuator_logic_test.cpp",
                 "scripts/tests/test_thermal_actuator_runtime_contract.py", "scripts/tests/test_v2_compressor_level_contract.py",
                 "scripts/tests/test_compressor_frequency_policy_contract.py")
        # Includes the confirmed-rest reporting and its new communication-gap regression cases.
        self.assertLessEqual(sum(len((ROOT / path).read_text().splitlines()) for path in paths), 1340)


if __name__ == "__main__":
    unittest.main()
