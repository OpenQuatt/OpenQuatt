from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HIL_CONFIG = ROOT / "tests" / "hil" / "crash_telemetry_q_duo_wifi.yaml"


class CrashHilConfigTests(unittest.TestCase):
    def test_crash_triggers_are_test_only(self) -> None:
        targets = (ROOT / "build_targets.yaml").read_text(encoding="utf-8")
        self.assertNotIn("tests/hil", targets)
        self.assertNotIn(HIL_CONFIG.name, targets)

    def test_abort_fault_and_watchdog_triggers_are_present(self) -> None:
        config = HIL_CONFIG.read_text(encoding="utf-8")
        self.assertIn("../../configs/heatpump_controller_q/duo_wifi.yaml", config)
        self.assertIn("std::abort();", config)
        self.assertIn("reinterpret_cast<volatile uint32_t *>", config)
        self.assertIn('asm volatile("nop")', config)
        self.assertEqual(3, config.count("disabled_by_default: true"))


if __name__ == "__main__":
    unittest.main()
