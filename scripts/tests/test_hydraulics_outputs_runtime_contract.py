from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
FLOW_YAML = (ROOT / "openquatt/oq_flow_control.yaml").read_text()
THERMAL_YAML = (ROOT / "openquatt/oq_thermal_limits.yaml").read_text()
AUX_YAML = (ROOT / "openquatt/oq_aux_relay_control.yaml").read_text()
FLOW_RUNTIME = (ROOT / "openquatt/includes/control/oq_flow_runtime.h").read_text()
FLOW_LOGIC = (ROOT / "openquatt/includes/control/oq_flow_control_logic.h").read_text()
SUBSTITUTIONS = (ROOT / "openquatt/oq_substitutions_common.yaml").read_text()
THERMAL_RUNTIME = (ROOT / "openquatt/includes/control/oq_thermal_limits_runtime.h").read_text()
AUX_RUNTIME = (ROOT / "openquatt/includes/control/oq_aux_relay_runtime.h").read_text()


class HydraulicsOutputsRuntimeContractTest(unittest.TestCase):
    def test_yaml_is_a_compact_runtime_contract(self) -> None:
        self.assertLessEqual(len(FLOW_YAML.splitlines()), 280)
        self.assertLessEqual(len(THERMAL_YAML.splitlines()), 100)
        self.assertLessEqual(len(AUX_YAML.splitlines()), 210)
        for call in (
            "oq_flow_runtime::runtime().local_flow(",
            "oq_flow_runtime::runtime().flow_mismatch(",
            "oq_flow_runtime::runtime().service_tick()",
            "oq_flow_runtime::runtime().tick(",
        ):
            self.assertIn(call, FLOW_YAML)
        self.assertIn("oq_thermal_limits_runtime::runtime().tick()", THERMAL_YAML)
        self.assertIn("oq_aux_relay_runtime::runtime().tick()", AUX_YAML)
        self.assertEqual(AUX_YAML.count("oq_aux_relay_runtime::runtime().external_command("), 2)

    def test_stateful_decisions_live_in_cpp(self) -> None:
        for removed in (
            "id: oq_flow_i",
            "id: oq_flow_last_mode",
            "static std::string last_mode",
            "enum FlowExecMode",
        ):
            self.assertNotIn(removed, FLOW_YAML)
        self.assertNotIn("id: oq_water_temp_trip_since_ms", THERMAL_YAML)
        for removed in ("id: oq_aux_relay_on", "id: oq_aux_temp_gate_on", "static std::string last_status"):
            self.assertNotIn(removed, AUX_YAML)
        self.assertIn("oq_flow_control::update_pi", FLOW_RUNTIME)
        self.assertIn("oq_thermal_limits::update", THERMAL_RUNTIME)
        self.assertIn("oq_aux_relay::update", AUX_RUNTIME)

    def test_hidden_flow_pwm_entities_are_fixed_build_contracts(self) -> None:
        for retired_id in ("oq_flow_frost_pwm", "oq_flow_auto_start_pwm"):
            self.assertNotIn(f"id: {retired_id}", FLOW_YAML)
            self.assertNotIn(f"id({retired_id})", FLOW_RUNTIME)
        self.assertIn('oq_cm98_pump_ipwm: "800"', SUBSTITUTIONS)
        self.assertIn("config.frost_ipwm", FLOW_RUNTIME)
        self.assertIn("constexpr int kAutoStartFallbackIpwm = 440;", FLOW_LOGIC)

    def test_host_regressions_cover_failure_boundaries(self) -> None:
        flow_test = (ROOT / "tests/host/flow_control_logic_test.cpp").read_text()
        thermal_test = (ROOT / "tests/host/thermal_limits_logic_test.cpp").read_text()
        aux_test = (ROOT / "tests/host/aux_relay_logic_test.cpp").read_text()
        self.assertIn("test_flow_mismatch_hold_is_rollover_safe", flow_test)
        self.assertIn("test_nan_flow_failsafe", flow_test)
        self.assertIn("test_normal_cooling_and_manual_flow_setpoint_selection", flow_test)
        self.assertIn("test_cm3_trip_requires_hold_and_survives_rollover", thermal_test)
        self.assertIn("test_leaving_cm3_escalates_an_armed_trip_immediately", thermal_test)
        self.assertIn("test_missing_supply_fails_closed_only_after_trip", thermal_test)
        self.assertIn("test_heating_gate_hysteresis_and_missing_supply", aux_test)
        self.assertIn("test_external_control_is_explicit_and_retained", aux_test)
        self.assertIn("test_command_outside_external_control_cannot_override_owner", aux_test)

    def test_aux_runtime_is_only_compiled_for_its_hardware_owner(self) -> None:
        self.assertIn("#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q", AUX_RUNTIME)


if __name__ == "__main__":
    unittest.main()
