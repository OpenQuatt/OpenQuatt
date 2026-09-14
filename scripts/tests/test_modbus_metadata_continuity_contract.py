from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt/oq_HP_io.yaml").read_text()
INCIDENT_MANAGER_HEADER = (
    ROOT / "components/openquatt_incident_manager/OpenQuattIncidentManager.h"
).read_text()
INCIDENT_MANAGER_SOURCE = (
    ROOT / "components/openquatt_incident_manager/OpenQuattIncidentManager.cpp"
).read_text()


def yaml_block(source: str, start: str, end: str) -> str:
    return source[source.index(start):source.index(end, source.index(start))]


class ModbusMetadataContinuityContractTest(unittest.TestCase):
    def test_runtime_snapshot_replaces_only_after_complete_validation(self) -> None:
        loader = yaml_block(
            HP_IO,
            "id: ${hp_id}_load_runtime_frequency_table_once",
            "id: ${hp_id}_detect_odu_generation",
        )
        self.assertIn("if (!snapshot.heating.valid || !snapshot.cooling.valid)", loader)
        self.assertIn("extension invalid; keeping previous snapshot", loader)
        self.assertIn("runtime_frequency_revalidation_required) = false", loader)
        self.assertNotIn("partially invalid; affected mode limited to F10", loader)

    def test_transport_flap_preserves_control_metadata_and_rechecks_it(self) -> None:
        offline = yaml_block(HP_IO, "on_offline:", "on_online:")
        online = yaml_block(HP_IO, "on_online:", "openquatt_odu_eeprom_dump:")
        detection = yaml_block(
            HP_IO,
            "id: ${hp_id}_detect_odu_generation_once",
            "id: ${hp_id}_load_runtime_frequency_table_once",
        )
        for value in (
            "CompressorLevelProfile::UNKNOWN",
            "runtime_frequency_snapshot_storage) = {}",
            'generation).publish_state("Unknown")',
        ):
            self.assertNotIn(value, offline)
        for value in (
            "compressor_level_profile_request_token",
            "odu_generation_revalidation_required",
            "runtime_frequency_revalidation_required",
            "odu_runtime_frequency)->reset_runtime_state",
            "Verification failed: ODU disconnected during write",
        ):
            self.assertIn(value, offline)
        self.assertIn("detect_odu_generation_once", online)
        for value in (
            "reset_runtime_state",
            "notify_odu_offline",
            "runtime_frequency_snapshot_storage) =",
        ):
            self.assertNotIn(value, detection)

    def test_interrupted_write_is_an_explicit_incident_manager_stop(self) -> None:
        offline = yaml_block(HP_IO, "on_offline:", "on_online:")
        detection = yaml_block(
            HP_IO,
            "id: ${hp_id}_detect_odu_generation_once",
            "id: ${hp_id}_load_runtime_frequency_table_once",
        )
        self.assertIn("runtime_reload_blocked_after_write()", offline)
        self.assertIn("observe_runtime_frequency_mapping(", offline)
        self.assertIn("invalidate_runtime_frequency_reload", detection)
        self.assertIn("odu_generation_detection_complete) = false", detection)
        self.assertIn("observe_runtime_frequency_mapping", INCIDENT_MANAGER_HEADER)
        self.assertIn("runtime_frequency_mapping_valid", INCIDENT_MANAGER_SOURCE)
        self.assertIn("outputs.must_stop = true", INCIDENT_MANAGER_SOURCE)
        self.assertIn("kRuntimeFrequencyMappingIncidentId", INCIDENT_MANAGER_SOURCE)


if __name__ == "__main__":
    unittest.main()
