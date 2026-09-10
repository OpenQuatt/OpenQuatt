from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
LOGIC = (ROOT / "openquatt/includes/control/oq_cooling_start_block_logic.h").read_text()
DIAGNOSTICS = (ROOT / "openquatt/includes/control/oq_cooling_start_block_runtime.h").read_text()
ACTUATOR = (ROOT / "openquatt/includes/control/oq_thermal_actuator_runtime.h").read_text()
STRATEGY = (ROOT / "openquatt/oq_cooling_strategy.yaml").read_text()
CONFIG = (ROOT / "openquatt/web/js/src/core/config.js").read_text()
SUBSTITUTIONS = (ROOT / "openquatt/oq_substitutions_common.yaml").read_text()


def entity_block(source: str, entity_id: str) -> str:
    start = source.index(f"id: {entity_id}")
    end = source.find("\n  - platform:", start + 1)
    interval = source.find("\ninterval:", start + 1)
    cut = min([i for i in (end, interval) if i >= 0], default=len(source))
    return source[start:cut]


class CoolingStartBlockContractTest(unittest.TestCase):
    def test_pure_logic_distinguishes_cooling_from_general_without_countdown_invention(self) -> None:
        for reason in (
            '"Ready"',
            '"Cooling minimum off-time"',
            '"Waiting for confirmed cooling stop"',
            '"Compressor restart protection"',
            '"Startup inhibit after reboot"',
            '"Compressor start limit (6/hour)"',
            '"Compressor start blocked"',
        ):
            self.assertIn(reason, LOGIC)
        # Actuator contract order: shared cooling rest before per-HP rest
        # before start quota, other blocks never invent a countdown.
        cooling = LOGIC.index("if (in.cooling_remaining_ms > 0)")
        confirm = LOGIC.index("if (in.cooling_confirmation_pending)")
        rest = LOGIC.index("if (rest_remaining_ms > 0)")
        limit = LOGIC.index("if (limit_remaining_ms > 0)")
        other = LOGIC.index("if (in.dispatch_blocked_other)")
        self.assertLess(cooling, confirm)
        self.assertLess(confirm, rest)
        self.assertLess(rest, limit)
        self.assertLess(limit, other)
        self.assertIn("any_hp_running", LOGIC)
        self.assertIn("cooling_demand_active", LOGIC)

    def test_actuator_reports_read_only_without_changing_control_timing(self) -> None:
        # Diagnostics live outside the actuator command path; the actuator only
        # exposes its start-quota remaining. Control timing is unchanged.
        self.assertIn("uint32_t start_limit_remaining_ms(bool is_hp1, uint32_t now_ms)", ACTUATOR)
        for method in (
            "oq_cooling_start_block::Inputs inputs(",
            "const char* reason(",
            "float remaining_s(",
            "float hp_minimum_off_remaining_s(",
            "bool stop_confirmation_pending(",
        ):
            self.assertIn(method, DIAGNOSTICS)
        self.assertIn("oq_cooling_start_block::resolve(", DIAGNOSTICS)
        for forbidden in ("write_level(", "make_call()", "invalidate_restart_credit", "record_transition("):
            self.assertNotIn(forbidden, DIAGNOSTICS)
        # Protection times themselves are untouched.
        self.assertIn('oq_hp_min_off_s: "240"', SUBSTITUTIONS)
        self.assertIn('oq_cooling_minimum_off_min_s: "240"', SUBSTITUTIONS)

    def test_diagnostic_entities_are_polled_without_new_command_intervals(self) -> None:
        for entity_id, name in (
            ("cooling_start_block_reason", 'name: "Cooling Start Block Reason"'),
            ("oq_cooling_start_block_remaining_sensor", 'name: "Cooling Start Block Remaining"'),
            ("oq_hp1_minimum_off_remaining_sensor", 'name: "HP1 - Minimum Off Remaining"'),
            ("oq_hp2_minimum_off_remaining_sensor", 'name: "HP2 - Minimum Off Remaining"'),
            ("cooling_stop_confirmation_pending", 'name: "Cooling Stop Confirmation Pending"'),
        ):
            self.assertIn(f"id: {entity_id}", STRATEGY)
            self.assertIn(name, STRATEGY)
        for entity_id in (
            "cooling_start_block_reason",
            "oq_cooling_start_block_remaining_sensor",
            "oq_hp1_minimum_off_remaining_sensor",
            "oq_hp2_minimum_off_remaining_sensor",
        ):
            block = entity_block(STRATEGY, entity_id)
            self.assertIn("update_interval: 5s", block)
        self.assertIn(
            "oq_cooling_start_block_runtime::runtime().reason(", STRATEGY
        )
        self.assertIn(
            "oq_cooling_start_block_runtime::runtime().remaining_s(", STRATEGY
        )
        # No new command scheduling: the actuator tick keeps its single
        # heat-loop interval with unchanged protection parameters.
        self.assertEqual(STRATEGY.count("oq_thermal_actuator_runtime::runtime().tick({"), 0)
        actuator_yaml = (ROOT / "openquatt/oq_thermal_actuator.yaml").read_text()
        self.assertEqual(actuator_yaml.count("oq_thermal_actuator_runtime::runtime().tick({"), 1)

    def test_web_contract_uses_effective_block_additively(self) -> None:
        for key in (
            "coolingStartBlockReason",
            "coolingStartBlockRemaining",
            "hp1MinimumOffRemaining",
            "hp2MinimumOffRemaining",
            "coolingStopConfirmationPending",
        ):
            self.assertIn(f'"{key}"', CONFIG)
        for key in ("requestReason", "coolingRequestHp1Level", "coolingRequestOwnerHp"):
            self.assertIn(f'"{key}"', CONFIG)


if __name__ == "__main__":
    unittest.main()
