from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
YAML = (ROOT / "openquatt/oq_supervisory_controlmode.yaml").read_text()
RUNTIME = (ROOT / "openquatt/includes/control/oq_supervisory_state_runtime.h").read_text()


class SupervisoryStateRuntimeContractTest(unittest.TestCase):
    def test_yaml_delegates_to_runtime(self) -> None:
        self.assertEqual(YAML.count("oq_supervisory_state_runtime::runtime().tick"), 1)
        for implementation_marker in (
            "resolve_desired_cm",
            "evaluate_fallback(",
            "CM1 hold expired",
            "apply_sticky_pump_policy",
            "power_house_assist(",
        ):
            self.assertNotIn(implementation_marker, YAML)

    def test_runtime_owns_complete_supervisory_side_effects(self) -> None:
        for header in ("oq_control_mode_log_logic.h", "oq_supervisory_power_limiter_runtime.h",
                       "oq_supervisory_safety_runtime.h"):
            self.assertIn(f'#include "{header}"', RUNTIME)
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


    def test_flow_guard_covers_heating_preflow_and_compressor_wind_down(self) -> None:
        self.assertIn("const bool heating_flow_req = heating_req || heating_preflow_req;", RUNTIME)
        self.assertIn("const bool thermal_req = heating_flow_req || cooling_req || manual_hp_thermal_req;", RUNTIME)
        self.assertIn(
            "oq_supervisory_state::flow_guard_required(thermal_req, any_hp_compressor_active, actuator_request_active)",
            RUNTIME,
        )
        self.assertIn("{now_ms, flow_guard_required, min_flow_lph", RUNTIME)

    def test_dynamic_pmin_only_uses_servable_heat_pumps(self) -> None:
        self.assertIn("uint32_t hp_min_off_s;", RUNTIME)
        self.assertIn("${oq_hp_min_off_s},", YAML)
        self.assertIn("oq_hp_candidate::candidate_state", RUNTIME)
        self.assertIn("oq_hp_candidate::minimum_off_ready", RUNTIME)
        self.assertIn("if (!oq_hp_candidate::may_serve_candidate(candidate)) return;", RUNTIME)

    def test_low_load_cache_covers_minimum_off_for_both_heat_pumps(self) -> None:
        self.assertIn("hp1_incidents, hp1_lvl, id(oq_incident_manager).minimum_off_remaining_ms(1, now_ms)", RUNTIME)
        duo_cache = RUNTIME.index("id(oq_incident_manager).minimum_off_remaining_ms(2, now_ms)")
        self.assertGreater(RUNTIME.rfind("#if OQ_TOPOLOGY_DUO", 0, duo_cache), RUNTIME.rfind("#endif", 0, duo_cache))
        self.assertIn("hp2_incidents, hp2_lvl,", RUNTIME)
        self.assertEqual(RUNTIME.count("oq_supervisory_state::low_load_minimum_off_active("), 2)
        self.assertIn("oq_supervisory_state::low_load_cache_duration_ms(", RUNTIME)
        self.assertIn("tick.low_load_dyn_cache_max_s), minimum_off_ms, minimum_off_active,", RUNTIME)
        self.assertIn("any_hp_compressor_active),", RUNTIME)


    def test_cold_start_follows_available_heat_pumps(self) -> None:
        # Regression for #705: Duo cold start required both ODU outlet samples
        # even when one ODU was unavailable, wedging CM1 forever. Required must
        # follow the incident-manager start contract per HP, never topology.
        # Recovery before the first compressor start must open a new freshness
        # epoch: a false->true edge re-arms sampling and invalidates every
        # earlier sample, so a recovered HP cannot release on a pre-loss
        # measurement.
        self.assertIn("cold_start_required_set(", RUNTIME)
        self.assertIn("get_outputs(1).available_for_start", RUNTIME)
        self.assertIn("get_outputs(2).available_for_start", RUNTIME)
        self.assertIn("cold_start_required_added(", RUNTIME)
        self.assertIn("id(oq_cold_start_sample_after_ms) = flow_ok ? now_ms : 0;", RUNTIME)
        self.assertIn("probe_allowed && hp1_cold_start_required", RUNTIME)
        self.assertIn("probe_allowed && hp2_cold_start_required", RUNTIME)
        self.assertNotIn("ColdStartWaterSample{true,", RUNTIME)
        self.assertNotIn("cold_start_release_set", RUNTIME)


if __name__ == "__main__":
    unittest.main()
