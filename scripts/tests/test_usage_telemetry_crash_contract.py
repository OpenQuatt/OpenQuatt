#!/usr/bin/env python3

import json
import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = (
    REPO_ROOT
    / "components"
    / "openquatt_usage_telemetry"
    / "OpenQuattUsageTelemetry.cpp"
).read_text()
HEADER = (
    REPO_ROOT
    / "components"
    / "openquatt_usage_telemetry"
    / "OpenQuattUsageTelemetry.h"
).read_text()
POLICY = (
    REPO_ROOT
    / "components"
    / "openquatt_usage_telemetry"
    / "OpenQuattUsageTelemetryPolicy.h"
).read_text()
YAML = (REPO_ROOT / "openquatt" / "oq_usage_telemetry.yaml").read_text()


def ordered(source: str, *tokens: str) -> bool:
    position = 0
    for token in tokens:
        position = source.find(token, position)
        if position < 0:
            return False
        position += len(token)
    return True


class UsageTelemetryCrashContractTest(unittest.TestCase):
    def test_mqtt_policy_is_kind_specific(self):
        self.assertIn(
            "case PublishKind::TELEMETRY:\n      return {1, 0, false};", POLICY
        )
        self.assertIn(
            "case PublishKind::CRASH:\n      return {1, 1, false};", POLICY
        )
        self.assertIn(
            "case PublishKind::TOMBSTONE:\n      return {1, 1, true};", POLICY
        )
        self.assertIn("const MqttPublishPolicy policy = mqtt_publish_policy(kind);", COMPONENT)

    def test_opt_out_has_two_reboot_recovery_records(self):
        opt_out = COMPONENT[
            COMPONENT.index("storage.enabled = 0U;") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::load_storage_"
            )
        ]
        self.assertTrue(
            ordered(
                opt_out,
                "storage.reserved[CLEANUP_REQUIRED_RESERVED_INDEX] = 1U;",
                "persist_cleanup_wal_()",
                "save_storage_(storage)",
            )
        )
        self.assertIn(
            "storage.reserved[CLEANUP_REQUIRED_RESERVED_INDEX] != 0U", COMPONENT
        )
        self.assertIn("retained_cleanup_recoverable_after_reboot", COMPONENT)
        self.assertIn("retained_cleanup_ready_for_tombstone", COMPONENT)

        storage_save = COMPONENT[
            COMPONENT.index("bool OpenQuattUsageTelemetry::save_storage_") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::load_cleanup_storage_"
            )
        ]
        cleanup_save = COMPONENT[
            COMPONENT.index("bool OpenQuattUsageTelemetry::save_cleanup_storage_") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::persist_cleanup_wal_"
            )
        ]
        self.assertIn("this->pref_.load(&verify)", storage_save)
        self.assertIn("std::memcmp(&storage, &verify, sizeof(storage)) == 0", storage_save)
        self.assertIn("this->cleanup_pref_.load(&verify)", cleanup_save)
        self.assertIn("std::memcmp(&storage, &verify, sizeof(storage)) == 0", cleanup_save)

        setup_recovery = COMPONENT[
            COMPONENT.index("CleanupStorage cleanup{};") : COMPONENT.index(
                "if (!this->apply_storage_(storage))"
            )
        ]
        self.assertIn(
            "storage.reserved[CLEANUP_REQUIRED_RESERVED_INDEX] != 0U",
            setup_recovery,
        )
        self.assertNotIn("storage.enabled == 0U", setup_recovery)

    def test_cleanup_cannot_finish_before_snapshot_discard_and_durable_opt_out(self):
        cleanup = COMPONENT[
            COMPONENT.index("bool OpenQuattUsageTelemetry::complete_cleanup_wal_") : COMPONENT.index(
                "void OpenQuattUsageTelemetry::apply_pending_cleanup_"
            )
        ]
        self.assertTrue(
            ordered(
                cleanup,
                "discard_pending_crash_()",
                "save_cleanup_storage_(cleared)",
                "storage.reserved[CLEANUP_REQUIRED_RESERVED_INDEX] = 0U;",
                "save_storage_(storage)",
                "cleanup_pending_.store(false)",
            )
        )
        self.assertIn("data_publish_allowed", COMPONENT)
        self.assertIn("!enabled || this->cleanup_pending_.load()", COMPONENT)
        write_state = COMPONENT[
            COMPONENT.index("void OpenQuattUsageTelemetry::write_state") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::load_storage_"
            )
        ]
        self.assertNotIn("discard_pending_crash_()", write_state[write_state.index("storage.enabled = 0U;") :])

    def test_retained_crash_evidence_survives_downgrade_opt_out(self):
        setup_recovery = COMPONENT[
            COMPONENT.index("CleanupStorage cleanup{};") : COMPONENT.index(
                "if (!this->apply_storage_(storage))"
            )
        ]
        self.assertIn("retained_crash_cleanup_required_on_disabled_boot", setup_recovery)
        self.assertIn("CRASH_PUBLISH_MAY_HAVE_REACHED_RESERVED_INDEX", setup_recovery)
        completion = COMPONENT[
            COMPONENT.index("void OpenQuattUsageTelemetry::complete_publish_session_") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::build_publish_topic_"
            )
        ]
        self.assertTrue(
            ordered(
                completion,
                "this->crash_provider_->acknowledge_pending_crash",
            )
        )
        cleanup = COMPONENT[
            COMPONENT.index("bool OpenQuattUsageTelemetry::complete_cleanup_wal_") : COMPONENT.index(
                "void OpenQuattUsageTelemetry::apply_pending_cleanup_"
            )
        ]
        self.assertIn(
            "storage.reserved[CRASH_PUBLISH_MAY_HAVE_REACHED_RESERVED_INDEX] = 0U;",
            cleanup,
        )
        session_start = COMPONENT[
            COMPONENT.index("void OpenQuattUsageTelemetry::start_publish_session_") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::ensure_worker_task_"
            )
        ]
        self.assertTrue(
            ordered(
                session_start,
                "payload_built = this->build_crash_payload_();",
                "this->mark_crash_publish_may_have_reached_broker_()",
                "this->ensure_worker_task_()",
            )
        )
        marker = COMPONENT[
            COMPONENT.index("bool OpenQuattUsageTelemetry::mark_crash_publish_may_have_reached_broker_") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::complete_cleanup_wal_"
            )
        ]
        self.assertIn("CRASH_ENDPOINT_GENERATION_RESERVED_INDEX", marker)
        self.assertIn("CRASH_ENDPOINT_GENERATION", marker)

    def test_cleanup_persistence_retries_before_network_gate(self):
        loop = COMPONENT[
            COMPONENT.index("void OpenQuattUsageTelemetry::loop()") : COMPONENT.index(
                "void OpenQuattUsageTelemetry::dump_config()"
            )
        ]
        self.assertTrue(
            ordered(
                loop,
                "this->retry_cleanup_persistence_();",
                "!this->is_configured() || !network::is_connected()",
            )
        )
        self.assertIn("CLEANUP_PERSIST_RETRY_MIN_MS", HEADER)
        self.assertIn("CLEANUP_PERSIST_RETRY_MAX_MS", HEADER)

    def test_discard_failure_keeps_cleanup_gate_closed_across_reenable(self):
        write_state = COMPONENT[
            COMPONENT.index("void OpenQuattUsageTelemetry::write_state") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::load_storage_"
            )
        ]
        self.assertTrue(
            ordered(
                write_state,
                "state && this->cleanup_pending_.load()",
                "Usage statistics cannot be enabled until retained crash cleanup completes",
                "this->set_consent_publish_blocked_(true)",
            )
        )
        self.assertIn("discard_pending_crash_()", write_state)

    def test_mqtt_threads_do_not_read_mutable_setup_sensor_state(self):
        session_gate = COMPONENT[
            COMPONENT.index("bool OpenQuattUsageTelemetry::session_publish_allowed_") : COMPONENT.index(
                "void OpenQuattUsageTelemetry::clear_session_buffers_"
            )
        ]
        self.assertNotIn("is_setup_complete_()", session_gate)
        self.assertIn("this->setup_complete_gate_.load()", session_gate)
        self.assertIn("data_publish_allowed", session_gate)
        setup = COMPONENT[
            COMPONENT.index("void OpenQuattUsageTelemetry::setup()") : COMPONENT.index(
                "void OpenQuattUsageTelemetry::loop()"
            )
        ]
        self.assertIn("add_on_state_callback", setup)
        self.assertIn("this->set_setup_complete_gate_(state)", setup)
        setup_gate = COMPONENT[
            COMPONENT.index("bool OpenQuattUsageTelemetry::set_setup_complete_gate_") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::ensure_installation_id_"
            )
        ]
        self.assertTrue(
            ordered(
                setup_gate,
                "xSemaphoreTake(this->consent_mutex_",
                "this->setup_complete_gate_.store(setup_complete)",
                "xSemaphoreGive(this->consent_mutex_)",
            )
        )

    def test_crash_schema_contains_reconstructable_build_identity(self):
        for field in (
            "current_build_id",
            "captured_build_id",
            "captured_source_repository",
            "captured_source_commit",
            "captured_build_epoch",
            "captured_build_target",
            "captured_firmware_version",
            "captured_release_channel",
            "captured_by_current_build",
            "raw_cause",
            "fault_addr",
            "other_core_backtrace",
            "backtrace_truncated",
        ):
            self.assertIn(f'"{field}"', COMPONENT)
        self.assertIn("esp_app_get_elf_sha256", COMPONENT)
        self.assertIn("CRASH_PAYLOAD_CAPACITY = 2048U", COMPONENT)

    def test_unserializable_crash_does_not_starve_normal_telemetry(self):
        session_start = COMPONENT[
            COMPONENT.index("void OpenQuattUsageTelemetry::start_publish_session_") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::ensure_worker_task_"
            )
        ]
        self.assertTrue(
            ordered(
                session_start,
                "payload_built = this->build_crash_payload_();",
                "this->regular_telemetry_due_()",
                "kind = PublishKind::TELEMETRY;",
                "payload_built = this->build_telemetry_payload_();",
                "this->active_publish_kind_.store(kind);",
            )
        )
        completion = COMPONENT[
            COMPONENT.index("void OpenQuattUsageTelemetry::complete_publish_session_") : COMPONENT.index(
                "bool OpenQuattUsageTelemetry::build_publish_topic_"
            )
        ]
        self.assertIn("this->crash_provider_->has_pending_crash()", completion)
        self.assertIn("this->schedule_retry_();", completion)
        self.assertIn("this->next_regular_telemetry_ms_ = millis() + this->interval_ms_", completion)

    def test_maximum_supported_identity_lengths_stay_below_mqtt_payload_limit(self):
        elf_sha = "f" * 64
        commit = "a" * 40
        repository = "R" * 96
        target = "T" * 96
        address = "0xffffffff"
        payload = {
            "schema_version": 1,
            "message_id": "0" * 36,
            "installation_id": "0" * 36,
            "event": "crash",
            "timestamp_s": 18446744073709551615,
            "uptime_s": 4294967295,
            "firmware_version": "v" * 32,
            "release_channel": "r" * 16,
            "current_build_id": elf_sha,
            "hardware_profile": "h" * 32,
            "topology": "t" * 16,
            "connection": "c" * 16,
            "reset_reason": "interrupt_watchdog",
            "crash": {
                "captured_build_id": elf_sha,
                "captured_source_repository": repository,
                "captured_source_commit": commit,
                "captured_build_epoch": 18446744073709551615,
                "captured_build_target": target,
                "captured_firmware_version": "v" * 32,
                "captured_release_channel": "r" * 16,
                "captured_by_current_build": True,
                "exception_type": "e" * 24,
                "reason": "r" * 63,
                "raw_cause": 4294967295,
                "core": 255,
                "pc": address,
                "fault_addr": address,
                "backtrace": [address] * 16,
                "other_core_backtrace": [address] * 16,
                "backtrace_truncated": False,
            },
        }
        encoded = json.dumps(payload, separators=(",", ":"))
        self.assertLessEqual(len(encoded), 2048)

    def test_yaml_wires_crash_provider(self):
        self.assertIn("crash_provider: oq_log_history", YAML)
        self.assertIn("CleanupStorage", HEADER)
        self.assertIn("cleanup_main_durable_", HEADER)


if __name__ == "__main__":
    unittest.main()
