from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
SUPERVISORY = (ROOT / "openquatt" / "oq_supervisory_controlmode.yaml").read_text()
REQUEST = (ROOT / "openquatt" / "oq_thermal_request_control.yaml").read_text()
ACTUATOR = (ROOT / "openquatt" / "oq_thermal_actuator.yaml").read_text()
STRATEGIES = "\n".join(
    (ROOT / "openquatt" / name).read_text()
    for name in (
        "oq_heating_curve_strategy.yaml",
        "oq_power_house_strategy.yaml",
        "oq_cooling_strategy.yaml",
    )
)
POLICY = (
    ROOT / "openquatt" / "includes" / "control" / "oq_compressor_frequency_policy.h"
).read_text()
RUNTIME = (
    ROOT / "openquatt" / "includes" / "control" / "oq_compressor_frequency_runtime.h"
).read_text()
WEB_CONFIG = (ROOT / "openquatt" / "web" / "js" / "src" / "core" / "config.js").read_text()


class CompressorFrequencyPolicyContractTest(unittest.TestCase):
    def test_day_and_silent_caps_have_direct_persistent_defaults(self) -> None:
        defaults = {
            "oq_day_max_frequency_hz": "90",
            "oq_silent_max_frequency_hz": "67",
        }
        for entity_id, initial_value in defaults.items():
            start = SUPERVISORY.index(f"id: {entity_id}")
            block = SUPERVISORY[start : start + 420]
            self.assertIn("restore_value: true", block)
            self.assertIn(f"initial_value: {initial_value}", block)

    def test_each_hp_has_one_persistent_excluded_range_shared_between_modes(self) -> None:
        for boundary in ("min", "max"):
            entity_id = f"${{hp_id}}_excluded_frequency_{boundary}_hz"
            start = HP_IO.index(f"id: {entity_id}")
            block = HP_IO[start : start + 420]
            self.assertIn("restore_value: true", block)
            self.assertIn("initial_value: 0", block)

    def test_invalid_or_unavailable_frequency_state_fails_closed(self) -> None:
        self.assertIn("if (frequency_hz <= 0", POLICY)
        self.assertIn("!valid_frequency_range(excluded)", POLICY)
        self.assertIn("if (frequency_hz > cap_hz) return false", POLICY)
        self.assertIn("return 0;", POLICY)

    def test_request_and_actuator_enforce_the_policy_independently(self) -> None:
        for source in (REQUEST, ACTUATOR):
            self.assertIn("frequency_runtime.pick_allowed_level(", source)
            self.assertIn("oq_frequency_runtime::capture()", source)
        self.assertIn("const bool use_frequency_policy = !manual_hp_service_active", ACTUATOR)
        self.assertIn("Revalidate the final request", REQUEST)

    def test_legacy_level_settings_and_migration_storage_are_removed(self) -> None:
        for removed in (
            "oq_day_max_level",
            "oq_silent_max_level",
            "${hp_id}_excluded_level_a",
            "${hp_id}_excluded_level_b",
            "oq_frequency_caps_migrated",
            "storage_has_migration",
            "store_configured_frequency_hz",
            "shared_cap_frequency",
        ):
            self.assertNotIn(removed, SUPERVISORY + HP_IO + REQUEST + ACTUATOR + POLICY + RUNTIME)
        for removed in ("dayMax:", "silentMax:", "hp1ExcludedA:", "hp2ExcludedA:"):
            self.assertNotIn(removed, WEB_CONFIG)
        self.assertIn('export const FREQUENCY_CAP_KEYS = ["dayMaxHz", "silentMaxHz"]', WEB_CONFIG)
        self.assertIn('"hp1ExcludeMinHz", "hp1ExcludeMaxHz", "hp2ExcludeMinHz", "hp2ExcludeMaxHz"', WEB_CONFIG)

    def test_strategies_only_offer_allowed_runtime_frequencies(self) -> None:
        self.assertEqual(STRATEGIES.count("oq_frequency_runtime::capture()"), 3)
        self.assertEqual(STRATEGIES.count("frequency_runtime.frequency_allowed("), 3)
        self.assertIn("boosted_allowed_level", STRATEGIES)
        self.assertIn("level_allowed(is_hp1, level)", STRATEGIES)

    def test_runtime_inputs_are_captured_once_per_control_callback(self) -> None:
        control_sources = STRATEGIES + REQUEST + ACTUATOR
        self.assertEqual(control_sources.count("oq_frequency_runtime::capture()"), 5)
        for inline_adapter in (
            "auto runtime_frequency_snapshot",
            "auto excluded_frequency_range",
            "auto frequency_cap_hz",
            "auto cooling_cap_hz",
            "auto heating_cap_hz",
        ):
            self.assertNotIn(inline_adapter, control_sources)
        self.assertIn("const auto hp1_snapshot =", RUNTIME)
        self.assertIn("const oq_frequency_policy::FrequencyRange hp2_excluded", RUNTIME)


if __name__ == "__main__":
    unittest.main()
