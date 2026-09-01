from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
MANUAL_HP = (ROOT / "openquatt" / "oq_manual_hp.yaml").read_text()
ACTUATOR = (ROOT / "openquatt/includes/control/oq_thermal_actuator_runtime.h").read_text()
REQUEST_CONTROL = (ROOT / "openquatt" / "oq_thermal_request_control.yaml").read_text()
REQUEST_RUNTIME = (
    ROOT / "openquatt/includes/control/oq_thermal_request_runtime.h"
).read_text()
LEVEL_HEADER = (
    ROOT / "openquatt" / "includes" / "odu" / "oq_odu_compressor_levels.h"
).read_text()
FREQUENCY_HEADER = (
    ROOT / "openquatt" / "includes" / "odu" / "oq_odu_frequency_table.h"
).read_text()
RUNTIME_EDITOR = (
    ROOT / "openquatt" / "experimental" / "oq_odu_runtime_frequency_service_hp.yaml"
).read_text()
RUNTIME_EDITOR_HEADER = (
    ROOT / "components/openquatt_odu_runtime_frequency/OpenQuattOduRuntimeFrequency.h"
).read_text()
RUNTIME_EDITOR_SOURCE = (
    ROOT / "components/openquatt_odu_runtime_frequency/OpenQuattOduRuntimeFrequency.cpp"
).read_text()
RUNTIME_EDITOR_LOGIC = (
    ROOT
    / "openquatt"
    / "includes"
    / "experimental"
    / "oq_odu_runtime_frequency_table_logic.h"
).read_text()
SERVICE_UI = (
    ROOT / "openquatt" / "web" / "js" / "src" / "settings" / "service.js"
).read_text()


def yaml_block(source: str, start: str, end: str) -> str:
    match = re.search(re.escape(start) + r"([\s\S]*?)" + re.escape(end), source)
    if match is None:
        raise AssertionError(f"block not found: {start}")
    return match.group(1)


class V2CompressorLevelContractTest(unittest.TestCase):
    def test_modbus_select_exposes_all_physical_levels(self) -> None:
        block = yaml_block(
            HP_IO,
            "id: ${hp_id}_compressor_level",
            "skip_updates: ${oq_modbus_control_readback_skip}",
        )
        for level in range(21):
            self.assertIn(f'"{level}": {level}', block)

    def test_manual_request_surface_exposes_f20(self) -> None:
        self.assertEqual(MANUAL_HP.count("max_value: 20"), 2)
        self.assertIn("this->physical_limit_(true", REQUEST_RUNTIME)
        self.assertIn("this->physical_limit_(false", REQUEST_RUNTIME)
        self.assertIn("oq_thermal_request_runtime::runtime().tick", REQUEST_CONTROL)

    def test_actuator_keeps_control_and_physical_domains_separate(self) -> None:
        self.assertIn("level_options[21]", ACTUATOR)
        self.assertIn("resolve_automatic_level(", ACTUATOR)
        self.assertIn("resolve_manual_level(", ACTUATOR)
        self.assertIn("return command.control_level;", ACTUATOR)
        self.assertIn("last_commanded_physical_level", ACTUATOR)
        self.assertLess(
            ACTUATOR.index("applied = cycle.frequency.pick_allowed_level("),
            ACTUATOR.rindex("resolve_automatic_level("),
        )
        self.assertLess(
            ACTUATOR.rindex("resolve_automatic_level("),
            ACTUATOR.index("apply_start_gate_before_active_write"),
        )
        self.assertIn("runtime frequency table cannot map compressor request", ACTUATOR)

    def test_runtime_table_loading_is_read_only_fingerprint_gated_and_fail_closed(self) -> None:
        self.assertIn("BASE_FREQUENCY_TABLE_REGISTER", HP_IO)
        self.assertIn("EXTENDED_FREQUENCY_TABLE_REGISTER", HP_IO)
        self.assertIn("variant != oq_odu::Variant::V2_NEW_MODEL", HP_IO)
        self.assertIn("create_read_command(", HP_IO)
        self.assertNotIn("create_write_multiple_command(", HP_IO)
        self.assertIn("affected mode limited to F10", HP_IO)
        self.assertIn("defer_base_publish", HP_IO)
        extension_guard = HP_IO.index("if (!extension_result.response_complete)")
        extension_publish = HP_IO.index("runtime_frequency_snapshot_storage) =", extension_guard)
        self.assertLess(extension_guard, extension_publish)
        self.assertNotIn("detect_extended_heating_table_response", HP_IO)
        self.assertNotIn("V2_HEATING_MODEL_TO_PHYSICAL", LEVEL_HEADER)

    def test_runtime_table_register_contract_matches_known_layout(self) -> None:
        self.assertIn("BASE_FREQUENCY_TABLE_REGISTER = 3000U", FREQUENCY_HEADER)
        self.assertIn("BASE_FREQUENCY_TABLE_REGISTER_COUNT = 22U", FREQUENCY_HEADER)
        self.assertIn("EXTENDED_FREQUENCY_TABLE_REGISTER = 3050U", FREQUENCY_HEADER)
        self.assertIn("EXTENDED_FREQUENCY_TABLE_REGISTER_COUNT = 20U", FREQUENCY_HEADER)
        self.assertIn("snapshot.variant != Variant::V2_NEW_MODEL", FREQUENCY_HEADER)

    def test_experimental_write_invalidates_and_then_reloads_control_snapshot(self) -> None:
        self.assertIn("on_write_started:", RUNTIME_EDITOR)
        self.assertIn("oq_odu::RuntimeFrequencySnapshotStorage{}", RUNTIME_EDITOR)
        self.assertIn("write_tainted_.store(true", RUNTIME_EDITOR_SOURCE)
        self.assertIn("VERIFY_FAILED: readback mismatch", RUNTIME_EDITOR_SOURCE)
        self.assertIn("write_tainted_.store(false", RUNTIME_EDITOR_SOURCE)
        self.assertIn("APPLIED: runtime table written and read back", RUNTIME_EDITOR_SOURCE)
        self.assertIn("on_write_applied:", RUNTIME_EDITOR)
        self.assertIn("script.execute: ${hp_id}_load_runtime_frequency_table_once", RUNTIME_EDITOR)

    def test_experimental_editor_only_opens_extension_for_confirmed_v2_new(self) -> None:
        self.assertIn("set_extended_layout(", HP_IO)
        self.assertIn("oq_odu::Variant::V2_NEW_MODEL", HP_IO)
        self.assertNotIn("platform: template", RUNTIME_EDITOR)
        self.assertNotIn("_odu_runtime_cooling_f", RUNTIME_EDITOR)
        self.assertNotIn("_odu_runtime_heating_f", RUNTIME_EDITOR)
        self.assertIn("EXTENDED_TABLE_START_ADDRESS = 3050U", RUNTIME_EDITOR_LOGIC)
        self.assertIn("EXTENDED_TABLE_REGISTER_COUNT = 20U", RUNTIME_EDITOR_LOGIC)
        self.assertIn("queue_load_extension_", RUNTIME_EDITOR_SOURCE)
        self.assertIn("queue_readback_extension_", RUNTIME_EDITOR_SOURCE)
        self.assertIn("EXTENDED_OPERATION_TIMEOUT_MS = 60000U", RUNTIME_EDITOR_HEADER)
        self.assertIn("BASE_OPERATION_TIMEOUT_MS = 30000U", RUNTIME_EDITOR_HEADER)
        self.assertIn("queue_guard_(pending_request_token)", RUNTIME_EDITOR_SOURCE)
        self.assertIn("token_matches_(operation_token)", RUNTIME_EDITOR_SOURCE)
        self.assertIn("values[0] != 0U", RUNTIME_EDITOR_LOGIC)
        self.assertIn("values[index] == 0U", RUNTIME_EDITOR_LOGIC)
        self.assertNotIn("create_write_multiple_command", RUNTIME_EDITOR_SOURCE)

    def test_failed_experimental_write_cannot_be_reaccepted_by_generic_retry(self) -> None:
        self.assertIn("runtime_reload_blocked_after_write", HP_IO)
        loader_block = yaml_block(
            HP_IO,
            "id: ${hp_id}_load_runtime_frequency_table_once",
            "id: ${hp_id}_detect_odu_generation",
        )
        self.assertIn("->runtime_reload_blocked_after_write()", loader_block)
        self.assertIn("->runtime_reload_blocked_after_write()", HP_IO)
        self.assertIn("write_tainted_", RUNTIME_EDITOR_HEADER)
        self.assertIn("if (this->write_started_)", RUNTIME_EDITOR_SOURCE)

    def test_offline_transition_invalidates_and_rechecks_profile(self) -> None:
        offline_block = yaml_block(HP_IO, "on_offline:", "on_online:")
        online_block = yaml_block(HP_IO, "on_online:", "openquatt_odu_eeprom_dump:")
        self.assertIn("CompressorLevelProfile::UNKNOWN", offline_block)
        self.assertIn("runtime_frequency_snapshot_storage) = {}", offline_block)
        self.assertIn("compressor_level_profile_request_token", offline_block)
        self.assertIn("odu_runtime_frequency)->reset_runtime_state", offline_block)
        self.assertIn("VERIFY_FAILED: ODU disconnected during write", offline_block)
        self.assertIn("detect_odu_generation_once", online_block)

    def test_blocked_or_incomplete_detection_is_retried_without_opening_extension(self) -> None:
        self.assertIn("runtime_frequency_retry_ms", HP_IO)
        self.assertIn("now - last_retry >= 60000UL", HP_IO)
        self.assertIn("!id(${hp_id}_odu_generation_detection_complete)", HP_IO)
        self.assertIn("table_incomplete", HP_IO)
        self.assertIn("!id(${hp_id}_compressor_level_profile_request_pending)", HP_IO)

    def test_frequency_telemetry_accepts_f20(self) -> None:
        for entity_id in (
            "${hp_id}_compressor_frequency_demand",
            "${hp_id}_compressor_frequency",
        ):
            block = yaml_block(HP_IO, f"id: {entity_id}", "skip_updates: ${oq_modbus_telemetry_skip}")
            self.assertIn("max_value: 120", block)

    def test_profile_label_matches_service_ui_gate(self) -> None:
        self.assertIn('"V2 F0-F20"', LEVEL_HEADER)
        self.assertIn('"V2 heating F0-F20"', LEVEL_HEADER)
        self.assertIn('"V2 cooling F0-F20"', LEVEL_HEADER)
        self.assertIn('profile === "V2 F0-F20"', SERVICE_UI)
        self.assertIn('mode === "Heating" && profile === "V2 heating F0-F20"', SERVICE_UI)
        self.assertIn('mode === "Cooling" && profile === "V2 cooling F0-F20"', SERVICE_UI)


if __name__ == "__main__":
    unittest.main()
