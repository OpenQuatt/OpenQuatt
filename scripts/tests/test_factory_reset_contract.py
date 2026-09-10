from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
FACTORY_RESET = (ROOT / "openquatt" / "oq_factory_reset.yaml").read_text()
RUNTIME_PACKAGE = (ROOT / "openquatt" / "packages" / "10_runtime.yaml").read_text()


class FactoryResetContractTest(unittest.TestCase):
    def test_fast_power_cycle_settings_and_logging_are_pinned(self) -> None:
        self.assertIn(
            "factory_reset:\n  resets_required: 3\n  max_delay: 10s",
            FACTORY_RESET,
        )
        self.assertIn(
            'format: "Fast power cycle count now %u, target %u"',
            FACTORY_RESET,
        )
        self.assertIn("args: [x, target]", FACTORY_RESET)

    def test_factory_reset_button_is_exposed_to_home_assistant(self) -> None:
        button = (
            '  - platform: factory_reset\n'
            '    id: factory_reset_button\n'
            '    name: "Factory reset"\n'
            '    entity_category: config'
        )
        self.assertIn(button, FACTORY_RESET)
        self.assertNotIn("internal: true", FACTORY_RESET)
        self.assertIn(
            "oq_factory_reset: !include ../oq_factory_reset.yaml",
            RUNTIME_PACKAGE,
        )
        self.assertNotIn("!extend factory_reset_button", RUNTIME_PACKAGE)


if __name__ == "__main__":
    unittest.main()
