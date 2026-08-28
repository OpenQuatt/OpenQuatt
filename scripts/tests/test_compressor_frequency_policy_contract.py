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


class CompressorFrequencyPolicyContractTest(unittest.TestCase):
    def test_day_and_silent_caps_are_independent_per_mode(self) -> None:
        for entity_id in (
            "oq_day_heating_max_frequency_hz",
            "oq_day_cooling_max_frequency_hz",
            "oq_silent_heating_max_frequency_hz",
            "oq_silent_cooling_max_frequency_hz",
        ):
            self.assertIn(f"id: {entity_id}", SUPERVISORY)
        self.assertIn("id: oq_frequency_caps_migrated", SUPERVISORY)
        self.assertIn("type: std::array<uint8_t, 22>", SUPERVISORY)
        self.assertIn("configuration_matches_variant(", POLICY)
        self.assertIn("conservative_mode_frequencies(", SUPERVISORY)
        self.assertIn("oq_frequency_policy::store_configured_frequency_hz(", SUPERVISORY)

    def test_each_hp_has_two_excluded_ranges_per_mode(self) -> None:
        for mode in ("heating", "cooling"):
            for range_name in ("a", "b"):
                for boundary in ("min", "max"):
                    self.assertIn(
                        f"id: ${{hp_id}}_excluded_{mode}_range_{range_name}_{boundary}_hz",
                        HP_IO,
                    )
        self.assertIn("snapshot.cooling.valid && snapshot.heating.valid", HP_IO)
        self.assertIn("configuration_matches_variant(", HP_IO)
        self.assertIn("exclusions_migrated_flag(${hp_index})", HP_IO)

    def test_frequency_policy_uses_one_compact_preference_record(self) -> None:
        self.assertIn("using Storage = std::array<uint8_t, STORAGE_SIZE>", POLICY)
        self.assertIn("STORAGE_SIZE = 22", POLICY)
        for source, entity_id in (
            (SUPERVISORY, "oq_day_heating_max_frequency_hz"),
            (SUPERVISORY, "oq_day_cooling_max_frequency_hz"),
            (HP_IO, "${hp_id}_excluded_heating_range_a_min_hz"),
            (HP_IO, "${hp_id}_excluded_cooling_range_b_max_hz"),
        ):
            start = source.index(f"id: {entity_id}")
            block = source[start : start + 520]
            self.assertNotIn("restore_value: true", block)
            self.assertIn("store_configured_frequency_hz(", block)

    def test_shared_caps_restore_from_the_effective_hp1_boot_path(self) -> None:
        self.assertIn("The HP1 package owns restoration", HP_IO)
        for entity_id in (
            "oq_day_heating_max_frequency_hz",
            "oq_day_cooling_max_frequency_hz",
            "oq_silent_heating_max_frequency_hz",
            "oq_silent_cooling_max_frequency_hz",
        ):
            self.assertIn(f"restore_cap(id({entity_id})", HP_IO)

    def test_invalid_or_unavailable_frequency_state_fails_closed(self) -> None:
        self.assertIn("if (frequency_hz <= 0", POLICY)
        self.assertIn("!valid_frequency_range(excluded.a)", POLICY)
        self.assertIn("if (frequency_hz > cap_hz) return false", POLICY)
        self.assertIn("return 0;", POLICY)

    def test_request_and_actuator_enforce_the_policy_independently(self) -> None:
        for source in (REQUEST, ACTUATOR):
            self.assertIn("oq_frequency_policy::pick_allowed_level(", source)
            self.assertIn("oq_frequency_caps_migrated", source)
            self.assertIn("storage_has_migration(", source)
        self.assertIn("!manual_hp_service_active && frequency_policy_active", ACTUATOR)
        self.assertIn("Revalidate the final request", REQUEST)

    def test_legacy_settings_remain_reachable_for_migration_and_old_backups(self) -> None:
        for source, entity_id in (
            (SUPERVISORY, "oq_day_max_level"),
            (SUPERVISORY, "oq_silent_max_level"),
            (HP_IO, "${hp_id}_excluded_level_a"),
            (HP_IO, "${hp_id}_excluded_level_b"),
        ):
            start = source.index(f"id: {entity_id}")
            block = source[start : start + 240]
            self.assertNotIn("internal: true", block)
        self.assertEqual(SUPERVISORY.count("clear_migrated("), 2)
        self.assertEqual(HP_IO.count("clear_migrated("), 2)
        self.assertIn("conservative_mode_frequencies(", SUPERVISORY)

    def test_strategies_only_offer_allowed_runtime_frequencies(self) -> None:
        self.assertEqual(
            STRATEGIES.count("oq_frequency_policy::frequency_allowed("), 3
        )
        self.assertEqual(
            STRATEGIES.count("oq_frequency_policy::automatic_frequency_hz("), 3
        )
        self.assertIn("boosted_allowed_level", STRATEGIES)
        self.assertIn("level_allowed(is_hp1, level)", STRATEGIES)


if __name__ == "__main__":
    unittest.main()
