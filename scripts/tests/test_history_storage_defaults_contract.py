from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMMON_YAML = (ROOT / "openquatt" / "oq_common.yaml").read_text()
DECISION_LOG_HEADER = (
    ROOT / "components" / "openquatt_decision_log" / "OpenQuattDecisionLog.h"
).read_text()


def switch_block(switch_id: str) -> str:
    marker = f"    id: {switch_id}\n"
    start = COMMON_YAML.index(marker)
    end = COMMON_YAML.find("\n  - platform:", start + len(marker))
    return COMMON_YAML[start : end if end >= 0 else len(COMMON_YAML)]


class HistoryStorageDefaultsContractTest(unittest.TestCase):
    def test_persistent_energy_and_decision_history_default_on(self) -> None:
        for switch_id in (
            "oq_lifetime_energy_history_switch",
            "oq_decision_log_flash_switch",
        ):
            block = switch_block(switch_id)
            self.assertIn("restore_mode: RESTORE_DEFAULT_ON", block)
            self.assertNotIn("restore_mode: ALWAYS_ON", block)

    def test_all_persistent_history_defaults_on_for_phase_two(self) -> None:
        block = switch_block("oq_trend_history_flash_switch")
        self.assertIn("restore_mode: RESTORE_DEFAULT_ON", block)
        self.assertNotIn("restore_mode: ALWAYS_ON", block)

    def test_decision_log_event_storm_bounds_remain_active(self) -> None:
        self.assertIn("URGENT_FLUSH_COALESCE_US = 2ULL * 1000ULL * 1000ULL", DECISION_LOG_HEADER)
        self.assertIn("URGENT_FLUSH_MIN_INTERVAL_US = 15ULL * 1000ULL * 1000ULL", DECISION_LOG_HEADER)
        self.assertIn("URGENT_FLUSH_RETRY_US = 30ULL * 1000ULL * 1000ULL", DECISION_LOG_HEADER)
        self.assertIn("URGENT_FLUSH_MAX_BATCHES = 4U", DECISION_LOG_HEADER)


if __name__ == "__main__":
    unittest.main()
