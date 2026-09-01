from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE_YAML = (ROOT / "openquatt/oq_sensor_sources.yaml").read_text()
API_YAML = (ROOT / "openquatt/oq_api_ingress.yaml").read_text()
SOURCE_RUNTIME = (ROOT / "openquatt/includes/control/oq_sensor_source_runtime.h").read_text()
API_RUNTIME = (ROOT / "openquatt/includes/control/oq_api_ingress_runtime.h").read_text()
SOURCE_LOGIC = (ROOT / "openquatt/includes/control/oq_input_source_logic.h").read_text()


def entity_block(source: str, entity_id: str) -> str:
    start = source.index(f"    id: {entity_id}")
    end = source.find("\n  - platform:", start + 1)
    return source[start:] if end < 0 else source[start:end]


class InputSourceRuntimeContractTest(unittest.TestCase):
    def test_entity_options_defaults_and_ranges_are_preserved(self) -> None:
        source_contracts = {
            "water_supply_source": ("options: [Local, CIC, HA input]", "initial_option: Local"),
            "flow_source": ("options: [Outdoor unit, CIC]", "initial_option: Outdoor unit"),
            "oq_duo_outdoor_flow_mode": (
                "options: [Flowmeter HP1, Flowmeter HP2, Local aggregate HP1/HP2]",
                "initial_option: Local aggregate HP1/HP2",
            ),
            "outside_temp_source": (
                "options: [Auto, Outdoor unit, HA input, API input, MQTT]",
                "initial_option: Auto",
            ),
        }
        for entity_id, snippets in source_contracts.items():
            block = entity_block(SOURCE_YAML, entity_id)
            for snippet in snippets:
                self.assertIn(snippet, block)

        api_ranges = {
            "api_input_cooling_dew_point": (-20, 35, 0.1),
            "api_input_outside_temperature": (-40, 60, 0.1),
            "api_input_room_temperature": (0, 50, 0.1),
            "api_input_room_setpoint": (5, 35, 0.1),
            "api_input_external_heat_demand": (0, 15000, 10),
        }
        for entity_id, (minimum, maximum, step) in api_ranges.items():
            block = entity_block(API_YAML, entity_id)
            self.assertIn(f"min_value: {minimum}", block)
            self.assertIn(f"max_value: {maximum}", block)
            self.assertIn(f"step: {step}", block)
            self.assertIn("restore_value: false", block)
            self.assertIn("optimistic: true", block)

        self.assertIn("restore_mode: RESTORE_DEFAULT_OFF", entity_block(SOURCE_YAML, "oq_manual_cooling_enable"))
        for entity_id in ("water_supply_temp_selected", "flow_rate_selected"):
            self.assertIn("update_interval: 5s", entity_block(SOURCE_YAML, entity_id))
        for entity_id in (
            "outside_temp_selected",
            "room_temp_selected",
            "room_setpoint_selected",
            "external_heat_demand_selected",
        ):
            self.assertIn("update_interval: 10s", entity_block(SOURCE_YAML, entity_id))

    def test_yaml_is_a_compact_runtime_contract(self) -> None:
        self.assertLessEqual(len(SOURCE_YAML.splitlines()) + len(API_YAML.splitlines()), 750)
        for call in (
            "oq_sensor_source::runtime().water_supply(",
            "oq_sensor_source::runtime().flow()",
            "oq_sensor_source::runtime().outside(",
            "oq_sensor_source::runtime().room_temperature(",
            "oq_sensor_source::runtime().room_setpoint(",
            "oq_sensor_source::runtime().external_heat_demand(",
        ):
            self.assertIn(call, SOURCE_YAML)
        self.assertEqual(API_YAML.count("oq_api_ingress::runtime().observe("), 9)
        self.assertIn("oq_api_ingress::runtime().tick(", API_YAML)

    def test_stateful_decisions_live_in_cpp(self) -> None:
        for removed in (
            "_has_value",
            "_last_valid_ms",
            "auto update_numeric",
            "auto update_binary",
            "static float last_valid",
        ):
            self.assertNotIn(removed, API_YAML)
        for removed in (
            "static float last_valid",
            "auto source_valid",
            "auto source_value",
            "auto source_name",
        ):
            self.assertNotIn(removed, SOURCE_YAML)
        self.assertIn("oq_input_source::evaluate_freshness", API_RUNTIME)
        self.assertIn("oq_input_source::select_outside", SOURCE_RUNTIME)
        self.assertIn("oq_input_source::select_direct", SOURCE_RUNTIME)

    def test_safety_and_upgrade_contracts_remain_explicit(self) -> None:
        self.assertEqual(API_YAML.count("restore_mode: ALWAYS_OFF"), 2)
        self.assertIn("oq_nvs_cleanup::erase_entity_preferences(", API_YAML)
        self.assertIn("oq_api_ingress::runtime().reset()", API_YAML)
        self.assertIn("oq_ot_room_temperature_fresh_expr", SOURCE_YAML)
        self.assertIn("oq_ot_room_setpoint_fresh_expr", SOURCE_YAML)
        self.assertIn("isfinite", API_RUNTIME)
        self.assertIn("isfinite", SOURCE_LOGIC)
        for runtime in (SOURCE_RUNTIME, API_RUNTIME, SOURCE_LOGIC):
            self.assertNotIn("${", runtime)

    def test_host_regressions_cover_failure_boundaries(self) -> None:
        host_test = (ROOT / "tests/host/input_source_logic_test.cpp").read_text()
        for test_name in (
            "test_freshness_accepts_timestamp_zero_and_rollover",
            "test_hold_is_bound_to_selected_source",
            "test_non_finite_samples_fail_closed",
            "test_outside_lowest_valid_selection",
            "test_enable_source_selection",
            "test_flow_source_routes",
        ):
            self.assertIn(test_name, host_test)


if __name__ == "__main__":
    unittest.main()
