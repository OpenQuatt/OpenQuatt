from pathlib import Path
import hashlib
import re
import sys
import importlib.util
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import check_nvs_budget  # noqa: E402


ODU_SERVICE = (ROOT / "openquatt/experimental/oq_odu_runtime_frequency_service_hp.yaml").read_text()
ODU_RUNTIME_HEADER = (
    ROOT
    / "components/openquatt_odu_runtime_frequency/OpenQuattOduRuntimeFrequency.h"
).read_text()
ODU_RUNTIME_SOURCE = (
    ROOT
    / "components/openquatt_odu_runtime_frequency/OpenQuattOduRuntimeFrequency.cpp"
).read_text()
ODU_SETTINGS_HEADER = (
    ROOT / "components/openquatt_odu_settings/OpenQuattOduSettings.h"
).read_text()
ODU_SETTINGS_SOURCE = (
    ROOT / "components/openquatt_odu_settings/OpenQuattOduSettings.cpp"
).read_text()
ODU_SETTINGS_LOGIC = (
    ROOT / "openquatt/includes/odu/oq_odu_bottom_plate_settings.h"
).read_text()
HP_IO = (ROOT / "openquatt/oq_HP_io.yaml").read_text()
API_INGRESS = (ROOT / "openquatt/oq_api_ingress.yaml").read_text()
FLOW_CONTROL = (ROOT / "openquatt/oq_flow_control.yaml").read_text()
NVS_CLEANUP = (ROOT / "openquatt/includes/storage/oq_nvs_cleanup.h").read_text()
DEV = (ROOT / "scripts/dev.py").read_text()
CRASH_HEADER = (ROOT / "components/openquatt_crash_telemetry/OpenQuattCrashTelemetry.h").read_text()
AUTH_HEADER = (ROOT / "components/openquatt_web_auth/OpenQuattWebAuth.h").read_text()
AIR_PURGE = (ROOT / "openquatt/oq_air_purge.yaml").read_text()
INSTALLATION_MONITORING = (ROOT / "openquatt/oq_installation_monitoring.yaml").read_text()
USAGE_TELEMETRY = (ROOT / "openquatt/oq_usage_telemetry.yaml").read_text()
ENERGY = (ROOT / "openquatt/oq_energy.yaml").read_text()
HEATING_CURVE = (ROOT / "openquatt/oq_heating_curve_strategy.yaml").read_text()


def yaml_entry(source: str, entity_id: str) -> str:
    """Select one top-level entity/global list item without requiring ESPHome."""
    for match in re.finditer(r"(?ms)^  - (?:id|platform): .*?(?=^  - |^[a-zA-Z_]|\Z)", source):
        block = match.group()
        if re.search(r"(?m)^\s*(?:- )?id: " + re.escape(entity_id) + r"\s*$", block):
            return block
    raise AssertionError(f"Missing YAML entry {entity_id}")


class NvsPersistenceContractTest(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec("esphome"), "ESPHome is required for package composition")
    def test_retirements_survive_real_target_package_composition(self) -> None:
        from esphome.config import read_config
        from esphome.core import CORE

        # Raw YAML assertions cannot detect hooks replaced by later packages.
        for target in ("duo.yaml", "duo_hil.yaml", "single.yaml"):
            with self.subTest(target=target):
                CORE.reset()
                CORE.config_path = ROOT / "configs/heatpump_controller_q" / target
                config = read_config({})
                self.assertIsNotNone(config)
                hooks = config["esphome"]["on_boot"]
                matching = [hook for hook in hooks if "retire_openquatt_preferences" in str(hook["then"])]
                self.assertEqual(len(matching), 1)
                self.assertEqual(matching[0]["priority"], -100)
                self.assertIn('erase_esphome_blob_if_size(306736601U, 1U', NVS_CLEANUP)
                self.assertIn('erase_esphome_blob_if_size(2881445393U, 1U', NVS_CLEANUP)
                night_day_id = "oq_cooling_fallback_night_min_last_day_key"
                globals_by_id = {str(item["id"]): item for item in config["globals"]}
                self.assertFalse(globals_by_id[night_day_id]["restore_value"])
                self.assertEqual(str(globals_by_id[night_day_id]["initial_value"]), "-1")
                self.assertTrue(globals_by_id["oq_cooling_fallback_night_min_last_c"]["restore_value"])
                day_key = 1944399030 ^ int(hashlib.md5(night_day_id.encode()).hexdigest()[:8], 16)
                self.assertIn(f'erase_esphome_blob_if_size({day_key}U, 4U', NVS_CLEANUP)
                for retired_id in ("oq_flow_cooling_settings_migrated", "oq_aux_heat_source_policy_migrated"):
                    self.assertNotIn(retired_id, globals_by_id)
                    self.assertNotIn(f"id({retired_id})", str(hooks))
                    key = 1944399030 ^ int(hashlib.md5(retired_id.encode()).hexdigest()[:8], 16)
                    self.assertIn(f'erase_esphome_blob_if_size({key}U, 1U', NVS_CLEANUP)
                for entity_id in ("oq_flow_last_good_pwm", "oq_flow_last_good_pwm_cooling"):
                    self.assertTrue(globals_by_id[entity_id]["restore_value"])
        CORE.reset()

    def test_cycling_alerts_have_ram_defaults_and_exact_retired_keys(self) -> None:
        expected_defaults = {
            "oq_compressor_cycling_alert_latched": "false",
            "oq_compressor_cycling_alert_first_seen_epoch": "0",
            "oq_compressor_cycling_alert_last_seen_epoch": "0",
            "oq_compressor_cycling_alert_alternating": "false",
            "oq_compressor_cycling_alert_hp1_peak_2h_value": "0",
            "oq_compressor_cycling_alert_hp1_peak_72h_value": "0",
            "oq_compressor_cycling_alert_hp2_peak_2h_value": "0",
            "oq_compressor_cycling_alert_hp2_peak_72h_value": "0",
        }
        keys = {name: int(key) for key, name in re.findall(r"(\d+)U,\s*// (oq_compressor_cycling_alert_\w+)", NVS_CLEANUP)}
        self.assertEqual(keys.keys(), expected_defaults.keys())
        for entity_id, default in expected_defaults.items():
            with self.subTest(entity_id=entity_id):
                block = yaml_entry(INSTALLATION_MONITORING, entity_id)
                self.assertIn("restore_value: false", block)
                self.assertIn(f"initial_value: '{default}'", block)
                name_hash = int(hashlib.md5(entity_id.encode()).hexdigest()[:8], 16)
                self.assertEqual(keys[entity_id], 1944399030 ^ name_hash)
        self.assertIn("retired_cycling_alert_preferences", NVS_CLEANUP)
        self.assertIn("erase_esphome_preferences(", NVS_CLEANUP)

    def test_air_purge_choice_starts_on_and_retires_its_old_entity_preference(self) -> None:
        block = yaml_entry(AIR_PURGE, "oq_air_purge_return_to_auto")
        self.assertIn("restore_mode: ALWAYS_ON", block)
        self.assertNotIn("RESTORE_DEFAULT_ON", block)
        self.assertIn("retire_openquatt_preferences(id(oq_air_purge_return_to_auto))", USAGE_TELEMETRY)
        self.assertIn("erase_entity_preferences(", NVS_CLEANUP)

    def test_operational_state_and_user_warning_limits_remain_persistent(self) -> None:
        for entity_id in ("oq_flow_last_good_pwm", "oq_flow_last_good_pwm_cooling"):
            self.assertIn("restore_value: true", yaml_entry(FLOW_CONTROL, entity_id))
        for entity_id in ("oq_compressor_starts_warning_limit_2h", "oq_compressor_starts_warning_limit_72h"):
            self.assertIn("restore_value: true", yaml_entry(INSTALLATION_MONITORING, entity_id))
        for entity_id in ("oq_system_thermal_energy_daily", "oq_system_thermal_energy_cumulative"):
            self.assertIn("restore: true", yaml_entry(ENERGY, entity_id))
        self.assertIn("id: oq_heating_curve_pid", HEATING_CURVE)
        self.assertIn("platform: pid", HEATING_CURVE)

    def test_odu_editor_is_ram_only_and_requires_a_current_boot_load(self) -> None:
        runtime_service = ODU_RUNTIME_HEADER + ODU_RUNTIME_SOURCE
        self.assertNotIn("restore_value:", ODU_SERVICE)
        self.assertNotIn("preferences", runtime_service.lower())
        self.assertIn("loaded_", ODU_RUNTIME_HEADER)
        self.assertIn('return "load_required";', ODU_RUNTIME_SOURCE)
        self.assertIn("std::array<uint32_t, 42>", ODU_SERVICE)
        self.assertIn("fnv1_hash_object_id", ODU_SERVICE)
        self.assertIn("erase_esphome_preferences", ODU_SERVICE)

    def test_api_enable_inputs_are_session_state(self) -> None:
        self.assertEqual(API_INGRESS.count("restore_mode: ALWAYS_OFF"), 2)
        self.assertNotIn("restore_mode: RESTORE_DEFAULT_OFF", API_INGRESS)
        self.assertIn('"API ingress enable state"', API_INGRESS)

    def test_bottom_plate_profiles_are_small_verified_preferences(self) -> None:
        settings_service = ODU_SETTINGS_HEADER + ODU_SETTINGS_SOURCE
        self.assertIn(
            "sizeof(BottomPlateProfileStorage) == 16U", ODU_SETTINGS_LOGIC
        )
        self.assertIn("make_preference<oq_odu::BottomPlateProfileStorage>", settings_service)
        self.assertIn("global_preferences->sync()", settings_service)
        self.assertIn("profile_pref_.load(&verify)", settings_service)
        self.assertIn("identity_matches_profile_", settings_service)
        self.assertIn("pending_profile_", settings_service)
        self.assertIn("manual_apply_pending_", settings_service)
        self.assertEqual(
            check_nvs_budget.estimate_custom_preferences({"openquatt_odu_settings": [{}, {}]}),
            {"openquatt_odu_settings": 6},
        )

    def test_retired_flow_pwm_preferences_are_cleaned_up(self) -> None:
        self.assertIn("435184091U", FLOW_CONTROL)
        self.assertIn("3242211636U", FLOW_CONTROL)
        self.assertIn('"retired flow PWM entities"', FLOW_CONTROL)

    def test_cleanup_is_targeted_and_never_erases_the_partition(self) -> None:
        self.assertIn('nvs_open(ESPHOME_NAMESPACE, NVS_READWRITE', NVS_CLEANUP)
        self.assertIn("nvs_erase_key", NVS_CLEANUP)
        self.assertNotIn("nvs_flash_erase", NVS_CLEANUP)

    def test_retired_calibration_keys_are_cleaned_without_import_or_current_record_deletion(self) -> None:
        legacy_ids = (
            "oq_water_supply_temp_calibration_source_code",
            "oq_water_supply_temp_calibration_source_fingerprint",
            "oq_water_supply_temp_calibration_checksum",
        )
        current_ids = tuple(
            f"oq_water_supply_temp_calibration_{kind}_record"
            for kind in ("pt1000", "ds18b20", "cic", "ha_input")
        )
        for entity_id in legacy_ids:
            with self.subTest(entity_id=entity_id):
                key = 1944399030 ^ int(hashlib.md5(entity_id.encode()).hexdigest()[:8], 16)
                self.assertIn(f'erase_esphome_blob_if_size({key}U, 4U', NVS_CLEANUP)
        yaml = (ROOT / "openquatt/oq_sensor_sources.yaml").read_text()
        calibration = (ROOT / "openquatt/includes/service/tasks/oq_hp_water_calibration_logic.h").read_text()
        for entity_id in legacy_ids:
            self.assertNotIn(entity_id, yaml)
            self.assertNotIn(f"id({entity_id})", calibration)
        for entity_id in current_ids:
            self.assertIn(entity_id, yaml)
            key = 1944399030 ^ int(hashlib.md5(entity_id.encode()).hexdigest()[:8], 16)
            self.assertNotRegex(NVS_CLEANUP, rf"\b{key}U\b")
        runtime = (ROOT / "openquatt/includes/control/oq_sensor_source_runtime.h").read_text()
        self.assertNotIn("migrate_legacy_calibration", runtime)
        self.assertNotIn("oq_supply_calibration_migration", runtime)
        self.assertIn("id(water_supply_temp_calibration_offset)", calibration)

    def test_budget_math_and_validation_integration(self) -> None:
        self.assertEqual(check_nvs_budget.blob_entries(1), 3)
        self.assertEqual(check_nvs_budget.blob_entries(32), 3)
        self.assertEqual(check_nvs_budget.blob_entries(33), 4)
        self.assertEqual(check_nvs_budget.blob_entries(256), 10)
        self.assertEqual(check_nvs_budget.cpp_type_bytes("uint32_t[3]"), 12)
        self.assertEqual(check_nvs_budget.REQUIRED_AVAILABLE_ENTRIES, 100)
        self.assertIn("check_nvs_budget.py", DEV)

    def test_custom_blob_sizes_are_compile_time_budget_contracts(self) -> None:
        self.assertIn("sizeof(StateStorage) == 56U", CRASH_HEADER)
        self.assertIn("sizeof(AuthStorage) == 104U", AUTH_HEADER)


if __name__ == "__main__":
    unittest.main()
