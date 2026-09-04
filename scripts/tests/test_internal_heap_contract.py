from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMMON_HEADER = (
    ROOT / "components" / "openquatt_common" / "PsramBuffer.h"
).read_text()
INCIDENT_HEADER = (
    ROOT
    / "components"
    / "openquatt_incident_manager"
    / "OpenQuattIncidentManager.h"
).read_text()
INCIDENT_CPP = (
    ROOT
    / "components"
    / "openquatt_incident_manager"
    / "OpenQuattIncidentManager.cpp"
).read_text()
TELEMETRY_HEADER = (
    ROOT
    / "components"
    / "openquatt_usage_telemetry"
    / "OpenQuattUsageTelemetry.h"
).read_text()
TELEMETRY_CPP = (
    ROOT
    / "components"
    / "openquatt_usage_telemetry"
    / "OpenQuattUsageTelemetry.cpp"
).read_text()
TELEMETRY_CODEGEN = (
    ROOT / "components" / "openquatt_usage_telemetry" / "__init__.py"
).read_text()
TELEMETRY_POLICY = (
    ROOT
    / "components"
    / "openquatt_usage_telemetry"
    / "OpenQuattUsageTelemetryPolicy.h"
).read_text()
CRASH_TELEMETRY_HEADER = (
    ROOT
    / "components"
    / "openquatt_crash_telemetry"
    / "OpenQuattCrashTelemetry.h"
).read_text()
CRASH_TELEMETRY_CPP = (
    ROOT
    / "components"
    / "openquatt_crash_telemetry"
    / "OpenQuattCrashTelemetryMqtt.cpp"
).read_text()
CRASH_TELEMETRY_CODEGEN = (
    ROOT / "components" / "openquatt_crash_telemetry" / "__init__.py"
).read_text()
LOG_HISTORY_CPP = (
    ROOT
    / "components"
    / "openquatt_log_history"
    / "OpenQuattLogHistory.cpp"
).read_text()
DEBUG_RECORDER_CPP = (
    ROOT
    / "components"
    / "openquatt_debug_recorder"
    / "OpenQuattDebugRecorder.cpp"
).read_text()
TRENDS_CPP = (
    ROOT / "components" / "openquatt_trends" / "OpenQuattTrends.cpp"
).read_text()
LOG_HISTORY_HEADER = (
    ROOT
    / "components"
    / "openquatt_log_history"
    / "OpenQuattLogHistory.h"
).read_text()
HP_WATER_CALIBRATION_HEADER = (
    ROOT
    / "openquatt"
    / "includes"
    / "service"
    / "tasks"
    / "oq_hp_water_calibration_logic.h"
).read_text()


class InternalHeapPlacementContractTest(unittest.TestCase):
    def test_incident_runtime_and_snapshots_are_external_only(self) -> None:
        self.assertIn("class PsramObjectArray", COMMON_HEADER)
        self.assertIn(
            "PsramObjectArray<ExternalState, 1U> external_state_",
            INCIDENT_HEADER,
        )
        self.assertIn(
            "std::array<PublishedSnapshot, 3U> snapshots",
            INCIDENT_HEADER,
        )
        self.assertIn("sizeof(OpenQuattIncidentManager) <= 512U", INCIDENT_HEADER)
        self.assertNotIn(
            "std::unique_ptr<PublishedSnapshot>",
            INCIDENT_CPP,
        )
        self.assertNotIn(
            "new (std::nothrow) PublishedSnapshot",
            INCIDENT_CPP,
        )

    def test_snapshot_publication_avoids_large_spinlock_copy(self) -> None:
        self.assertIn(
            "xSemaphoreCreateMutexStatic(&this->snapshot_mutex_storage_)",
            INCIDENT_CPP,
        )
        self.assertIn(
            "std::swap(this->published_snapshot_index_",
            INCIDENT_CPP,
        )
        self.assertNotIn("portENTER_CRITICAL", INCIDENT_CPP)

    def test_s3_telemetry_worker_and_payload_use_psram(self) -> None:
        self.assertIn("StaticTask worker_task_state_", TELEMETRY_HEADER)
        self.assertIn(
            "MQTT_WORKER_STACK_IN_PSRAM = true",
            TELEMETRY_HEADER,
        )
        self.assertIn("TELEMETRY_PAYLOAD_CAPACITY + 1U", TELEMETRY_CPP)
        self.assertIn(
            "FixedBufferWriter payload(this->payload_.data()",
            TELEMETRY_CPP,
        )
        self.assertNotIn("std::string payload;", TELEMETRY_CPP)
        self.assertIn("psram.request_external_task_stack()", TELEMETRY_CODEGEN)
        self.assertIn(
            "get_esp32_variant() == VARIANT_ESP32S3",
            TELEMETRY_CODEGEN,
        )
        self.assertNotIn("xTaskCreatePinnedToCore(", TELEMETRY_CPP)
        self.assertNotIn("vTaskDelete(nullptr)", TELEMETRY_CPP)

    def test_classic_esp32_worker_remains_internal(self) -> None:
        self.assertIn(
            "MQTT_WORKER_STACK_IN_PSRAM = false",
            TELEMETRY_HEADER,
        )
        self.assertIn(
            "this->worker_task_state_.deallocate();",
            TELEMETRY_CPP,
        )
        self.assertIn("eTaskGetState(handle) != eSuspended", TELEMETRY_CPP)

    def test_telemetry_cleanup_and_consent_fail_closed(self) -> None:
        self.assertIn("mqtt_cleanup_decision(", TELEMETRY_POLICY)
        self.assertIn("DESTROY_ALREADY_STOPPED", TELEMETRY_POLICY)
        self.assertIn("disconnect_requested", TELEMETRY_POLICY)
        self.assertIn("xSemaphoreCreateMutexStatic", TELEMETRY_CPP)
        self.assertIn("consent_mutex_", TELEMETRY_HEADER)
        self.assertIn("consent_publish_blocked_", TELEMETRY_HEADER)
        self.assertIn(
            "this->set_consent_publish_blocked_(true)",
            TELEMETRY_CPP,
        )
        self.assertIn(
            "A failed opt-out write must not reopen telemetry",
            TELEMETRY_CPP,
        )
        self.assertIn("eSetValueWithOverwrite", TELEMETRY_CPP)
        self.assertNotIn("eSetValueWithoutOverwrite", TELEMETRY_CPP)

    def test_crash_telemetry_worker_owns_mqtt_lifecycle(self) -> None:
        self.assertIn("StaticTask worker_task_state_", CRASH_TELEMETRY_HEADER)
        self.assertIn(
            "MQTT_WORKER_STACK_IN_PSRAM = true",
            CRASH_TELEMETRY_HEADER,
        )
        self.assertIn(
            "MQTT_WORKER_STACK_IN_PSRAM = false",
            CRASH_TELEMETRY_HEADER,
        )
        self.assertIn("psram.request_external_task_stack()", CRASH_TELEMETRY_CODEGEN)
        self.assertIn(
            "get_esp32_variant() == VARIANT_ESP32S3",
            CRASH_TELEMETRY_CODEGEN,
        )
        self.assertNotIn("xTaskCreatePinnedToCore(", CRASH_TELEMETRY_CPP)
        self.assertNotIn("vTaskDelete(", CRASH_TELEMETRY_CPP)
        self.assertIn(
            "this->worker_task_state_.deallocate();",
            CRASH_TELEMETRY_CPP,
        )
        self.assertIn("eTaskGetState(handle) != eSuspended", CRASH_TELEMETRY_CPP)
        # Every MQTT lifecycle call exists exactly once, inside the worker
        # start/cleanup path. The main loop only notifies the worker.
        for lifecycle_call in (
            "esp_mqtt_client_init(",
            "esp_mqtt_client_start(",
            "esp_mqtt_client_stop(",
            "esp_mqtt_client_destroy(",
        ):
            self.assertEqual(CRASH_TELEMETRY_CPP.count(lifecycle_call), 1)
        self.assertIn("void OpenQuattCrashTelemetry::finalize_session_()", CRASH_TELEMETRY_CPP)
        self.assertIn("this->finalize_session_();", CRASH_TELEMETRY_CPP)
        self.assertIn("request_session_finish_(", CRASH_TELEMETRY_CPP)
        self.assertIn("select_crash_session_action(", CRASH_TELEMETRY_CPP)
        self.assertIn("select_crash_cleanup_decision(", CRASH_TELEMETRY_CPP)

    def test_large_diagnostic_buffers_never_fall_back_to_internal_heap(self) -> None:
        self.assertIn(
            "this->entries_.allocate_external(ENTRY_CAPACITY)",
            LOG_HISTORY_CPP,
        )
        self.assertIn(
            "snapshot.allocate_external(snapshot_capacity)",
            LOG_HISTORY_CPP,
        )
        self.assertIn(
            "this->samples_.allocate_external(BUFFER_BYTES)",
            DEBUG_RECORDER_CPP,
        )
        self.assertIn(
            "this->fields_.allocate_external(FIELD_CAPACITY)",
            DEBUG_RECORDER_CPP,
        )
        self.assertIn(
            "this->pending_fields_.allocate_external(FIELD_CAPACITY)",
            DEBUG_RECORDER_CPP,
        )
        self.assertIn(
            "this->string_entries_.allocate_external(STRING_ENTRY_CAPACITY)",
            DEBUG_RECORDER_CPP,
        )
        self.assertIn(
            "this->string_buckets_.allocate_external(STRING_BUCKET_CAPACITY)",
            DEBUG_RECORDER_CPP,
        )
        self.assertIn(
            "this->string_compaction_order_.allocate_external(STRING_ENTRY_CAPACITY)",
            DEBUG_RECORDER_CPP,
        )
        self.assertIn(
            "this->string_data_.allocate_external(STRING_DATA_BYTES)",
            DEBUG_RECORDER_CPP,
        )
        self.assertIn(
            "this->ram_history_.allocate_external(RAM_CAPACITY)",
            TRENDS_CPP,
        )
        self.assertIn(
            "this->flash_index_.allocate_external(FLASH_SLOT_COUNT)",
            TRENDS_CPP,
        )

    def test_optional_history_allocation_failures_are_explicit(self) -> None:
        self.assertIn("bool storage_available() const", LOG_HISTORY_HEADER)
        self.assertIn('"503 Service Unavailable"', LOG_HISTORY_CPP)
        self.assertIn('"psram_unavailable"', LOG_HISTORY_CPP)

        load_archive = TRENDS_CPP[
            TRENDS_CPP.index("void OpenQuattTrends::load_archive_if_needed_()"):
            TRENDS_CPP.index("void OpenQuattTrends::push_ram_sample_")
        ]
        merge_archive = TRENDS_CPP[
            TRENDS_CPP.index("bool OpenQuattTrends::merge_flash_history_into_ram_()"):
            TRENDS_CPP.index("bool OpenQuattTrends::clear_flash_archive_()")
        ]
        self.assertIn("!this->flash_archive_available_()", load_archive)
        self.assertIn("!this->flash_archive_available_()", merge_archive)
        self.assertLess(
            merge_archive.index("!this->flash_archive_available_()"),
            merge_archive.index("this->ram_head_ = 0"),
        )

    def test_hp_water_calibration_samples_are_task_scoped_psram(self) -> None:
        self.assertIn(
            "PsramBuffer<SampleSet> samples_",
            HP_WATER_CALIBRATION_HEADER,
        )
        self.assertIn(
            "samples_.allocate_external(MAX_WINDOW_SAMPLES)",
            HP_WATER_CALIBRATION_HEADER,
        )
        self.assertIn(
            'publish("REFUSED: calibration memory unavailable")',
            HP_WATER_CALIBRATION_HEADER,
        )
        self.assertIn("release_samples();", HP_WATER_CALIBRATION_HEADER)
        self.assertNotIn(
            "SampleSet samples_[MAX_WINDOW_SAMPLES]",
            HP_WATER_CALIBRATION_HEADER,
        )


if __name__ == "__main__":
    unittest.main()
