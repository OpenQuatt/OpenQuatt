from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMMON = (ROOT / "openquatt" / "oq_common.yaml").read_text()
RUNTIME_PACKAGE = (ROOT / "openquatt" / "packages" / "10_runtime.yaml").read_text()


class FactoryResetContractTest(unittest.TestCase):
    def test_fast_power_cycle_settings_and_logging_are_pinned(self) -> None:
        counter = COMMON.split("factory_reset:\n", 1)[1].split("\nota:", 1)[0]
        self.assertIn("id: oq_factory_reset_counter", counter)
        self.assertIn("setup_priority: 246", counter)  # After EEPROM journal checks at 247.
        self.assertIn("resets_required: 3\n  max_delay: 10s", counter)
        self.assertIn(
            'format: "Fast power cycle count now %u, target %u"',
            COMMON,
        )
        self.assertIn("args: [x, target]", COMMON)

    def test_factory_reset_button_is_exposed_to_home_assistant(self) -> None:
        button = (
            '  - platform: template\n'
            '    id: factory_reset_button\n'
            '    name: "Factory reset"\n'
            '    entity_category: config'
        )
        self.assertIn(button, COMMON)
        reset_action = COMMON.split("    id: factory_reset_button\n", 1)[1].split("\nswitch:", 1)[0]
        self.assertIn("device_class: restart", reset_action)
        self.assertIn("icon: mdi:restart-alert", reset_action)
        self.assertIn("lambda: return oq_eeprom::factory_reset_allowed();", reset_action)
        delayed = reset_action.split("- delay: 100ms", 1)[1]
        self.assertLess(delayed.index("if (!oq_eeprom::factory_reset_allowed()) return;"),
                        delayed.index("global_preferences->reset();"))
        self.assertNotIn(
            "id: factory_reset_button\n    internal: true",
            COMMON,
        )
        self.assertNotIn("oq_factory_reset", RUNTIME_PACKAGE)
        self.assertNotIn("!extend factory_reset_button", RUNTIME_PACKAGE)


if __name__ == "__main__":
    unittest.main()
