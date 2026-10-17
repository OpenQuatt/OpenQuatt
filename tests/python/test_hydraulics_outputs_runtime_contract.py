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
    def test_yaml_delegates_to_runtime(self) -> None:
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


    def test_aux_runtime_is_only_compiled_for_its_hardware_owner(self) -> None:
        self.assertIn("#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q", AUX_RUNTIME)


if __name__ == "__main__":
    unittest.main()
