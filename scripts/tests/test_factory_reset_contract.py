from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMMON = (ROOT / "openquatt" / "oq_common.yaml").read_text()
RUNTIME_PACKAGE = (ROOT / "openquatt" / "packages" / "10_runtime.yaml").read_text()


class FactoryResetContractTest(unittest.TestCase):
    def test_fast_power_cycle_settings_and_logging_are_pinned(self) -> None:
        self.assertIn("factory_reset:\n  resets_required: 3\n  max_delay: 10s", COMMON)
        self.assertIn(
            'format: "Fast power cycle count now %u, target %u"',
            COMMON,
        )
        self.assertIn("args: [x, target]", COMMON)

    def test_factory_reset_button_is_exposed_to_home_assistant(self) -> None:
        self.assertIn("platform: factory_reset", COMMON)
        self.assertIn("id: factory_reset_button", COMMON)
        self.assertIn(
            "button:\n  - id: !extend factory_reset_button\n"
            "    internal: false\n"
            "    entity_category: config",
            RUNTIME_PACKAGE,
        )


if __name__ == "__main__":
    unittest.main()
