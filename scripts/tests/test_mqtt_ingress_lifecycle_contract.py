from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = (
    ROOT
    / "components"
    / "openquatt_mqtt_config"
    / "OpenQuattMqttConfig.h"
).read_text()
CPP = (
    ROOT
    / "components"
    / "openquatt_mqtt_config"
    / "OpenQuattMqttConfig.cpp"
).read_text()
CODEGEN = (
    ROOT / "components" / "openquatt_mqtt_config" / "__init__.py"
).read_text()


class MqttIngressLifecycleContractTest(unittest.TestCase):
    def test_worker_placement_and_lifetime_are_explicit(self) -> None:
        self.assertIn("StaticTask client_worker_task_state_", HEADER)
        self.assertIn("MQTT_WORKER_STACK_IN_PSRAM = true", HEADER)
        self.assertIn("MQTT_WORKER_STACK_IN_PSRAM = false", HEADER)
        self.assertIn("MQTT_WORKER_TASK_STACK_SIZE = 24576", HEADER)
        self.assertIn("psram.request_external_task_stack()", CODEGEN)
        self.assertIn("get_esp32_variant() == VARIANT_ESP32S3", CODEGEN)
        self.assertNotIn("xTaskCreatePinnedToCore(", CPP)
        self.assertNotIn("vTaskDelete(nullptr)", CPP)
        wrong_region = CPP.index("stack_is_external != MQTT_WORKER_STACK_IN_PSRAM")
        suspend = CPP.index("vTaskSuspend(handle)", wrong_region)
        release = CPP.index("client_worker_task_state_.deallocate()", wrong_region)
        cleanup = CPP.index("this->start_permanent_failure_cleanup_();", wrong_region)
        self.assertLess(suspend, release)
        self.assertLess(release, cleanup)
        failure_start = CPP[
            CPP.index("void OpenQuattMqttConfig::start_permanent_failure_cleanup_()") :
            CPP.index("bool OpenQuattMqttConfig::process_permanent_failure_cleanup_()")
        ]
        self.assertLess(
            failure_start.index("this->close_client_event_gate_();"),
            failure_start.index("this->process_permanent_failure_cleanup_();"),
        )
        self.assertIn("this->client_requested_generation_.fetch_add(1U) + 1U", failure_start)
        self.assertIn("this->client_applied_generation_.store(failed_generation);", failure_start)
        failure_cleanup = CPP[
            CPP.index("bool OpenQuattMqttConfig::process_permanent_failure_cleanup_()") :
            CPP.index("bool OpenQuattMqttConfig::request_client_reconcile_()")
        ]
        self.assertIn("this->stop_client_()", failure_cleanup)
        self.assertLess(
            failure_cleanup.index("this->stop_client_()"),
            failure_cleanup.index("this->mark_failed();"),
        )
        self.assertLess(
            failure_cleanup.index("this->client_worker_active_.load()"),
            failure_cleanup.index("this->stop_client_()"),
        )
        self.assertIn("this->maybe_release_classic_worker_();", failure_cleanup)
        self.assertIn("this->permanent_failure_cleanup_pending_.load()", CPP)

    def test_worker_config_copies_do_not_allocate_internal_heap(self) -> None:
        config_start = HEADER.index("struct ClientConfig")
        config_end = HEADER.index(
            "ClientConfig get_desired_client_config_()", config_start
        )
        config = HEADER[config_start:config_end]
        self.assertNotIn("std::string", config)
        self.assertIn("std::array<char, BROKER_MAX_LEN + 1>", config)
        self.assertIn("std::array<char, PASSWORD_MAX_LEN + 1>", config)
        self.assertIn("std::array<char, MQTT_CLIENT_ID_MAX_LEN>", CPP)

    def test_worker_is_marked_active_before_it_can_run(self) -> None:
        request = CPP.index("bool OpenQuattMqttConfig::request_client_reconcile_()")
        active = CPP.index("this->client_worker_active_.store(true);", request)
        notify = CPP.index("xTaskNotifyGive(handle)", request)
        resume = CPP.index("vTaskResume(handle)", request)
        self.assertLess(active, notify)
        self.assertLess(notify, resume)

    def test_classic_worker_waits_for_notification_and_is_released_from_loop(
        self,
    ) -> None:
        self.assertIn("#if !defined(CONFIG_IDF_TARGET_ESP32S3)", CPP)
        self.assertIn("state == eBlocked || state == eSuspended", CPP)
        self.assertIn("classic_worker_action(", CPP)
        worker_start = CPP.index(
            "void OpenQuattMqttConfig::client_worker_task_("
        )
        worker_end = CPP.index(
            "void OpenQuattMqttConfig::set_numeric_input_topic_",
            worker_start,
        )
        worker = CPP[worker_start:worker_end]
        idle_transition = worker.index(
            "self->client_worker_active_.store(false);"
        )
        self.assertNotIn(
            "vTaskSuspend(nullptr)", worker[idle_transition:]
        )
        self.assertIn("client_worker_task_state_.deallocate()", CPP)

    def test_config_changes_close_callbacks_before_reconcile(self) -> None:
        gate = CPP.index("this->close_client_event_gate_();")
        generation = CPP.index("this->mqtt_session_generation_.fetch_add(1U);")
        request = CPP.index("this->client_requested_generation_.fetch_add(1U)")
        self.assertLess(gate, generation)
        self.assertLess(generation, request)
        self.assertIn("mqtt_event_is_current(", CPP)
        self.assertIn("event_generation", CPP)

    def test_stop_failure_never_falls_through_to_destroy(self) -> None:
        self.assertIn("mqtt_stop_decision(", CPP)
        self.assertIn("MqttStopDecision::FORCE_DISCONNECT", CPP)
        self.assertIn("esp_mqtt_client_disconnect(client)", CPP)
        self.assertIn("MqttStopDecision::RETRY_STOP", CPP)
        self.assertIn("esp_mqtt_client_destroy(client)", CPP)

    def test_start_failure_keeps_ownership_when_destroy_fails(self) -> None:
        start = CPP.index("bool OpenQuattMqttConfig::start_client_(")
        destroy = CPP.index(
            "const esp_err_t destroy_error = esp_mqtt_client_destroy(client);",
            start,
        )
        cleanup_failure = CPP.index("if (destroy_error != ESP_OK)", destroy)
        clear_handle = CPP.index("this->mqtt_client_ = nullptr;", cleanup_failure)
        return_failure = CPP.index("return false;", cleanup_failure)
        self.assertLess(return_failure, clear_handle)

    def test_only_loop_transaction_code_writes_preferences(self) -> None:
        process_start = CPP.index(
            "void OpenQuattMqttConfig::process_storage_transaction_()"
        )
        process_end = CPP.index(
            "OpenQuattMqttConfig::begin_storage_mutation_", process_start
        )
        process = CPP[process_start:process_end]
        self.assertEqual(CPP.count("this->pref_.save("), 2)
        self.assertEqual(CPP.count("global_preferences->sync()"), 2)
        for token in ("this->pref_.save(", "global_preferences->sync()"):
            positions = [
                index
                for index in range(len(CPP))
                if CPP.startswith(token, index)
            ]
            self.assertTrue(positions)
            self.assertTrue(
                all(process_start <= index < process_end for index in positions)
            )
        self.assertIn("this->pref_.save(&storage)", process)
        self.assertIn("global_preferences->sync()", process)
        self.assertLess(
            process.index("this->pref_.save(&storage)"),
            process.index("global_preferences->sync()"),
        )

        setters_start = CPP.index(
            "OpenQuattMqttConfig::set_runtime_config("
        )
        setters_end = CPP.index(
            "bool OpenQuattMqttConfig::load_storage_", setters_start
        )
        setters = CPP[setters_start:setters_end]
        self.assertNotIn("pref_.", setters)
        self.assertNotIn("global_preferences", setters)
        self.assertNotIn("load_storage_", setters)
        self.assertNotIn("apply_storage_", setters)

    def test_full_payload_is_retained_for_save_and_sync_retries(self) -> None:
        self.assertIn("this->desired_storage_ = storage;", CPP)
        self.assertIn("storage = this->desired_storage_;", CPP)
        self.assertIn(
            "this->storage_persistence_pending_ = true;", CPP
        )
        self.assertIn("STORAGE_MAX_ATTEMPTS = 2U", HEADER)
        self.assertIn("storage_should_retry(", CPP)
        self.assertNotIn("storage_matches_persisted_", CPP)
        self.assertIn("this->restore_committed_storage_(committed_storage)", CPP)
        self.assertNotIn("STORAGE_RETRY_MS", HEADER)
        self.assertNotIn("next_storage_attempt_ms_", HEADER)
        self.assertNotIn("preference_sync_pending_", HEADER)
        self.assertNotIn("save_storage_", HEADER)
        self.assertNotIn("save_storage_", CPP)

    def test_only_successful_sync_proves_commit_or_rollback(self) -> None:
        process_start = CPP.index(
            "void OpenQuattMqttConfig::process_storage_transaction_()"
        )
        restore_start = CPP.index(
            "bool OpenQuattMqttConfig::restore_committed_storage_(",
            process_start,
        )
        process = CPP[process_start:restore_start]
        restore_end = CPP.index(
            "bool OpenQuattMqttConfig::preflight_storage_apply_", restore_start
        )
        restore = CPP[restore_start:restore_end]
        self.assertEqual(process.count("durable = true;"), 1)
        self.assertIn("if (global_preferences->sync())", process)
        self.assertIn("if (synced)", restore)
        self.assertNotIn("storage_matches_persisted_", process + restore)
        self.assertGreaterEqual(
            process.count("this->start_permanent_failure_cleanup_();"), 3
        )

    def test_bootstrap_and_http_share_the_main_loop_persistence_path(self) -> None:
        setup_start = CPP.index("void OpenQuattMqttConfig::setup()")
        setup_end = CPP.index(
            "OpenQuattMqttConfig::StatusSnapshot", setup_start
        )
        setup = CPP[setup_start:setup_end]
        self.assertIn(
            "storage, committed_storage, !loaded);",
            setup,
        )
        self.assertIn(
            'this->build_storage_("", 1883U, "", "", false',
            setup,
        )
        self.assertIn("if (loaded &&", setup)
        self.assertIn(
            'apply_as_bootstrap ? "bootstrap" : "runtime"',
            CPP,
        )
        self.assertNotIn("pref_.save", setup)
        self.assertNotIn("global_preferences->sync", setup)

    def test_http_success_requires_exact_transaction_success(self) -> None:
        self.assertEqual(
            CPP.count("if (send_mutation_error_(request, result))"), 3
        )
        self.assertIn("StorageTransactionPhase::CANCELLED", CPP)
        self.assertIn("StorageTransactionPhase::WRITING", CPP)
        self.assertIn("StorageTransactionPhase::COMMITTING", CPP)
        self.assertIn("storage_generation_may_commit(", CPP)
        self.assertIn("STORAGE_MUTATION_WAIT_MS = 2000U", HEADER)
        self.assertIn("STORAGE_CANCELLATION_GRACE_MS = 750U", HEADER)
        self.assertIn("MutationResult::APPLY_FAILED", CPP)
        self.assertIn("MutationResult::SAVE_FAILED", CPP)
        self.assertIn("MutationResult::SYNC_FAILED", CPP)
        self.assertIn("MutationResult::TIMEOUT", CPP)
        self.assertIn("MutationResult::RUNTIME_PENDING", CPP)
        self.assertIn("MutationResult::RECOVERY_PENDING", CPP)
        self.assertIn("MutationResult::PERSISTENCE_PENDING", CPP)
        self.assertIn('"preference_commit_pending"', CPP)
        self.assertIn(
            'request->send(503, "application/json", R"({"ok":false,"error":"preference_commit_pending"})")',
            CPP,
        )
        self.assertIn('"pending":true', CPP)
        self.assertIn('"runtime_pending":%s', CPP)

    def test_failed_component_rejects_new_mutations_immediately(self) -> None:
        begin_start = CPP.index(
            "OpenQuattMqttConfig::begin_storage_mutation_"
        )
        begin_end = CPP.index(
            "OpenQuattMqttConfig::submit_storage_mutation_", begin_start
        )
        begin = CPP[begin_start:begin_end]
        self.assertIn("this->is_failed()", begin)
        self.assertIn("return MutationResult::UNAVAILABLE;", begin)

    def test_input_policy_is_applied_only_with_the_durable_snapshot(self) -> None:
        apply_start = CPP.index(
            "OpenQuattMqttConfig::apply_storage_("
        )
        apply_end = CPP.index(
            "bool OpenQuattMqttConfig::build_storage_", apply_start
        )
        apply = CPP[apply_start:apply_end]
        self.assertIn("newly_enabled_inputs", apply)
        self.assertIn("newly_accepted_retained_inputs", apply)
        self.assertIn("newly_rejected_retained_inputs", apply)
        self.assertIn("this->resubscribe_inputs_.store(true);", apply)
        self.assertIn("this->clear_input_mask_pending_.fetch_or(", apply)

    def test_disabled_input_is_rechecked_after_callback_queueing(self) -> None:
        numeric_start = CPP.index(
            "void OpenQuattMqttConfig::handle_numeric_payload_("
        )
        binary_start = CPP.index(
            "void OpenQuattMqttConfig::handle_binary_payload_("
        )
        numeric = CPP[numeric_start:binary_start]
        binary = CPP[
            binary_start : CPP.index(
                "void OpenQuattMqttConfig::invalidate_numeric_input_",
                binary_start,
            )
        ]
        self.assertIn("!this->is_numeric_input_enabled_(input_index)", numeric)
        self.assertIn("!this->is_binary_input_enabled_(input_index)", binary)

    def test_storage_bit_positions_are_frozen_for_upgrade(self) -> None:
        # Stored enable/retained masks must keep their meaning when inputs are
        # added: bits 0..3 are the original numeric inputs, bits 4..5 the
        # binary enables, and the heating supply target owns bit 6 (issue #649).
        # Shifting positions instead would re-enable a disabled heating topic
        # or reject the stored config outright, without any migration.
        self.assertIn("HEATING_SUPPLY_TARGET_BIT = 6U", HEADER)
        self.assertIn("BINARY_INPUT_BIT_BASE = 4U", HEADER)
        self.assertIn("INPUT_MASK_ALL = 0x7FU", HEADER)
        for token in (
            "1U << (NUMERIC_INPUT_COUNT",
            "1U << input_index",
            "1U << i)",
        ):
            self.assertNotIn(token, CPP)
        self.assertIn("numeric_input_bit_(input_index)", CPP)
        self.assertIn("binary_input_bit_(input_index)", CPP)
        self.assertIn("numeric_input_bit_(i)", CPP)
        self.assertIn("binary_input_bit_(i)", CPP)

    def test_idempotent_retry_reconciles_an_unresolved_client(self) -> None:
        apply_start = CPP.index(
            "OpenQuattMqttConfig::apply_storage_("
        )
        apply_end = CPP.index(
            "bool OpenQuattMqttConfig::build_storage_", apply_start
        )
        apply = CPP[apply_start:apply_end]
        self.assertIn("const bool lifecycle_unresolved", apply)
        self.assertIn("this->client_reconcile_pending_.load()", apply)
        self.assertIn(
            "this->client_applied_generation_.load() != requested_generation",
            apply,
        )
        self.assertIn("!this->active_client_matches_(requested_generation)", apply)
        self.assertIn("StorageApplyResult::RUNTIME_PENDING", apply)


if __name__ == "__main__":
    unittest.main()
