from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
REQUEST_CONTROL = (ROOT / "openquatt" / "oq_thermal_request_control.yaml").read_text()
SUPERVISORY = (ROOT / "openquatt" / "oq_supervisory_controlmode.yaml").read_text()
POWER_HOUSE = (ROOT / "openquatt" / "oq_power_house_strategy.yaml").read_text()
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
SUPERVISORY_RUNTIME = (
    ROOT / "openquatt" / "includes" / "control" / "oq_supervisory_state_runtime.h"
).read_text()
DUO_PACKAGES = (ROOT / "openquatt" / "topology" / "duo_packages.yaml").read_text()
RECORDER_SOURCE = (
    ROOT / "components/openquatt_debug_recorder/OpenQuattDebugRecorder.cpp"
).read_text()
RECORDER_HEADER = (
    ROOT / "components/openquatt_debug_recorder/OpenQuattDebugRecorder.h"
).read_text()
CONFIG_JS = (
    ROOT / "openquatt" / "web" / "js" / "src" / "core" / "config.js"
).read_text()


CHAIN_SENSORS = [
    ("oq_hp1_requested_control_level_sensor", "HP1 requested control level", REQUEST_CONTROL),
    ("oq_hp1_applied_control_level_sensor", "HP1 applied control level", REQUEST_CONTROL),
    ("oq_hp1_table_frequency_sensor", "HP1 table frequency", REQUEST_CONTROL),
    ("oq_hp2_requested_control_level_sensor", "HP2 requested control level", REQUEST_CONTROL),
    ("oq_hp2_applied_control_level_sensor", "HP2 applied control level", REQUEST_CONTROL),
    ("oq_hp2_table_frequency_sensor", "HP2 table frequency", REQUEST_CONTROL),
    ("oq_ph_fast_intent_code_sensor", "Power House fast intent code", POWER_HOUSE),
    ("oq_low_load_pmin_dyn_w_sensor", "Low-load Pmin", SUPERVISORY),
    ("oq_low_load_off_dyn_w_sensor", "Low-load off threshold", SUPERVISORY),
    ("oq_low_load_on_dyn_w_sensor", "Low-load on threshold", SUPERVISORY),
]

CHAIN_KEYS = [
    "hp1RequestedControlLevel",
    "hp1AppliedControlLevel",
    "hp1TableFrequency",
    "hp2RequestedControlLevel",
    "hp2AppliedControlLevel",
    "hp2TableFrequency",
    "phFastIntentCode",
    "lowLoadLatch",
    "lowLoadPminW",
    "lowLoadOffW",
    "lowLoadOnW",
    "debugStaticSnapshot",
]

REGISTER_KEYS = [
    "hp1CompressorFrequencyDemand",
    "hp2CompressorFrequencyDemand",
    "hp1LowNoiseMode",
    "hp2LowNoiseMode",
]


class DebugRecorderV2ChainContractTest(unittest.TestCase):
    def test_chain_entities_exist_with_exact_recorder_names(self) -> None:
        for entity_id, name, source in CHAIN_SENSORS:
            self.assertIn(f"id: {entity_id}", source)
            self.assertIn(f'name: "{name}"', source)
        self.assertIn('id: oq_lowload_heat_latch_bs', SUPERVISORY)
        self.assertIn('name: "Low-load latch"', SUPERVISORY)
        self.assertIn('id: oq_debug_static_snapshot', POWER_HOUSE)
        self.assertIn('name: "Debug static snapshot"', POWER_HOUSE)

    def test_chain_entities_are_read_only_diagnostics(self) -> None:
        blocks = []
        for entity_id, _, source in CHAIN_SENSORS:
            block = source[source.index(f"id: {entity_id}"):]
            end = block.find("\n  - platform:", 10)
            blocks.append(block[: end if end != -1 else len(block)])
        latch_block = SUPERVISORY[SUPERVISORY.index("id: oq_lowload_heat_latch_bs"):]
        blocks.append(latch_block[: latch_block.index("text_sensor:", 10)])
        snapshot_block = POWER_HOUSE[POWER_HOUSE.index("id: oq_debug_static_snapshot"):]
        blocks.append(snapshot_block[: snapshot_block.index("\nsensor:", 10)])
        for block in blocks:
            self.assertIn("internal: true", block)
            self.assertNotIn("set_action:", block)
            self.assertNotIn("on_press:", block)
            self.assertNotIn("on_turn_on:", block)
            self.assertNotIn("on_turn_off:", block)
            writes = re.findall(r"id\([A-Za-z0-9_${}]+\)\s*=", block)
            self.assertEqual(writes, [])

    def test_table_hz_and_snapshot_fail_closed_without_invented_values(self) -> None:
        self.assertIn("if (physical == 0) return 0.0f;", REQUEST_CONTROL)
        self.assertIn("if (physical < 0 || !snapshot.heating.valid) return NAN;", REQUEST_CONTROL)
        self.assertIn("return hz > 0 ? (float) hz : NAN;", REQUEST_CONTROL)
        self.assertIn("frequency_for_physical_level(snapshot.heating, physical)", REQUEST_CONTROL)
        self.assertIn("if (!table.valid) return 0U;", POWER_HOUSE)
        self.assertIn("valid", POWER_HOUSE)
        self.assertIn("return std::string(\"unknown\");", POWER_HOUSE)
        # No invented frequency fallback inside the new table sensors.
        hp1_block = REQUEST_CONTROL[REQUEST_CONTROL.index("id: oq_hp1_table_frequency_sensor"):]
        hp1_block = hp1_block[: hp1_block.index("id: oq_hp2_requested_control_level_sensor")]
        self.assertNotIn("900", hp1_block)
        self.assertNotIn("1300", hp1_block)

    def test_duo_guards_return_nan_and_single_snapshot_uses_null(self) -> None:
        self.assertIn("#if OQ_TOPOLOGY_DUO", REQUEST_CONTROL)
        self.assertIn("id(${secondary_last_applied_level_id})", REQUEST_CONTROL)
        self.assertIn("id(${secondary_runtime_frequency_snapshot_storage_id})", REQUEST_CONTROL)
        self.assertIn("id(${secondary_last_commanded_physical_level_id})", REQUEST_CONTROL)
        self.assertIn("hp2", POWER_HOUSE)
        self.assertIn('"null"', POWER_HOUSE)

    def test_static_snapshot_is_excluded_from_status_events(self) -> None:
        self.assertIn('std::strcmp(field.key, "debugStaticSnapshot") != 0', RECORDER_SOURCE)

    def test_debug_keys_are_additive_compact_and_within_budget(self) -> None:
        for key in CHAIN_KEYS + REGISTER_KEYS:
            self.assertIn(f'"{key}"', CONFIG_JS)
        debug_section = CONFIG_JS[CONFIG_JS.index("export const DEBUG_RECORDING_KEYS"):]
        tail = debug_section[debug_section.index('"boilerPowerTestResultQuality"'):]
        positions = [tail.index(f'"{key}"') for key in CHAIN_KEYS + REGISTER_KEYS]
        self.assertEqual(positions, sorted(positions))
        for key in CHAIN_KEYS + REGISTER_KEYS:
            self.assertLess(len(key), 40)
        header_capacity = int(re.search(r"FIELD_CAPACITY = (\d+)", RECORDER_HEADER).group(1))
        header_system = int(re.search(r"SYSTEM_FIELD_COUNT = (\d+)", RECORDER_HEADER).group(1))
        self.assertEqual(header_capacity, 224)
        self.assertEqual(header_system, 5)
        self.assertLessEqual(206, header_capacity - header_system)

    def test_low_load_numerics_keep_text_for_backward_compatibility(self) -> None:
        self.assertIn('name: "Low-load dynamic thresholds"', SUPERVISORY)
        self.assertIn("lowLoadDynamicThresholds", CONFIG_JS)

    def test_odu_register_reuse_needs_no_new_entities(self) -> None:
        # Demand is het bestaande Modbus-register 2102 (Hz, geclampte meting);
        # zonder brondata levert de sensor NAN en neemt de recorder null op.
        self.assertIn("id: ${hp_id}_compressor_frequency_demand", HP_IO)
        self.assertIn('name: "${prefix}Compressor frequency demand"', HP_IO)
        self.assertIn("address: 2102", HP_IO)
        self.assertIn('unit_of_measurement: "Hz"', HP_IO)
        # Silent-status is de aangestuurde ODU-select op register 2006
        # (Off/On, optimistisch + readback), niet de planningsbit.
        self.assertIn("id: ${hp_id}_low_noise_mode", HP_IO)
        self.assertIn('name: "${prefix}Silent Mode"', HP_IO)
        self.assertIn("address: 2006", HP_IO)
        self.assertIn('"Off": 0', HP_IO)
        self.assertIn('"On": 1', HP_IO)
        # HP2-instanties bestaan alleen op Duo; op Single slaat de recorder
        # ze veilig over als missing (null).
        self.assertIn('hp_id: "hp2"', DUO_PACKAGES)

    def test_silent_select_is_commanded_not_planned(self) -> None:
        # De supervisory stuurt de ODU-select uit het silent-venster, met
        # uitzondering voor manual-HP; silentActive is slechts planning.
        self.assertIn("set_select_option(id(hp1_low_noise_mode), silent_opt)", SUPERVISORY_RUNTIME)
        self.assertIn("silent_active && !oq_manual_hp::owns_control()", SUPERVISORY_RUNTIME)


if __name__ == "__main__":
    unittest.main()
