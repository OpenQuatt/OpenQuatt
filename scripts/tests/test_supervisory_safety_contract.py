from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SUPERVISOR = (ROOT / "openquatt/oq_supervisory_controlmode.yaml").read_text()
SUPERVISOR_RUNTIME = (ROOT / "openquatt/includes/control/oq_supervisory_state_runtime.h").read_text()
LOGIC = (ROOT / "openquatt/includes/control/oq_supervisory_safety_logic.h").read_text()
RUNTIME = (ROOT / "openquatt/includes/control/oq_supervisory_safety_runtime.h").read_text()
HOST_TEST = (ROOT / "tests/host/supervisory_safety_logic_test.cpp").read_text()


class SupervisorySafetyContractTest(unittest.TestCase):
    def test_yaml_delegates_flow_and_frost_decisions_once(self) -> None:
        self.assertEqual(SUPERVISOR_RUNTIME.count("oq_supervisory_safety_runtime::runtime().tick"), 1)
        for removed_inline_state in ("oq_lowflow_since_ms", "oq_flow_recover_since_ms", "frost_nan_grace_active"):
            self.assertNotIn(removed_inline_state, SUPERVISOR)
        self.assertIn("const bool flow_low = safety.flow_low;", SUPERVISOR_RUNTIME)
        self.assertIn("const bool frost = safety.frost_active;", SUPERVISOR_RUNTIME)

    def test_runtime_owns_side_effects_and_preserves_shared_contracts(self) -> None:
        for marker in (
            "id(oq_lowflow_fault_active) =",
            "id(oq_cm_frost_prev) =",
            "id(oq_lowflow_fault_active_bs)",
            "REASON_FLOW_TOO_LOW",
            "id(hp1_set_working_mode)",
            "id(hp1_compressor_level)",
            "#if OQ_TOPOLOGY_DUO",
            "id(hp2_set_working_mode)",
            "id(hp2_compressor_level)",
        ):
            self.assertIn(marker, RUNTIME)
        frost_global = SUPERVISOR.split("id: oq_cm_frost_prev", 1)[1].split("# Pre/Postflow", 1)[0]
        self.assertIn("restore_value: false", frost_global)
        self.assertIn("2881445393U", SUPERVISOR)
        self.assertIn("frost_initialized", LOGIC)
        self.assertIn("config.frost_off_c", LOGIC)
        self.assertIn("id: oq_lowflow_fault_active", SUPERVISOR)

    def test_logic_covers_failure_boundaries(self) -> None:
        for marker in (
            "std::isfinite(input.flow_lph)",
            "std::isfinite(input.outside_temperature_c)",
            "low_flow_fault_started",
            "flow_recovery_timing",
            "frost_nan_grace_active",
            "force_standby(",
            "seconds_to_ms(",
        ):
            self.assertIn(marker, LOGIC)
        for marker in (
            "low_flow_fault_started",
            "flow_recovery_timing",
            "frost_nan_grace_active",
            "force_standby(",
            "seconds_to_ms(",
        ):
            self.assertIn(marker, HOST_TEST)
        self.assertIn("UINT32_MAX - 20", HOST_TEST)
        self.assertIn("std::numeric_limits<float>::infinity()", HOST_TEST)

    def test_supervisory_yaml_is_smaller_than_the_electrical_only_stage(self) -> None:
        self.assertLess(len(SUPERVISOR.splitlines()), 2039)


if __name__ == "__main__":
    unittest.main()
