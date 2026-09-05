from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
YAML = (ROOT / "openquatt/oq_supervisory_controlmode.yaml").read_text()
LOGIC = (ROOT / "openquatt/includes/control/oq_supervisory_state_logic.h").read_text()
RUNTIME = (ROOT / "openquatt/includes/control/oq_supervisory_state_runtime.h").read_text()
PROBE = (ROOT / "openquatt/includes/control/oq_cold_start_probe.h").read_text()
HOST_TEST = (ROOT / "tests/host/supervisory_state_logic_test.cpp").read_text()


class SupervisoryStateRuntimeContractTest(unittest.TestCase):
    def test_yaml_is_a_compact_runtime_contract(self) -> None:
        self.assertEqual(YAML.count("oq_supervisory_state_runtime::runtime().tick"), 1)
        for implementation_marker in (
            "resolve_desired_cm",
            "evaluate_fallback(",
            "CM1 hold expired",
            "apply_sticky_pump_policy",
            "power_house_assist(",
        ):
            self.assertNotIn(implementation_marker, YAML)
        self.assertLessEqual(len(YAML.splitlines()), 700)

    def test_runtime_owns_complete_supervisory_side_effects(self) -> None:
        self.assertIn('#include "../performance/hp_perf_frequency.h"', RUNTIME)
        for marker in (
            "oq_supervisory_power_runtime::runtime().tick",
            "oq_supervisory_safety_runtime::runtime().tick",
            "resolve_desired_cm",
            "id(oq_control_mode).publish_state",
            "id(oq_boiler_command_valid) = false",
            "id(oq_cooling_energy_session_active)",
            "id(hp1_low_noise_mode)",
            "id(hp1_set_pump_mode)",
            "shutdown_boiler_transport_",
            "#if OQ_TOPOLOGY_DUO",
        ):
            self.assertIn(marker, RUNTIME)

    def test_stateful_policies_have_host_regressions(self) -> None:
        for marker in (
            "update_low_load(",
            "confirm_request(",
            "update_idle_exit(",
            "update_override(",
            "silent_window(",
            "update_sticky_pump(",
            "seconds_to_ms(",
            "window_active(",
        ):
            self.assertIn(marker, LOGIC)
            self.assertIn(marker, HOST_TEST)
        self.assertIn("UINT32_MAX - 20", HOST_TEST)

    def test_flow_guard_covers_heating_preflow_and_compressor_wind_down(self) -> None:
        self.assertIn("const bool heating_flow_req = heating_req || heating_preflow_req;", RUNTIME)
        self.assertIn("const bool thermal_req = heating_flow_req || cooling_req || manual_hp_thermal_req;", RUNTIME)
        self.assertIn(
            "oq_supervisory_state::flow_guard_required(thermal_req, any_hp_compressor_active, actuator_request_active)",
            RUNTIME,
        )
        self.assertIn("{now_ms, flow_guard_required, min_flow_lph", RUNTIME)

    def test_production_sources_remain_bounded(self) -> None:
        # Include the bounded Modbus reader added for first-start water samples.
        total = sum(len(source.splitlines()) for source in (YAML, LOGIC, RUNTIME, PROBE))
        self.assertLessEqual(total, 2250)


if __name__ == "__main__":
    unittest.main()
