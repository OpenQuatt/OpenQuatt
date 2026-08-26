#include "OpenQuattCrashTelemetry.h"
#include "OpenQuattCrashTelemetryHelpers.h"

#include "esp_crt_bundle.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

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
    if (now.is_valid() && timestamp >= static_cast<int64_t>(openquatt_log_history::MIN_VALID_CRASH_EPOCH_S) &&
        timestamp < static_cast<int64_t>(openquatt_log_history::MAX_VALID_CRASH_EPOCH_S)) {
      reported_at = static_cast<uint32_t>(timestamp);
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

bool OpenQuattCrashTelemetry::start_session_(CrashPublishKind kind) {
  if (kind == CrashPublishKind::NONE || this->session_active_.load() || !this->build_topic_()) return false;
  if (kind == CrashPublishKind::CRASH && !this->build_crash_payload_()) {
    this->topic_buffer_.release();
    return false;
  }
  if (kind == CrashPublishKind::TOMBSTONE) {
    this->payload_buffer_.release();
    this->payload_size_ = 0U;
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

  this->session_succeeded_.store(false);
  this->session_failed_.store(false);
  this->pending_message_id_.store(-1);
  this->active_kind_.store(kind);
  this->cleanup_attempts_ = 0U;
  this->session_started_ms_ = millis();
  this->session_active_.store(true);

  this->mqtt_client_ = esp_mqtt_client_init(&config);
  if (this->mqtt_client_ == nullptr) {
    this->session_active_.store(false);
    this->active_kind_.store(CrashPublishKind::NONE);
    return false;
  }
  esp_err_t error = esp_mqtt_client_register_event(this->mqtt_client_, MQTT_EVENT_ANY, mqtt_event_handler_, this);
  if (error == ESP_OK) error = esp_mqtt_client_start(this->mqtt_client_);
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "Could not start crash MQTT client: %s", esp_err_to_name(error));
    esp_mqtt_client_destroy(this->mqtt_client_);
    this->mqtt_client_ = nullptr;
    this->session_active_.store(false);
    this->active_kind_.store(CrashPublishKind::NONE);
    return false;
  }
  this->mqtt_client_started_ = true;
  ESP_LOGD(TAG, "Started %s publication", kind == CrashPublishKind::CRASH ? "crash" : "retained tombstone");
  return true;
}

bool OpenQuattCrashTelemetry::stop_client_() {
  if (this->mqtt_client_ == nullptr) return true;
  if (this->mqtt_client_started_) {
    const esp_err_t stop_error = esp_mqtt_client_stop(this->mqtt_client_);
    if (stop_error != ESP_OK) {
      ++this->cleanup_attempts_;
      if (this->cleanup_attempts_ == 1U) esp_mqtt_client_disconnect(this->mqtt_client_);
      if (this->cleanup_attempts_ < 3U) return false;
      ESP_LOGW(TAG, "Crash MQTT stop did not confirm; releasing the one-shot client");
    }
  }
  const esp_err_t destroy_error = esp_mqtt_client_destroy(this->mqtt_client_);
  if (destroy_error != ESP_OK) return false;
  this->mqtt_client_ = nullptr;
  this->mqtt_client_started_ = false;
  this->cleanup_attempts_ = 0U;
  return true;
}

void OpenQuattCrashTelemetry::complete_session_(bool succeeded) {
  if (!this->stop_client_()) return;
  const CrashPublishKind kind = this->active_kind_.load();

  if (succeeded && kind == CrashPublishKind::CRASH) {
    if (!this->clear_record_()) {
      succeeded = false;
      ESP_LOGW(TAG, "Crash was acknowledged, but local record clearing failed; retrying is harmless");
    }
  } else if (succeeded && kind == CrashPublishKind::TOMBSTONE) {
    this->state_.data()->tombstone_pending = 0U;
    if (!this->save_state_()) {
      this->state_.data()->tombstone_pending = 1U;
      succeeded = false;
      ESP_LOGW(TAG, "Retained tombstone was acknowledged, but pending state clearing failed");
    }
  }

  this->session_active_.store(false);
  this->session_succeeded_.store(false);
  this->session_failed_.store(false);
  this->pending_message_id_.store(-1);
  this->active_kind_.store(CrashPublishKind::NONE);
  this->topic_buffer_.release();
  this->payload_buffer_.release();
  this->payload_size_ = 0U;

  if (succeeded) {
    this->next_attempt_ms_ = 0U;
    ESP_LOGI(TAG, "%s published successfully", kind == CrashPublishKind::CRASH ? "Crash" : "Crash tombstone");
  } else {
    this->schedule_retry_();
  }
}

void OpenQuattCrashTelemetry::schedule_retry_() { this->next_attempt_ms_ = millis() + INITIAL_RETRY_MS; }

void OpenQuattCrashTelemetry::schedule_immediate_() { this->next_attempt_ms_ = millis() + 1U; }

void OpenQuattCrashTelemetry::loop() {
  if (this->session_active_.load()) {
    const bool timed_out = static_cast<int32_t>(millis() - (this->session_started_ms_ + SESSION_TIMEOUT_MS)) >= 0;
    if (this->active_kind_.load() == CrashPublishKind::CRASH && !this->consent_enabled_.load()) {
      this->session_failed_.store(true);
    }
    if (this->session_succeeded_.load()) {
      this->complete_session_(true);
    } else if (this->session_failed_.load() || timed_out) {
      this->complete_session_(false);
    }
    return;
  }

  if (!this->state_ || !this->record_ || !this->consent_seen_ || !this->is_configured() || !network::is_connected())
    return;
  const CrashPublishKind kind =
      select_crash_publish_kind(this->state_.data()->tombstone_pending != 0U, this->consent_enabled_.load(),
                                this->setup_complete_.load(), this->record_.data()->pending != 0U);
  if (kind == CrashPublishKind::NONE || !valid_installation_id(this->state_.data()->installation_id)) return;
  if (this->next_attempt_ms_ == 0U) this->next_attempt_ms_ = millis() + INITIAL_PUBLISH_DELAY_MS;
  if (static_cast<int32_t>(millis() - this->next_attempt_ms_) < 0) return;
  if (!this->start_session_(kind)) this->schedule_retry_();
}

void OpenQuattCrashTelemetry::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt crash telemetry:");
  ESP_LOGCONFIG(TAG, "  Broker configured: %s", YESNO(this->is_configured()));
  ESP_LOGCONFIG(TAG, "  Pending crash: %s", YESNO(this->record_ && this->record_.data()->pending != 0U));
  ESP_LOGCONFIG(TAG, "  Tombstone pending: %s", YESNO(this->state_ && this->state_.data()->tombstone_pending != 0U));
  ESP_LOGCONFIG(TAG, "  Consent observed: %s", YESNO(this->consent_seen_));
}

void OpenQuattCrashTelemetry::mqtt_event_handler_(void* handler_args, esp_event_base_t base, int32_t event_id,
                                                  void* event_data) {
  (void)base;
  auto* self = static_cast<OpenQuattCrashTelemetry*>(handler_args);
  auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
  if (self == nullptr || event == nullptr || !self->session_active_.load()) return;

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
