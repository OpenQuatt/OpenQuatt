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

    def test_frequency_limit_event_requires_a_pre_policy_request(self) -> None:
        start = RUNTIME.index("void publish_frequency_limit_block_")
        end = RUNTIME.index("int apply_level_", start)
        diagnostic = RUNTIME[start:end]
        self.assertIn("const int requested = is_hp1 ? id(oq_request_hp1_level) : id(oq_request_hp2_level);", diagnostic)
        self.assertIn("requested > 0", diagnostic)

    def test_complete_runtime_stack_stays_bounded(self) -> None:
        paths = ("openquatt/oq_thermal_actuator.yaml", "openquatt/includes/control/oq_thermal_actuator_logic.h",
                 "openquatt/includes/control/oq_thermal_actuator_runtime.h", "tests/host/thermal_actuator_logic_test.cpp",
                 "openquatt/includes/control/oq_compressor_start_limit.h", "tests/host/compressor_start_limit_test.cpp",
                 "scripts/tests/test_thermal_actuator_runtime_contract.py", "scripts/tests/test_v2_compressor_level_contract.py",
                 "scripts/tests/test_compressor_frequency_policy_contract.py")
        # Includes restart credit, frequency diagnosis and the bounded start quota with regression coverage.
        self.assertLessEqual(sum(len((ROOT / path).read_text().splitlines()) for path in paths), 1600)

    def test_start_quota_covers_retained_writes_and_final_transitions(self) -> None:
        actuator = RUNTIME[RUNTIME.index("int apply_level_"):RUNTIME.index("int previous_applied_")]
        remaining = actuator.index("start_limits_[is_hp1 ? 0 : 1].remaining_ms")
        retained = actuator.index("const auto retained")
        preflight = actuator.index("oq_thermal_actuator::decide_preflight(")
        self.assertLess(remaining, retained)
        self.assertLess(retained, preflight)
        self.assertIn("oq_thermal_actuator::may_retain_command(previous, incident_guard.bypass_runtime_and_defrost_holds)", actuator)
        self.assertIn("if (!may_retain) this->clear_retained_level(is_hp1);", actuator)
        self.assertIn("may_retain ? this->retained_level(is_hp1, cycle.frequency) : oq_odu::RetainedLevel{}", actuator[retained:preflight])
        self.assertIn("cooling_start_blocked, start_limit_remaining_ms)", actuator)
        self.assertLess(preflight, actuator.index("apply_active_mode_hold("))
        self.assertLess(preflight, actuator.index("apply_start_gate_before_active_write("))
        self.assertIn("for (auto& limit : this->start_limits_) limit.expire(config.now_ms);", RUNTIME)
        self.assertEqual(RUNTIME.count(".record_transition(previous, new_level, now_ms)"), 1)
        transition = RUNTIME[RUNTIME.index("void record_transition("):RUNTIME.index("private:")]
        self.assertLess(transition.index(".record_transition("), transition.index("previous = new_level;"))


if __name__ == "__main__":
    unittest.main()
