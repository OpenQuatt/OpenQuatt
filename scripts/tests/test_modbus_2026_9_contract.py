from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
SUBSTITUTIONS = (ROOT / "openquatt" / "oq_substitutions_common.yaml").read_text()
HUB = (ROOT / "openquatt" / "oq_common.yaml").read_text()
BASE_COMMON = (ROOT / "openquatt" / "base" / "common.yaml").read_text()
REQUIREMENTS = (ROOT / ".github" / "requirements-esphome.txt").read_text()
DUO_PACKAGES = (ROOT / "openquatt" / "topology" / "duo_packages.yaml").read_text()


def yaml_block(source: str, start: str, end: str) -> str:
    start_idx = source.index(start)
    end_idx = source.index(end, start_idx)
    return source[start_idx:end_idx]


def entity_block(source: str, marker: str) -> str:
    """Return the single `- platform:` list item containing marker.

    Bounds assertions to the entity's own block, so a key found here
    provably belongs to this entity and not to a neighbour.
    """
    idx = source.index(marker)
    item_start = source.rfind("\n  - platform:", 0, idx) + 1
    item_end = source.find("\n  - platform:", idx)
    if item_end == -1:
        item_end = len(source)
    return source[item_start:item_end]


class Modbus20269ContractTest(unittest.TestCase):
    def test_esphome_pin_is_explicit_2026_9_beta(self) -> None:
        # PR 1 uses the pinned 2026.9.0b4 beta until a stable 2026.9.x is chosen.
        self.assertIn("esphome==2026.9.0b4", REQUIREMENTS)
        self.assertNotIn("esphome==2026.8.2", REQUIREMENTS)
        self.assertIn("min_version: 2026.9.0b4", BASE_COMMON)

    def test_regular_online_polling_is_10s_and_offline_probe_is_30s(self) -> None:
        self.assertIn('oq_modbus_update_interval_s: "10"', SUBSTITUTIONS)
        self.assertIn('oq_modbus_offline_probe_interval_s: "30"', SUBSTITUTIONS)
        self.assertNotIn("oq_modbus_telemetry_skip", SUBSTITUTIONS)
        self.assertNotIn("oq_modbus_target_readback_skip", SUBSTITUTIONS)
        self.assertNotIn("oq_modbus_control_readback_skip", SUBSTITUTIONS)
        self.assertIn("interval: ${oq_modbus_update_interval_s}s", HP_IO)
        self.assertIn("${oq_modbus_offline_probe_interval_s}UL * 1000UL", HP_IO)

    def test_skip_updates_removed_but_offline_skip_kept(self) -> None:
        active_skips = [
            line
            for line in HP_IO.splitlines()
            if "skip_updates:" in line and not line.strip().startswith("#")
        ]
        # Only the controller-level offline cadence remains; per-entity skip_updates is gone.
        self.assertEqual(active_skips, ["    offline_skip_updates: 5"])
        self.assertNotIn("oq_modbus_telemetry_skip", HP_IO)
        self.assertNotIn("oq_modbus_target_readback_skip", HP_IO)
        self.assertNotIn("oq_modbus_control_readback_skip", HP_IO)

    def test_old_range_fields_migrated(self) -> None:
        active_counts = [
            line
            for line in HP_IO.splitlines()
            if "register_count:" in line and not line.strip().startswith("#")
        ]
        self.assertEqual(active_counts, [])
        self.assertNotIn("force_new_range", HP_IO)

    def test_known_gaps_bridged_with_reuse(self) -> None:
        # Each previously bridged reserved register is now covered by reuse on the next entity.
        cases = (
            ("id: ${hp_id}_eev_steps\n", "2107"),
            ("id: ${hp_id}_outside_temp\n", "2110"),
            ("id: ${hp_id}_status_2115_raw\n", "2115"),
            ("id: ${hp_id}_control_board_item\n", "2127"),
            ("id: ${hp_id}_condensing_temp\n", "2131"),
            ("id: ${hp_id}_pump_ipwm_feedback_raw\n", "2137"),
        )
        for marker, address in cases:
            block = entity_block(HP_IO, marker)
            with self.subTest(entity=marker.strip()):
                # Trailing newline prevents prefix matches (e.g. outside_temp
                # vs outside_temp_last_change_ms in the globals section).
                self.assertIn(f"address: {address}", block)
                self.assertIn("reuse_previous_range: true", block)
                self.assertNotIn("register_count", block)
        # No other Modbus entity should need an explicit reuse flag for PR 1.
        self.assertEqual(HP_IO.count("reuse_previous_range: true"), 6)

    def test_status_2115_decoded_from_physical_2115(self) -> None:
        block = yaml_block(
            HP_IO,
            "id: ${hp_id}_status_2115_raw",
            "id: ${hp_id}_status_2120_raw",
        )
        self.assertIn("address: 2115", block)
        self.assertIn("value_type: U_WORD", block)
        self.assertIn("reuse_previous_range: true", block)
        self.assertNotIn("offset: 4", block)
        self.assertNotIn("register_count", block)
        self.assertNotIn("address: 2113", block)

    def test_planner_ownership_and_transports_unchanged(self) -> None:
        self.assertIn("update_interval: never", HP_IO)
        self.assertIn("id(${hp_id}).update();", HP_IO)
        self.assertIn('oq_modbus_startup_delay_ms: "2500"', DUO_PACKAGES)
        self.assertIn("baud_rate: 19200", HUB)
        self.assertIn("parity: EVEN", HUB)
        self.assertIn("turnaround_time: ${oq_modbus_command_throttle_ms}ms", HUB)
        # Control readbacks are intentionally on the regular 10s cadence now.
        for entity_id in (
            "${hp_id}_set_working_mode",
            "${hp_id}_compressor_level",
            "${hp_id}_low_noise_mode",
            "${hp_id}_set_pump_mode",
            "${hp_id}_pump_speed",
        ):
            with self.subTest(entity=entity_id):
                self.assertIn(f"id: {entity_id}", HP_IO)
        self.assertNotIn("skip_updates", HP_IO.replace("offline_skip_updates", "").replace("#skip_updates: 0", ""))

    def test_telemetry_still_uses_expected_registers(self) -> None:
        # Spot-check that each entity is coupled to its own register: the
        # address must sit inside the entity's own block, not anywhere else.
        for address, marker in (
            ("2099", "id: ${hp_id}_working_mode\n"),
            ("2105", "id: ${hp_id}_fan_speed\n"),
            ("2107", "id: ${hp_id}_eev_steps\n"),
            ("2110", "id: ${hp_id}_outside_temp\n"),
            ("2113", "id: ${hp_id}_gas_return_temp\n"),
            ("2115", "id: ${hp_id}_status_2115_raw\n"),
            ("2122", "id: ${hp_id}_pcb_firmware_raw\n"),
            ("2123", 'name: "${prefix}Firmware EEPROM"'),
            ("2127", "id: ${hp_id}_control_board_item\n"),
            ("2131", "id: ${hp_id}_condensing_temp\n"),
            ("2135", "id: ${hp_id}_inner_coil_temp\n"),
            ("2137", "id: ${hp_id}_pump_ipwm_feedback_raw\n"),
            ("2138", "id: ${hp_id}_flow\n"),
            ("1999", "id: ${hp_id}_compressor_level\n"),
            ("3999", "id: ${hp_id}_set_working_mode\n"),
        ):
            with self.subTest(entity=marker.strip()):
                self.assertIn(f"address: {address}", entity_block(HP_IO, marker))
        # Temperature filter offsets are unrelated to the Modbus address offset migration.
        self.assertIn("- offset: -3000", HP_IO)


if __name__ == "__main__":
    unittest.main()
