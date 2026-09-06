from pathlib import Path
import sys
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


class NvsPersistenceContractTest(unittest.TestCase):
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
        self.assertEqual(check_nvs_budget.CUSTOM_PREFERENCE_ENTRIES, 42)

    def test_retired_flow_pwm_preferences_are_cleaned_up(self) -> None:
        self.assertIn("435184091U", FLOW_CONTROL)
        self.assertIn("3242211636U", FLOW_CONTROL)
        self.assertIn('"retired flow PWM entities"', FLOW_CONTROL)

    def test_cleanup_is_targeted_and_never_erases_the_partition(self) -> None:
        self.assertIn('nvs_open(ESPHOME_NAMESPACE, NVS_READWRITE', NVS_CLEANUP)
        self.assertIn("nvs_erase_key", NVS_CLEANUP)
        self.assertNotIn("nvs_flash_erase", NVS_CLEANUP)

    def test_budget_math_and_validation_integration(self) -> None:
        self.assertEqual(check_nvs_budget.blob_entries(1), 3)
        self.assertEqual(check_nvs_budget.blob_entries(32), 3)
        self.assertEqual(check_nvs_budget.blob_entries(33), 4)
        self.assertEqual(check_nvs_budget.blob_entries(256), 10)
        self.assertEqual(check_nvs_budget.cpp_type_bytes("uint32_t[3]"), 12)
        self.assertEqual(check_nvs_budget.REQUIRED_AVAILABLE_ENTRIES, 126)
        self.assertIn("check_nvs_budget.py", DEV)

    def test_custom_blob_sizes_are_compile_time_budget_contracts(self) -> None:
        self.assertIn("sizeof(StateStorage) == 56U", CRASH_HEADER)
        self.assertIn("sizeof(AuthStorage) == 104U", AUTH_HEADER)


if __name__ == "__main__":
    unittest.main()
