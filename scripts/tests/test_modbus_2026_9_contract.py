from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
SUBSTITUTIONS = (ROOT / "openquatt" / "oq_substitutions_common.yaml").read_text()
HUB = (ROOT / "openquatt" / "oq_common.yaml").read_text()
BASE_COMMON = (ROOT / "openquatt" / "base" / "common.yaml").read_text()
REQUIREMENTS = (ROOT / ".github" / "requirements-esphome.txt").read_text()
DUO_PACKAGES = (ROOT / "openquatt" / "topology" / "duo.yaml").read_text()


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
    def test_esphome_pin_matches_reviewed_2026_10_beta(self) -> None:
        # Retain the 2026.9 Modbus API contract on the reviewed 2026.10 beta.
        self.assertEqual(REQUIREMENTS.strip(), "esphome==2026.10.0b2")
        self.assertIn("min_version: 2026.10.0b2", BASE_COMMON)
        web_base = (ROOT / "components/web_server_base/__init__.py").read_text()
        self.assertIn('if __version__ != "2026.10.0b2":', web_base)

    def test_regular_online_polling_is_10s_and_offline_probe_is_30s(self) -> None:
        self.assertIn('oq_modbus_update_interval_s: "10"', SUBSTITUTIONS)
        self.assertIn('oq_modbus_offline_probe_interval_s: "30"', SUBSTITUTIONS)
        self.assertIn('oq_modbus_command_throttle_ms: "100"', SUBSTITUTIONS)
        self.assertNotIn("oq_modbus_telemetry_skip", SUBSTITUTIONS)
        self.assertNotIn("oq_modbus_target_readback_skip", SUBSTITUTIONS)
        self.assertNotIn("oq_modbus_control_readback_skip", SUBSTITUTIONS)
        self.assertIn("interval: ${oq_modbus_update_interval_s}s", HP_IO)
        self.assertIn("${oq_modbus_offline_probe_interval_s}UL * 1000UL", HP_IO)
        self.assertIn("send_wait_time: 100ms", HUB)
        self.assertIn("turnaround_time: ${oq_modbus_command_throttle_ms}ms", HUB)

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

    def test_fast_slow_same_address_controllers(self) -> None:
        # Upstream #18652 skip_updates replacement: per HP a primary (fast)
        # and a slow controller share mod_bus and the physical ODU address,
        # both with update_interval: never so OpenQuatt owns all timing.
        primary = yaml_block(HP_IO, "- id: ${hp_id}\n", "- id: ${hp_id}_slow")
        slow = yaml_block(HP_IO, "- id: ${hp_id}_slow\n", "openquatt_odu_eeprom_dump:")
        for block in (primary, slow):
            self.assertIn("address: ${device_address}", block)
            self.assertIn("modbus_id: mod_bus", block)
            self.assertIn("update_interval: never", block)
            self.assertIn("max_cmd_retries: ${oq_modbus_max_cmd_retries}", block)
        # Only the controller-level offline cadence remains; per-entity skip_updates is gone.
        self.assertIn("offline_skip_updates: 5", primary)
        self.assertNotIn("offline_skip_updates", slow)
        # Control readbacks live on the slow controller; target R3999 stays fast.
        for entity_id in (
            "${hp_id}_compressor_level",
            "${hp_id}_low_noise_mode",
            "${hp_id}_set_pump_mode",
            "${hp_id}_pump_speed",
        ):
            with self.subTest(entity=entity_id):
                block = entity_block(HP_IO, f"id: {entity_id}\n")
                self.assertIn("modbus_controller_id: ${hp_id}_slow", block)
        fast_block = entity_block(HP_IO, "id: ${hp_id}_set_working_mode\n")
        self.assertIn("modbus_controller_id: ${hp_id}\n", fast_block)
        self.assertNotIn("${hp_id}_slow", fast_block)

    def test_planner_schedules_fast_10s_and_slow_30s(self) -> None:
        self.assertIn("id(${hp_id}).update();", HP_IO)
        # Explicit per-HP cycle counter drives the 30 s cadence, not millis()%30000.
        self.assertIn("id: ${hp_id}_poll_cycle", HP_IO)
        self.assertIn("poll_cycle % 3U == 0U", HP_IO)
        self.assertIn("id(${hp_id}_slow).update();", HP_IO)
        # On a slow cycle the primary is queued before the slow controller.
        fast_idx = HP_IO.index("id(${hp_id}).update();")
        slow_idx = HP_IO.index("id(${hp_id}_slow).update();")
        self.assertLess(fast_idx, slow_idx)
        # HP1 starts immediately, HP2 keeps its 2500 ms staging phase.
        self.assertIn('oq_modbus_startup_delay_ms: "0"', SUBSTITUTIONS)
        self.assertIn('oq_modbus_startup_delay_ms: "2500"', DUO_PACKAGES)
        self.assertIn("baud_rate: 19200", HUB)
        self.assertIn("parity: EVEN", HUB)

    def test_transport_ownership_stays_with_primary(self) -> None:
        # Only the primary controller carries on_online. The slow controller
        # has exactly one on_offline hook, and only to escalate an exhausted
        # write timeout to the transport owner (see below).
        self.assertEqual(HP_IO.count("on_online:"), 1)
        self.assertEqual(HP_IO.count("on_offline:"), 2)
        slow = yaml_block(HP_IO, "- id: ${hp_id}_slow\n", "openquatt_odu_eeprom_dump:")
        self.assertNotIn("on_online", slow)
        self.assertNotIn("observe_transport", slow)
        self.assertNotIn("revalidation", slow)
        self.assertNotIn("_is_online) =", slow)
        primary = yaml_block(HP_IO, "- id: ${hp_id}\n", "- id: ${hp_id}_slow")
        self.assertIn("observe_transport", primary)

    def test_slow_offline_escalates_only_write_timeouts(self) -> None:
        slow = yaml_block(HP_IO, "- id: ${hp_id}_slow\n", "openquatt_odu_eeprom_dump:")
        # Write timeouts (any mutating function code, present and future)
        # escalate to the primary transport owner.
        self.assertIn("on_offline:", slow)
        self.assertIn("is_function_code_write", slow)
        self.assertIn("id(${hp_id}).set_online(false, function_code, address)", slow)
        # Read timeouts stay local: no transport observation, no online flag,
        # no revalidation is driven from the slow controller.
        self.assertNotIn("observe_transport", slow)
        self.assertNotIn("incident_manager", slow)

    def test_offline_recovery_queues_without_idle_bus(self) -> None:
        # The recovery probe must queue behind in-flight traffic: no exact-idle
        # guard, at most one outstanding probe per HP on a 30 s cadence.
        self.assertNotIn("tx_buffer_empty", HP_IO)
        self.assertNotIn("tx_blocked", HP_IO)
        self.assertIn("id: ${hp_id}_recovery_probe_pending", HP_IO)
        self.assertIn("id(${hp_id}_recovery_probe_pending) = true;", HP_IO)
        self.assertIn("id(${hp_id}_recovery_probe_pending) = false;", HP_IO)
        self.assertIn("openquatt_modbus_shim::queue_modbus_read(", HP_IO)

    def test_planner_ownership_and_transports_unchanged(self) -> None:
        self.assertIn("update_interval: never", HP_IO)
        self.assertIn("id(${hp_id}).update();", HP_IO)
        self.assertIn('oq_modbus_startup_delay_ms: "2500"', DUO_PACKAGES)
        self.assertIn("baud_rate: 19200", HUB)
        self.assertIn("parity: EVEN", HUB)
        self.assertIn("turnaround_time: ${oq_modbus_command_throttle_ms}ms", HUB)
        # Slow control readbacks keep their historic 30 s cadence via the
        # slow controller; target R3999 stays on the regular 10 s cadence.
        for entity_id in (
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
