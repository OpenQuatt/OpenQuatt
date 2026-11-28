from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE_YAML = (ROOT / "openquatt/oq_sensor_sources.yaml").read_text()
API_YAML = (ROOT / "openquatt/oq_api_ingress.yaml").read_text()
HA_YAML = (ROOT / "openquatt/oq_ha_inputs.yaml").read_text()
SUBSTITUTIONS_YAML = (ROOT / "openquatt/oq_substitutions_common.yaml").read_text()
SOURCE_RUNTIME = (ROOT / "openquatt/includes/control/oq_sensor_source_runtime.h").read_text()
FLOW_RUNTIME = (ROOT / "openquatt/includes/control/oq_flow_runtime.h").read_text()
HEAT_INTENT_RUNTIME = (ROOT / "openquatt/includes/control/oq_heat_intent_runtime.h").read_text()
API_RUNTIME = (ROOT / "openquatt/includes/control/oq_api_ingress_runtime.h").read_text()
SOURCE_LOGIC = (ROOT / "openquatt/includes/control/oq_input_source_logic.h").read_text()
QUICKSTART_JS = (ROOT / "openquatt/web/js/src/features/quickstart.js").read_text()


def entity_block(source: str, entity_id: str) -> str:
    start = source.index(f"    id: {entity_id}")
    end = source.find("\n  - platform:", start + 1)
    return source[start:] if end < 0 else source[start:end]


class InputSourceRuntimeContractTest(unittest.TestCase):
    def test_circulation_uses_the_receipt_for_the_current_flow_route(self) -> None:
        flow_check = SOURCE_RUNTIME.split("float circulation_flow_lph(", 1)[1].split("\n  }", 1)[0]
        self.assertIn("flow_selection()", flow_check)
        self.assertIn("selected_flow_receipt_(selected.route)", flow_check)
        self.assertIn("fresh_flow_lph(selected_flow_receipt_(selected.route), now_ms, max_age_ms)", flow_check)
        self.assertIn("fresh_flow_lph(primary_local_flow_receipt_()", flow_check)
        self.assertIn("fresh_flow_lph(oq_sources::hp2.flow", flow_check)
        self.assertIn("select_local_flow(OQ_TOPOLOGY_DUO, primary, secondary, mismatch_threshold_lph)", flow_check)
        self.assertIn("oq_sensor_source::runtime().circulation_flow_lph(", FLOW_RUNTIME)
        self.assertNotIn("odu_poll_fresh", FLOW_RUNTIME)
        route_reader = SOURCE_RUNTIME.split("static oq_sources::RawFloatReceipt selected_flow_receipt_(", 1)[1]
        route_reader = route_reader.split("\n  }", 1)[0]
        for route, receipt in (
            ("CONTROLLER", "oq_sources::current_controller_filtered_flow()"),
            ("CIC", "id(cic_component).flow_rate_receipt()"),
            ("HP1", "oq_sources::hp1.flow"),
            ("HP2", "oq_sources::hp2.flow"),
            ("AGGREGATE", "oq_sources::local_flow"),
        ):
            self.assertRegex(route_reader, rf"case Route::{route}:\s*return [^;]*{re.escape(receipt)}")

    def test_frost_uses_fixed_pwm_and_only_compressor_postflow_can_boost(self) -> None:
        self.assertRegex(SUBSTITUTIONS_YAML, r'(?m)^oq_cm98_pump_ipwm: "800"\s')
        self.assertIn("const bool postflow = cm_code == 1 && config.compressor_postflow_active;", FLOW_RUNTIME)
        self.assertIn("if (!postflow) postflow_ipwm_limit_ = oq_flow_control::kMaximumIpwm;", FLOW_RUNTIME)
        frost_branch = FLOW_RUNTIME.split("} else if (frost) {", 1)[1].split("} else {", 1)[0]
        self.assertIn("pwm = oq_flow_control::clamp_ipwm(config.frost_ipwm);", frost_branch)
        self.assertRegex(FLOW_RUNTIME, r"if \(postflow\) \{\s+const float flow_lph =\s+"
                         r"oq_sensor_source::runtime\(\)\.circulation_flow_lph\(")

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
            "api_input_heating_supply_target": (20, 70, 0.5),
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
            "heating_supply_target_selected",
        ):
            self.assertIn("update_interval: 10s", entity_block(SOURCE_YAML, entity_id))

    def test_yaml_delegates_to_runtime(self) -> None:
        for call in (
            "oq_sensor_source::runtime().water_supply(",
            "oq_sensor_source::runtime().flow()",
            "oq_sensor_source::runtime().outside(",
            "oq_sensor_source::runtime().room_temperature(",
            "oq_sensor_source::runtime().room_setpoint(",
            "oq_sensor_source::runtime().external_heat_demand(",
            "oq_sensor_source::runtime().heating_supply_target(",
        ):
            self.assertIn(call, SOURCE_YAML)
        self.assertEqual(API_YAML.count("oq_api_ingress::runtime().observe("), 11)
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

    def test_room_setpoint_validity_contract_is_shared(self) -> None:
        self.assertIn("ROOM_SETPOINT_MIN_C = 5.0f", SOURCE_LOGIC)
        self.assertIn("ROOM_SETPOINT_MAX_C = 35.0f", SOURCE_LOGIC)
        self.assertIn("room_setpoint_sample", SOURCE_RUNTIME)
        self.assertIn("room_setpoint_usable", HEAT_INTENT_RUNTIME)
        self.assertIn("roomSetpointUsable", QUICKSTART_JS)

    def test_safety_and_upgrade_contracts_remain_explicit(self) -> None:
        self.assertEqual(API_YAML.count("restore_mode: ALWAYS_OFF"), 2)
        self.assertIn("oq_nvs_cleanup::erase_entity_preferences(", API_YAML)
        self.assertIn("oq_api_ingress::runtime().reset()", API_YAML)
        self.assertIn("oq_ot_room_temperature_fresh_expr", SOURCE_YAML)
        self.assertIn("oq_ot_room_setpoint_fresh_expr", SOURCE_YAML)
        self.assertIn("oq_ot_supply_target_fresh_expr", SOURCE_YAML)
        self.assertIn("isfinite", API_RUNTIME)
        self.assertIn("isfinite", SOURCE_LOGIC)
        for runtime in (SOURCE_RUNTIME, API_RUNTIME, SOURCE_LOGIC):
            self.assertNotIn("${", runtime)

    def test_ha_ingress_heartbeat_gates_live_inputs(self) -> None:
        # A value frozen by HA connection loss must go stale even though
        # ESPHome retains the states (issue #698). Freshness is tracked via
        # one central heartbeat ingress: the heartbeat state itself changes
        # about once per minute, because Home Assistant does not forward
        # attribute-only changes to ESPHome sensors on the bare state.
        # Only Heating Supply Target keeps its pre-#700 observer during
        # migration. It is used until the first shared heartbeat so an older
        # HA package still expires a frozen target after its stale window.
        self.assertEqual(HA_YAML.count("observe_heating_supply_target_ha"), 2)
        self.assertIn("observe_heating_supply_target_ha", SOURCE_RUNTIME)
        self.assertIn("ha_supply_target_legacy_state_", SOURCE_RUNTIME)
        self.assertIn("ha_live_valid_with_legacy_freshness", SOURCE_RUNTIME)
        self.assertIn("ha_live_valid_with_legacy_freshness", SOURCE_LOGIC)
        self.assertNotIn("last_refresh", HA_YAML)
        self.assertEqual(HA_YAML.count("observe_ha_ingress"), 1)
        self.assertIn("ha_ingress_heartbeat", HA_YAML)
        # Only a finite heartbeat proves freshness: an unknown/unavailable
        # publish arrives as NAN and must not restart the stale clock.
        heartbeat_block = entity_block(HA_YAML, "ha_ingress_heartbeat")
        self.assertIn("if (isfinite(x))", heartbeat_block)
        self.assertIn("observe_ha_ingress", heartbeat_block)
        self.assertIn("ha_ingress_heartbeat_entity_id", SUBSTITUTIONS_YAML)
        self.assertIn("ha_ingress_state_", SOURCE_RUNTIME)
        self.assertIn("ha_live_valid", SOURCE_RUNTIME)
        self.assertIn("ha_live_valid", SOURCE_LOGIC)
        # Backward compatibility: while this boot never received a heartbeat
        # (pre-heartbeat HA package or custom proxies), plain entity validity
        # applies so an OTA never suddenly rejects existing HA ingress.
        self.assertIn("ha_live_valid_with_legacy", SOURCE_LOGIC)
        self.assertIn("ha_live_valid_with_legacy", SOURCE_RUNTIME)
        self.assertIn("if (!ingress.has_value) return entity_valid;", SOURCE_LOGIC)
        self.assertIn("if (!runtime.ha_ingress_seen()) return {};", SOURCE_YAML)
        for stale_sub in (
            "ha_outside_temperature_stale_s",
            "ha_water_supply_temperature_stale_s",
            "ha_room_temperature_stale_s",
            "ha_cooling_dew_point_stale_s",
            "ha_external_heat_demand_stale_s",
            "ha_heating_supply_target_stale_s",
        ):
            self.assertIn(stale_sub, SUBSTITUTIONS_YAML)
        for stale_sub in (
            "ha_outside_temperature_stale_s",
            "ha_water_supply_temperature_stale_s",
            "ha_room_temperature_stale_s",
            "ha_external_heat_demand_stale_s",
            "ha_heating_supply_target_stale_s",
        ):
            self.assertIn(stale_sub, SOURCE_YAML)
        cooling_safety_yaml = (ROOT / "openquatt/oq_cooling_safety.yaml").read_text()
        self.assertIn("ha_cooling_dew_point_stale_s", cooling_safety_yaml)
        cooling_safety_runtime = (ROOT / "openquatt/includes/control/oq_cooling_safety_logic.h").read_text()
        self.assertIn("ha_stale_s", cooling_safety_runtime)
        self.assertIn("ha_hold_revoked", SOURCE_RUNTIME)



if __name__ == "__main__":
    unittest.main()
