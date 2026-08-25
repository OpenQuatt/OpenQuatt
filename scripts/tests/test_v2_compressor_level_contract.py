from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
MANUAL_HP = (ROOT / "openquatt" / "oq_manual_hp.yaml").read_text()
ACTUATOR = (ROOT / "openquatt" / "oq_thermal_actuator.yaml").read_text()
REQUEST_CONTROL = (ROOT / "openquatt" / "oq_thermal_request_control.yaml").read_text()
LEVEL_HEADER = (
    ROOT / "openquatt" / "includes" / "odu" / "oq_odu_compressor_levels.h"
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
        self.assertIn("configured_v2, compressor_level_profile(true)", REQUEST_CONTROL)
        self.assertIn("configured_v2, compressor_level_profile(false)", REQUEST_CONTROL)

    def test_actuator_keeps_control_and_physical_domains_separate(self) -> None:
        self.assertIn("const char* lvl_opts[21]", ACTUATOR)
        self.assertIn("resolve_automatic_level(", ACTUATOR)
        self.assertIn("resolve_manual_level(", ACTUATOR)
        self.assertIn("return level_command.control_level;", ACTUATOR)
        self.assertIn("last_commanded_physical_level", ACTUATOR)

    def test_extended_profile_detection_is_read_only_and_fail_closed(self) -> None:
        self.assertIn("EXTENDED_HEATING_TABLE_REGISTER", HP_IO)
        self.assertIn("create_read_command(", HP_IO)
        self.assertNotIn("create_write_multiple_command(", HP_IO)
        self.assertIn("limiting writes to F10", HP_IO)

    def test_offline_transition_invalidates_and_rechecks_profile(self) -> None:
        offline_block = yaml_block(HP_IO, "on_offline:", "on_online:")
        online_block = yaml_block(HP_IO, "on_online:", "openquatt_odu_eeprom_dump:")
        self.assertIn("CompressorLevelProfile::UNKNOWN", offline_block)
        self.assertIn("compressor_level_profile_request_token", offline_block)
        self.assertIn("detect_compressor_level_profile_once", online_block)

    def test_frequency_telemetry_accepts_f20(self) -> None:
        for entity_id in (
            "${hp_id}_compressor_frequency_demand",
            "${hp_id}_compressor_frequency",
        ):
            block = yaml_block(HP_IO, f"id: {entity_id}", "skip_updates: ${oq_modbus_telemetry_skip}")
            self.assertIn("max_value: 120", block)

    def test_profile_label_matches_service_ui_gate(self) -> None:
        self.assertIn('"V2 F0-F20"', LEVEL_HEADER)
        self.assertIn('=== "V2 F0-F20"', SERVICE_UI)


if __name__ == "__main__":
    unittest.main()
