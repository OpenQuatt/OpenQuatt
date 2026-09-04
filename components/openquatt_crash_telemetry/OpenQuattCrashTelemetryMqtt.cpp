#include "OpenQuattCrashTelemetry.h"
#include "OpenQuattCrashTelemetryHelpers.h"

#include "esp_crt_bundle.h"
#include "esp_memory_utils.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <freertos/task.h>

namespace esphome::openquatt_crash_telemetry {
namespace {

static const char* const TAG = "openquatt.crash_telemetry";

using detail::append_json_key;
using detail::FixedWriter;
using detail::reset_reason_name;
using detail::valid_installation_id;

}  // namespace

bool OpenQuattCrashTelemetry::build_topic_() {
  if (!this->state_ || !valid_installation_id(this->state_.data()->installation_id)) return false;
  const size_t required = this->topic_.size() + 1U + 36U + sizeof("/crash");
  if (!this->topic_buffer_.allocate_external(required)) return false;
  FixedWriter writer(this->topic_buffer_.data(), this->topic_buffer_.size());
  writer.append(this->topic_.c_str());
  writer.append("/");
  writer.append(this->state_.data()->installation_id);
  writer.append("/crash");
  return writer.ok();
}

bool OpenQuattCrashTelemetry::build_crash_payload_() {
  if (!this->record_ || !this->state_ || this->record_.data()->pending == 0U ||
      !valid_installation_id(this->state_.data()->installation_id)) {
    return false;
  }
  if (!this->payload_buffer_.allocate_external(CRASH_PAYLOAD_CAPACITY + 1U)) return false;

  const CrashRecord& record = *this->record_.data();
  uint32_t reported_at = 0U;
  if (this->clock_ != nullptr) {
    const auto now = this->clock_->now();
    const int64_t timestamp = static_cast<int64_t>(now.timestamp);
    const bool timestamp_is_sane = now.is_valid() &&
                                   timestamp >= static_cast<int64_t>(openquatt_log_history::MIN_VALID_CRASH_EPOCH_S) &&
                                   timestamp < static_cast<int64_t>(openquatt_log_history::MAX_VALID_CRASH_EPOCH_S);
    const uint32_t candidate = timestamp_is_sane ? static_cast<uint32_t>(timestamp) : 0U;
    if (reported_at_is_usable(this->time_synchronized_.load(), timestamp_is_sane, record.crash_time_valid != 0U,
                              candidate, record.crash_timestamp)) {
      reported_at = candidate;
    } else if (this->time_synchronized_.load() && timestamp_is_sane && record.crash_time_valid != 0U &&
               candidate < record.crash_timestamp) {
      ESP_LOGW(TAG, "Synchronized reporting time predates crash breadcrumb; omitting reported_at");
    }
  }
  FixedWriter writer(this->payload_buffer_.data(), this->payload_buffer_.size());
  writer.append("{\"schema_version\":1,\"message_id\":");
  writer.append_json_string(record.crash_id);
  append_json_key(writer, "installation_id");
  writer.append_json_string(this->state_.data()->installation_id);
  append_json_key(writer, "event");
  writer.append_json_string("crash");
  append_json_key(writer, "crash_timestamp");
  if (record.crash_time_valid != 0U) {
    writer.append_uint(record.crash_timestamp);
  } else {
    writer.append("null");
  }
  append_json_key(writer, "crash_uptime_s");
  if (record.crash_time_valid != 0U) {
    writer.append_uint(record.crash_uptime_s);
  } else {
    writer.append("null");
  }
  append_json_key(writer, "reported_at");
  if (reported_at != 0U) {
    writer.append_uint(reported_at);
  } else {
    writer.append("null");
  }
  append_json_key(writer, "reset_reason");
  writer.append_json_string(reset_reason_name(static_cast<esp_reset_reason_t>(record.reset_reason)));
  append_json_key(writer, "reporting_build_id");
  writer.append_json_string(record.build_id);
  append_json_key(writer, "source_repository");
  writer.append_json_string(record.source_repository);
  append_json_key(writer, "source_commit");
  writer.append_json_string(record.source_commit);
  append_json_key(writer, "build_target");
  writer.append_json_string(record.build_target);
  append_json_key(writer, "release_manifest_url");
  writer.append_json_string(record.release_manifest_url);
  append_json_key(writer, "reporting_build_epoch");
  writer.append_uint(record.build_epoch);
  append_json_key(writer, "esphome_config_hash");
  writer.append_uint(record.config_hash);
  append_json_key(writer, "firmware_version");
  writer.append_json_string(record.firmware_version);
  append_json_key(writer, "release_channel");
  writer.append_json_string(record.release_channel);
  append_json_key(writer, "esphome_version");
  writer.append_json_string(record.esphome_version);
  append_json_key(writer, "hardware_profile");
  writer.append_json_string(record.hardware_profile);
  append_json_key(writer, "topology");
  writer.append_json_string(record.topology);
  append_json_key(writer, "connection");
  writer.append_json_string(record.connection);
  append_json_key(writer, "captured_by_reporting_build");
  writer.append(record.captured_by_reporting_build != 0U ? "true" : "false");
  append_json_key(writer, "report_truncated");
  writer.append(record.truncated != 0U ? "true" : "false");
  append_json_key(writer, "report");
  writer.append_json_string(record.report, record.report_length);
  writer.append("}");

  if (!writer.ok()) {
    this->payload_buffer_.release();
    return false;
  }
  this->payload_size_ = writer.size();
  return true;
}

void OpenQuattCrashTelemetry::start_publish_session_(CrashPublishKind kind) {
  // Main-loop only: bounded preparation, then hand off to the worker. The
  // loopTask must never run esp_mqtt_client_init/start/stop/destroy itself.
  if (kind == CrashPublishKind::NONE || this->session_active_.load() || this->finishing_session_.load()) {
    return;
  }
  if (!this->build_topic_()) {
    this->schedule_retry_();
    return;
  }
  if (kind == CrashPublishKind::CRASH && !this->build_crash_payload_()) {
    this->topic_buffer_.release();
    this->schedule_retry_();
    return;
  }
  if (kind == CrashPublishKind::TOMBSTONE) {
    this->payload_buffer_.release();
    this->payload_size_ = 0U;
  }
  if (!this->ensure_worker_task_()) {
    this->topic_buffer_.release();
    this->payload_buffer_.release();
    this->payload_size_ = 0U;
    this->schedule_retry_();
    return;
  }

  this->session_succeeded_.store(false);
  this->session_failed_.store(false);
  this->pending_message_id_.store(-1);
  this->active_kind_.store(kind);
  this->start_task_running_.store(true);
  this->start_task_complete_.store(false);
  this->finishing_session_.store(false);
  this->cleanup_task_complete_.store(false);
  this->mqtt_connected_seen_.store(false);
  this->mqtt_disconnected_seen_.store(false);
  this->publication_result_succeeded_ = false;
  this->cleanup_disconnect_requested_ = false;
  this->cleanup_stop_failures_ = 0U;
  this->session_started_ms_ = millis();
  this->worker_operation_started_ms_ = millis();
  this->worker_stall_logged_ = false;
  this->session_active_.store(true);

  if (!this->notify_worker_(WorkerCommand::START)) {
    // No client was started: reset state directly, never touch MQTT handles
    // from the main loop.
    this->session_active_.store(false);
    this->start_task_running_.store(false);
    this->active_kind_.store(CrashPublishKind::NONE);
    this->topic_buffer_.release();
    this->payload_buffer_.release();
    this->payload_size_ = 0U;
    this->schedule_retry_();
    ESP_LOGE(TAG, "Failed to notify crash telemetry MQTT worker");
  }
}

bool OpenQuattCrashTelemetry::ensure_worker_task_() {
  if (this->worker_task_state_.is_created()) {
    return this->worker_task_region_valid_;
  }
  this->worker_task_region_valid_ = false;
  if (!this->worker_task_state_.create(&OpenQuattCrashTelemetry::worker_task_, "oq_crash_mqtt",
                                       MQTT_WORKER_TASK_STACK_SIZE, this, 4, MQTT_WORKER_STACK_IN_PSRAM)) {
    ESP_LOGE(TAG, "Failed to create %u-byte crash telemetry worker in %s",
             static_cast<unsigned>(MQTT_WORKER_TASK_STACK_SIZE), MQTT_WORKER_STACK_IN_PSRAM ? "PSRAM" : "internal RAM");
    return false;
  }

  const bool stack_is_external = esp_ptr_external_ram(pxTaskGetStackStart(this->worker_task_state_.get_handle()));
  if (stack_is_external != MQTT_WORKER_STACK_IN_PSRAM) {
    ESP_LOGE(TAG, "Crash telemetry worker stack was allocated in the wrong memory region; worker remains parked");
    // StaticTask owns a static stack. It must not free that stack while the
    // freshly created task may still be entering its notification wait on the
    // other core. Treat this impossible allocator-contract violation as a
    // permanent telemetry failure and leave the parked task intact.
    this->mark_failed();
    return false;
  }
  this->worker_task_region_valid_ = true;
  ESP_LOGD(TAG, "Crash telemetry worker stack: %u bytes in %s", static_cast<unsigned>(MQTT_WORKER_TASK_STACK_SIZE),
           stack_is_external ? "PSRAM" : "internal RAM");
  return true;
}

bool OpenQuattCrashTelemetry::notify_worker_(WorkerCommand command) {
  const TaskHandle_t handle = this->worker_task_state_.get_handle();
  if (handle == nullptr) return false;
  return xTaskNotify(handle, static_cast<uint32_t>(command), eSetValueWithOverwrite) == pdPASS;
}

bool OpenQuattCrashTelemetry::start_client_() {
  // Worker only. Payload and topic were built by the main loop beforehand.
  if (!this->session_active_.load() || this->active_kind_.load() == CrashPublishKind::NONE ||
      this->mqtt_client_ != nullptr) {
    return false;
  }

  this->client_id_ = this->state_.data()->installation_id;
  this->client_id_ += "-crash";
  esp_mqtt_client_config_t config{};
  config.broker.address.hostname = this->broker_.c_str();
  config.broker.address.port = this->port_;
  config.broker.address.transport = this->tls_ ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
  if (this->tls_) config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
  config.credentials.client_id = this->client_id_.c_str();
  config.session.keepalive = 30;
  config.session.disable_clean_session = false;
  config.network.timeout_ms = 10000;
  config.network.disable_auto_reconnect = true;
  config.task.stack_size = MQTT_TASK_STACK_SIZE;
  config.buffer.size = 4096;
  config.buffer.out_size = 4096;
  config.outbox.limit = 8192;
  if (!this->username_.empty()) config.credentials.username = this->username_.c_str();
  if (!this->password_.empty()) config.credentials.authentication.password = this->password_.c_str();

  this->mqtt_client_ = esp_mqtt_client_init(&config);
  if (this->mqtt_client_ == nullptr) {
    return false;
  }
  // On partial initialization the handle is kept for the central worker
  // cleanup; never destroy from multiple places.
  esp_err_t error = esp_mqtt_client_register_event(this->mqtt_client_, MQTT_EVENT_ANY, mqtt_event_handler_, this);
  if (error == ESP_OK) {
    error = esp_mqtt_client_start(this->mqtt_client_);
  }
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "Could not start crash MQTT client: %s", esp_err_to_name(error));
    return false;
  }
  this->mqtt_client_started_ = true;
  ESP_LOGD(TAG, "Started %s publication",
           this->active_kind_.load() == CrashPublishKind::CRASH ? "crash" : "retained tombstone");
  return true;
}

void OpenQuattCrashTelemetry::worker_task_(void* arg) {
  auto* self = static_cast<OpenQuattCrashTelemetry*>(arg);
  if (self == nullptr) {
    while (true) {
      vTaskSuspend(nullptr);
    }
  }

  while (true) {
    uint32_t command_value = 0U;
    if (xTaskNotifyWait(0U, UINT32_MAX, &command_value, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    const WorkerCommand command = static_cast<WorkerCommand>(command_value);
    if (command == WorkerCommand::START) {
      if (!self->start_client_()) {
        self->session_failed_.store(true);
      }
      self->start_task_complete_.store(true);
      ESP_LOGD(TAG, "Crash telemetry worker stack free after start: %u bytes",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
      App.wake_loop_threadsafe();
      continue;
    }

    if (command == WorkerCommand::CLEANUP) {
      while (!self->cleanup_client_()) {
        vTaskDelay(pdMS_TO_TICKS(WORKER_CLEANUP_RETRY_MS));
      }
      ESP_LOGD(TAG, "Crash telemetry worker stack free after cleanup: %u bytes",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
      self->cleanup_task_complete_.store(true);
      App.wake_loop_threadsafe();
      if (!MQTT_WORKER_STACK_IN_PSRAM) {
        vTaskSuspend(nullptr);
      }
      continue;
    }

    ESP_LOGE(TAG, "Crash telemetry worker received invalid command: %u", static_cast<unsigned>(command_value));
  }
}

bool OpenQuattCrashTelemetry::cleanup_client_() {
  // Worker only. Never delete the worker while it may still run inside
  // ESP-IDF code; a permanently stuck stop just parks this boot's session.
  esp_mqtt_client_handle_t client = this->mqtt_client_;
  if (client == nullptr) return true;

  if (this->mqtt_client_started_) {
    const esp_err_t error = esp_mqtt_client_stop(client);
    if (error != ESP_OK && error != ESP_FAIL) {
      ESP_LOGE(TAG, "Unexpected crash telemetry MQTT stop error: %s", esp_err_to_name(error));
      return false;
    }
    if (error != ESP_OK) {
      ++this->cleanup_stop_failures_;
      const CrashCleanupDecision decision =
          select_crash_cleanup_decision(false, this->mqtt_connected_seen_.load(), this->mqtt_disconnected_seen_.load(),
                                        this->cleanup_stop_failures_, this->cleanup_disconnect_requested_);
      if (decision == CrashCleanupDecision::FORCE_DISCONNECT) {
        // A connected client may fail to construct its graceful DISCONNECT
        // packet under memory pressure. Its own task handles the disconnect by
        // aborting the transport without allocating that packet.
        if (!this->cleanup_disconnect_requested_) {
          ESP_LOGW(TAG, "Crash telemetry MQTT stop failed (%s); forcing disconnect before retry",
                   esp_err_to_name(error));
        }
        esp_mqtt_client_disconnect(client);
        this->cleanup_disconnect_requested_ = true;
        return false;
      }

      // ESP-IDF also returns ESP_FAIL when the mqtt task has already stopped
      // itself (for example after a transport-allocation failure). Allow one
      // full worker interval for its STOPPED tail to finish before destroy.
      if (decision == CrashCleanupDecision::RETRY_STOP) {
        ESP_LOGD(TAG, "Crash telemetry MQTT task may already be stopped; verifying before destroy");
        return false;
      }
      ESP_LOGW(TAG, "Crash telemetry MQTT task stopped before cleanup; releasing its client resources");
    }
  }

  const esp_err_t error = esp_mqtt_client_destroy(client);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Failed to destroy crash telemetry MQTT client: %s", esp_err_to_name(error));
    return false;
  }
  this->mqtt_client_ = nullptr;
  this->mqtt_client_started_ = false;
  this->cleanup_stop_failures_ = 0U;
  this->cleanup_disconnect_requested_ = false;
  return true;
}

void OpenQuattCrashTelemetry::request_session_finish_(bool publication_succeeded) {
  // Main loop only: request worker cleanup, never perform it. Cleanup is sent
  // only after the start worker completed, so a pending START notification
  // cannot be overwritten by CLEANUP.
  if (!this->session_active_.load() || this->start_task_running_.load() || this->finishing_session_.exchange(true)) {
    return;
  }
  this->publication_result_succeeded_ = publication_succeeded;
  this->worker_operation_started_ms_ = millis();
  this->worker_stall_logged_ = false;
  if (!this->notify_worker_(WorkerCommand::CLEANUP)) {
    this->finishing_session_.store(false);
    this->session_failed_.store(true);
    ESP_LOGE(TAG, "Failed to notify crash telemetry MQTT cleanup");
  }
}

void OpenQuattCrashTelemetry::finalize_session_() {
  // Main loop only, after the worker confirmed full client destruction.
  // PUBACK alone is not enough to release buffers: the MQTT task may still
  // reference them until destroy completed.
  const CrashPublishKind kind = this->active_kind_.load();
  const bool succeeded = this->publication_result_succeeded_;
  bool finalized = true;

  if (succeeded && kind == CrashPublishKind::CRASH) {
    if (!this->clear_record_()) {
      finalized = false;
      ESP_LOGW(TAG, "Crash was acknowledged, but local record clearing failed; retrying is harmless");
    }
  } else if (succeeded && kind == CrashPublishKind::TOMBSTONE) {
    this->state_.data()->tombstone_pending = 0U;
    if (!this->save_state_()) {
      this->state_.data()->tombstone_pending = 1U;
      finalized = false;
      ESP_LOGW(TAG, "Retained tombstone was acknowledged, but pending state clearing failed");
    }
  } else {
    finalized = false;
  }

  this->session_active_.store(false);
  this->session_succeeded_.store(false);
  this->session_failed_.store(false);
  this->pending_message_id_.store(-1);
  this->active_kind_.store(CrashPublishKind::NONE);
  this->finishing_session_.store(false);
  this->publication_result_succeeded_ = false;
  this->topic_buffer_.release();
  this->payload_buffer_.release();
  this->payload_size_ = 0U;

  if (finalized) {
    this->next_attempt_ms_ = 0U;
    ESP_LOGI(TAG, "%s published successfully", kind == CrashPublishKind::CRASH ? "Crash" : "Crash tombstone");
  } else {
    // Record/tombstone stays intact for retry under the same message ID, so a
    // lost QoS 1 PUBACK remains idempotent for ingestion.
    this->schedule_retry_();
  }
}

bool OpenQuattCrashTelemetry::time_reached_(uint32_t now_ms, uint32_t target_ms) {
  return static_cast<int32_t>(now_ms - target_ms) >= 0;
}

void OpenQuattCrashTelemetry::schedule_retry_() { this->next_attempt_ms_ = millis() + INITIAL_RETRY_MS; }

void OpenQuattCrashTelemetry::schedule_immediate_() { this->next_attempt_ms_ = millis() + 1U; }

void OpenQuattCrashTelemetry::loop() {
  // The loopTask only observes worker completions and asks for cleanup. It
  // never runs MQTT lifecycle calls or waits on network I/O itself.
  if (this->start_task_complete_.exchange(false)) {
    this->start_task_running_.store(false);
  }
  if (this->cleanup_task_complete_.exchange(false)) {
    if (!MQTT_WORKER_STACK_IN_PSRAM) {
      const TaskHandle_t handle = this->worker_task_state_.get_handle();
      if (handle != nullptr && eTaskGetState(handle) != eSuspended) {
        // The classic-ESP32 worker publishes completion immediately before it
        // parks itself. Do not free a static stack that may still be executing
        // on the other core.
        this->cleanup_task_complete_.store(true);
        return;
      }
      this->worker_task_state_.deallocate();
      this->worker_task_region_valid_ = false;
    }
    this->finalize_session_();
  }
  if (this->start_task_running_.load() || this->finishing_session_.load()) {
    // Signal only: a stuck worker must never stall the controller loop or
    // trigger a second session while the first is unresolved.
    if (!this->worker_stall_logged_ &&
        time_reached_(millis(), this->worker_operation_started_ms_ + WORKER_STALL_LOG_MS)) {
      ESP_LOGW(TAG, "Crash telemetry MQTT worker has not completed within %u ms; controller loop remains active",
               static_cast<unsigned>(WORKER_STALL_LOG_MS));
      this->worker_stall_logged_ = true;
    }
    return;
  }

  if (this->session_active_.load()) {
    if (this->active_kind_.load() == CrashPublishKind::CRASH && !this->consent_enabled_.load()) {
      this->session_failed_.store(true);
    }
    const bool timed_out = time_reached_(millis(), this->session_started_ms_ + SESSION_TIMEOUT_MS);
    switch (select_crash_session_action(this->session_active_.load(), this->start_task_running_.load(),
                                        this->finishing_session_.load(), this->session_succeeded_.load(),
                                        this->session_failed_.load(), timed_out)) {
      case CrashSessionAction::FINISH_SUCCESS:
        this->request_session_finish_(true);
        break;
      case CrashSessionAction::FINISH_FAILURE:
        this->request_session_finish_(false);
        break;
      case CrashSessionAction::NONE:
        break;
    }
    return;
  }

  if (!this->state_ || !this->record_ || !this->consent_seen_ || !this->is_configured() || !network::is_connected())
    return;
  const CrashPublishKind kind =
      select_crash_publish_kind(this->state_.data()->tombstone_pending != 0U, this->consent_enabled_.load(),
                                this->setup_complete_.load(), this->record_.data()->pending != 0U);
  if (kind == CrashPublishKind::NONE || !valid_installation_id(this->state_.data()->installation_id)) return;
  if (this->next_attempt_ms_ == 0U) this->next_attempt_ms_ = millis() + initial_publish_delay_ms(kind);
  if (!time_reached_(millis(), this->next_attempt_ms_)) return;
  if (should_wait_for_time_sync(kind, this->time_synchronized_.load(), millis(), this->time_sync_deadline_ms_)) return;
  this->start_publish_session_(kind);
}

void OpenQuattCrashTelemetry::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt crash telemetry:");
  ESP_LOGCONFIG(TAG, "  Broker configured: %s", YESNO(this->is_configured()));
  ESP_LOGCONFIG(TAG, "  Worker stack: %u bytes in %s", static_cast<unsigned>(MQTT_WORKER_TASK_STACK_SIZE),
                MQTT_WORKER_STACK_IN_PSRAM ? "PSRAM" : "internal RAM");
  ESP_LOGCONFIG(TAG, "  Pending crash: %s", YESNO(this->record_ && this->record_.data()->pending != 0U));
  ESP_LOGCONFIG(TAG, "  Tombstone pending: %s", YESNO(this->state_ && this->state_.data()->tombstone_pending != 0U));
  ESP_LOGCONFIG(TAG, "  Consent observed: %s", YESNO(this->consent_seen_));
  ESP_LOGCONFIG(TAG, "  Time synchronized this boot: %s", YESNO(this->time_synchronized_.load()));
}

void OpenQuattCrashTelemetry::mqtt_event_handler_(void* handler_args, esp_event_base_t base, int32_t event_id,
                                                  void* event_data) {
  (void)base;
  auto* self = static_cast<OpenQuattCrashTelemetry*>(handler_args);
  auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
  if (self == nullptr || event == nullptr) return;

  // Connection tracking always runs first: the worker needs the disconnect
  // signal during cleanup, while publish callbacks must no longer touch the
  // session once finishing started.
  if (event_id == MQTT_EVENT_CONNECTED) {
    self->mqtt_connected_seen_.store(true);
  } else if (event_id == MQTT_EVENT_DISCONNECTED) {
    self->mqtt_disconnected_seen_.store(true);
  }
  if (!self->session_active_.load() || self->finishing_session_.load()) {
    return;
  }

  switch (event_id) {
    case MQTT_EVENT_CONNECTED: {
      if (!self->lock_gate_()) {
        self->session_failed_.store(true);
        App.wake_loop_threadsafe();
        break;
      }
      const CrashPublishKind kind = self->active_kind_.load();
      int message_id = -1;
      if (crash_data_may_be_published(kind, self->consent_enabled_.load(), self->setup_complete_.load())) {
        static const char EMPTY_PAYLOAD[] = "";
        const char* payload = kind == CrashPublishKind::TOMBSTONE ? EMPTY_PAYLOAD : self->payload_buffer_.data();
        const size_t payload_size = kind == CrashPublishKind::TOMBSTONE ? 0U : self->payload_size_;
        const int retain = crash_publication_is_retained(kind) ? 1 : 0;
        message_id = esp_mqtt_client_enqueue(event->client, self->topic_buffer_.data(), payload,
                                             static_cast<int>(payload_size), 1, retain, true);
      }
      self->unlock_gate_();
      if (message_id < 0) {
        self->session_failed_.store(true);
      } else {
        self->pending_message_id_.store(message_id);
      }
      App.wake_loop_threadsafe();
      break;
    }
    case MQTT_EVENT_PUBLISHED:
      if (event->msg_id == self->pending_message_id_.load()) {
        self->session_succeeded_.store(true);
        App.wake_loop_threadsafe();
      }
      break;
    case MQTT_EVENT_ERROR:
      self->session_failed_.store(true);
      App.wake_loop_threadsafe();
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (!self->session_succeeded_.load()) {
        self->session_failed_.store(true);
        App.wake_loop_threadsafe();
      }
      break;
    default:
      break;
  }
}

}  // namespace esphome::openquatt_crash_telemetry
