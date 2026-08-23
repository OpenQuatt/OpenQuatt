#include "OpenQuattUsageTelemetry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "freertos/idf_additions.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3) && __has_include("heatpump_controller_q_hardware_revision.h")
#include "heatpump_controller_q_hardware_revision.h"
#define OPENQUATT_HAS_Q_HARDWARE_REVISION
#endif
#include "esp_system.h"
#include "esp_timer.h"
#include "esphome/components/network/util.h"
#include "esphome/components/openquatt_log_history/OpenQuattLogHistory.h"
#include "esphome/components/openquatt_mqtt_config/OpenQuattMqttConfig.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_usage_telemetry {
namespace {

static const char* const TAG = "openquatt.usage_telemetry";
static const uint32_t STORAGE_KEY = fnv1_hash("openquatt_usage_telemetry_store");
static const uint32_t CLEANUP_STORAGE_KEY = fnv1_hash("openquatt_usage_telemetry_crash_cleanup");
static constexpr size_t TELEMETRY_PAYLOAD_CAPACITY = 2048U;
static constexpr size_t CRASH_PAYLOAD_CAPACITY = 2048U;
static constexpr size_t CRASH_TELEMETRY_BACKTRACE_CAPACITY = openquatt_log_history::CRASH_BACKTRACE_CAPACITY;

void log_heap_state_(const char* phase) {
  const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const size_t fragmentation_percent =
      free_internal == 0U ? 0U : 100U - std::min<size_t>(100U, (largest_internal * 100U) / free_internal);
  ESP_LOGD(TAG,
           "%s: heap free=%u, min=%u, largest=%u, fragmentation=%u%%, "
           "PSRAM free=%u",
           phase, static_cast<unsigned>(free_internal),
           static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(largest_internal), static_cast<unsigned>(fragmentation_percent),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

bool uuid_is_present_(const std::array<uint8_t, 16>& bytes) {
  return std::any_of(bytes.begin(), bytes.end(), [](uint8_t byte) { return byte != 0U; });
}

void append_json_key_(FixedBufferWriter& payload, const char* key) {
  payload += R"(,")";
  payload += key;
  payload += R"(":)";
}

template <size_t N>
void append_json_optional_fixed_value_(FixedBufferWriter& payload, const char (&value)[N], bool valid) {
  size_t length = 0U;
  while (length < N && value[length] != '\0') {
    ++length;
  }
  if (!valid || length == 0U || length == N) {
    payload += "null";
    return;
  }
  payload += '"';
  append_json_escaped(payload, value, length);
  payload += '"';
}

template <size_t N>
bool fixed_hex_string_is_valid_(const char (&value)[N], size_t required_length) {
  if (required_length >= N || value[required_length] != '\0') {
    return false;
  }
  for (size_t index = 0U; index < required_length; ++index) {
    const char c = value[index];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

template <size_t N>
void append_json_optional_fixed_string_(FixedBufferWriter& payload, const char* key, const char (&value)[N],
                                        bool valid) {
  append_json_key_(payload, key);
  append_json_optional_fixed_value_(payload, value, valid);
}

void append_json_optional_uint64_(FixedBufferWriter& payload, const char* key, uint64_t value, bool valid) {
  append_json_key_(payload, key);
  if (!valid) {
    payload += "null";
    return;
  }
  payload.append_uint(value);
}

void append_json_optional_string_(FixedBufferWriter& payload, const char* key, const std::string& value) {
  append_json_key_(payload, key);
  if (value.empty()) {
    payload += "null";
    return;
  }
  payload += '"';
  append_json_escaped(payload, value);
  payload += '"';
}

void append_json_address_(FixedBufferWriter& payload, const char* key, uint32_t address, bool valid) {
  append_json_key_(payload, key);
  if (!valid) {
    payload += "null";
    return;
  }
  char value[13];
  std::snprintf(value, sizeof(value), "\"0x%08" PRIx32 "\"", address);
  payload += value;
}

void append_json_backtrace_(FixedBufferWriter& payload, const char* key,
                            const openquatt_log_history::CrashBacktrace& backtrace, bool valid, size_t capacity) {
  append_json_key_(payload, key);
  payload += '[';
  if (valid) {
    const size_t count = std::min<size_t>({backtrace.count, backtrace.addresses.size(), capacity});
    for (size_t index = 0U; index < count; ++index) {
      if (index != 0U) {
        payload += ',';
      }
      char value[13];
      std::snprintf(value, sizeof(value), "\"0x%08" PRIx32 "\"", backtrace.addresses[index]);
      payload += value;
    }
  }
  payload += ']';
}

void append_json_uint_(FixedBufferWriter& payload, const char* key, size_t value) {
  append_json_key_(payload, key);
  payload.append_uint(static_cast<uint64_t>(value));
}

void append_json_optional_number_(FixedBufferWriter& payload, const char* key, const sensor::Sensor* source,
                                  unsigned decimals) {
  append_json_key_(payload, key);
  if (source == nullptr || !source->has_state() || !std::isfinite(source->state)) {
    payload += "null";
    return;
  }
  char value[32];
  std::snprintf(value, sizeof(value), "%.*f", static_cast<int>(decimals), source->state);
  payload += value;
}

void append_json_optional_bool_(FixedBufferWriter& payload, const char* key, const switch_::Switch* source) {
  append_json_key_(payload, key);
  if (source == nullptr) {
    payload += "null";
    return;
  }
  payload += source->state ? "true" : "false";
}

void append_json_optional_bool_(FixedBufferWriter& payload, const char* key, bool available, bool value) {
  append_json_key_(payload, key);
  if (!available) {
    payload += "null";
    return;
  }
  payload += value ? "true" : "false";
}

void append_json_wire_value_(FixedBufferWriter& payload, const char* key, const char* value) {
  append_json_key_(payload, key);
  if (value == nullptr) {
    payload += "null";
    return;
  }
  payload += '"';
  payload += value;
  payload += '"';
}

void append_json_optional_select_(FixedBufferWriter& payload, const char* key, const select::Select* source,
                                  const char* (*wire_value)(const std::string&)) {
  if (source == nullptr || !source->has_state()) {
    append_json_wire_value_(payload, key, nullptr);
    return;
  }
  append_json_wire_value_(payload, key, wire_value(source->current_option()));
}

void append_json_flow_source_(FixedBufferWriter& payload, const select::Select* flow_source,
                              const select::Select* q_flow_source) {
  const std::string empty_option;
  const std::string& flow_option =
      flow_source != nullptr && flow_source->has_state() ? flow_source->current_option() : empty_option;
  const bool q_source_available = q_flow_source != nullptr;
  const std::string& q_flow_option =
      q_source_available && q_flow_source->has_state() ? q_flow_source->current_option() : empty_option;
  append_json_wire_value_(payload, "flow_source_config",
                          flow_source_config_wire_value(flow_option, q_source_available, q_flow_option));
}

void append_json_boiler_connection_(FixedBufferWriter& payload, const select::Select* source) {
  append_json_key_(payload, "boiler_connection");
  if (source == nullptr) {
    // Firmware without the OTB transport selector can only drive the R1 on/off route.
    payload += R"("on_off")";
    return;
  }
  if (!source->has_state()) {
    payload += "null";
    return;
  }
  const std::string& option = source->current_option();
  // Keep the wire values independent from the user-facing select labels.
  if (option == "R1") {
    payload += R"("on_off")";
  } else if (option == "OpenTherm") {
    payload += R"("opentherm")";
  } else {
    payload += "null";
  }
}

const char* reset_reason_name_(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power_on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
      return "task_watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep_sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    default:
      return "unknown";
  }
}

}  // namespace

float OpenQuattUsageTelemetry::get_setup_priority() const { return setup_priority::LATE; }

void OpenQuattUsageTelemetry::setup() {
  this->setup_complete_gate_.store(this->is_setup_complete_());
  this->consent_mutex_ = xSemaphoreCreateMutexStatic(&this->consent_mutex_storage_);
  if (this->consent_mutex_ == nullptr) {
    ESP_LOGE(TAG,
             "Failed to initialize usage telemetry consent gate; "
             "usage statistics remain disabled");
    this->publish_state(false);
    this->mark_failed();
    return;
  }
  if (this->setup_complete_sensor_ != nullptr) {
    this->setup_complete_sensor_->add_on_state_callback([this](bool state) {
      if (!this->set_setup_complete_gate_(state)) {
        ESP_LOGE(TAG, "Could not update setup-complete telemetry gate; usage statistics remain blocked");
      }
      App.wake_loop_threadsafe();
    });
  }
  if (global_preferences == nullptr) {
    ESP_LOGE(TAG, "Preferences backend is unavailable; usage statistics remain disabled");
    this->publish_state(false);
    return;
  }
  if (this->crash_provider_ != nullptr && !this->crash_snapshot_.allocate_external(1U)) {
    ESP_LOGW(TAG, "Crash telemetry scratch space is unavailable; normal usage statistics remain available");
  }

  this->pref_ = global_preferences->make_preference<Storage>(STORAGE_KEY, true);
  this->cleanup_pref_ = global_preferences->make_preference<CleanupStorage>(CLEANUP_STORAGE_KEY, true);
  Storage storage{};
  if (!this->load_storage_(&storage)) {
    storage.magic = STORAGE_MAGIC;
    storage.version = STORAGE_VERSION;
    storage.enabled = 0;
    storage.choice_configured = 0;
    storage.installation_id_present = 0;
    storage.reserved.fill(0);
    storage.installation_id.fill(0);

    StorageV1 legacy_storage{};
    const bool migrated_legacy = this->load_legacy_storage_(&legacy_storage);
    if (migrated_legacy) {
      // Storage v1 could not distinguish a deliberate opt-in from the old
      // default-on migration. Reset it once, preserving only the anonymous ID.
      storage.choice_configured = 1;
      storage.installation_id_present = legacy_storage.installation_id_present;
      storage.installation_id = legacy_storage.installation_id;
    }

    const bool initialized = this->save_storage_(storage);
    if (!initialized) {
      ESP_LOGE(TAG, "Failed to initialize usage telemetry preferences; usage statistics remain disabled");
      storage.enabled = 0;
      storage.choice_configured = 0;
      storage.installation_id_present = 0;
      storage.reserved.fill(0);
      storage.installation_id.fill(0);
    } else if (migrated_legacy) {
      ESP_LOGI(TAG, "Migrated legacy usage telemetry preference to disabled");
    } else {
      ESP_LOGI(TAG, "No usage telemetry choice found; remaining disabled until onboarding records a choice");
    }
  }

  CleanupStorage cleanup{};
  if (this->load_cleanup_storage_(&cleanup) && cleanup.pending != 0U) {
    this->apply_pending_cleanup_(cleanup, &storage, true);
  } else if (storage.installation_id_present != 0U && storage.reserved[CLEANUP_REQUIRED_RESERVED_INDEX] != 0U) {
    cleanup.magic = CLEANUP_STORAGE_MAGIC;
    cleanup.version = CLEANUP_STORAGE_VERSION;
    cleanup.pending = 1U;
    cleanup.installation_id = storage.installation_id;
    this->apply_pending_cleanup_(cleanup, &storage, false);
  } else if (retained_crash_cleanup_required_on_disabled_boot(
                 storage.enabled != 0U, storage.installation_id_present != 0U,
                 storage.reserved[CRASH_PUBLISH_MAY_HAVE_REACHED_RESERVED_INDEX] != 0U)) {
    // A pre-crash-telemetry downgrade can preserve the anonymous ID and a
    // retained-crash marker while knowing nothing about the crash topic. A
    // durable may-have-reached marker was set before the crash MQTT session
    // started, so the topic must be tombstoned even if its PUBACK was lost.
    this->cleanup_installation_id_bytes_ = storage.installation_id;
    this->cleanup_installation_id_ = format_uuid_(storage.installation_id);
    this->cleanup_pending_.store(true);
    this->cleanup_wal_durable_.store(this->persist_cleanup_wal_());
    this->cleanup_main_durable_.store(this->persist_cleanup_main_intent_());
    if (!retained_cleanup_recoverable_after_reboot(this->cleanup_wal_durable_.load(),
                                                   this->cleanup_main_durable_.load())) {
      this->schedule_cleanup_persist_retry_();
    }
  } else {
    this->cleanup_pending_.store(false);
    this->cleanup_wal_durable_.store(false);
    this->cleanup_main_durable_.store(false);
  }

  if (!this->apply_storage_(storage)) {
    ESP_LOGE(TAG,
             "Failed to apply usage telemetry consent state; "
             "usage statistics remain disabled");
    this->publish_state(false);
    this->mark_failed();
    return;
  }
  if (!this->enabled_.load() && !this->cleanup_pending_.load() && !this->discard_pending_crash_()) {
    ESP_LOGW(TAG, "Crash captured while usage statistics were disabled remains blocked and will not be published");
  }
  this->boot_publish_pending_ = this->enabled_.load() || this->cleanup_pending_.load();
  this->next_publish_ms_ = 0;
}

void OpenQuattUsageTelemetry::loop() {
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
    this->complete_publish_session_();
  }
  this->retry_cleanup_persistence_();
  if (this->finishing_session_.load()) {
    return;
  }

  if (this->session_active_.load()) {
    if (this->start_task_running_.load()) {
      return;
    }
    if (!this->session_publish_allowed_(this->active_publish_kind_.load())) {
      this->finish_publish_session_(false);
      return;
    }
    if (this->publish_succeeded_.load()) {
      this->finish_publish_session_(true);
      return;
    }
    if (this->publish_failed_.load() || time_reached_(millis(), this->session_started_ms_ + SESSION_TIMEOUT_MS)) {
      this->finish_publish_session_(false);
    }
    return;
  }

  if (!this->is_configured() || !network::is_connected()) {
    if (this->boot_publish_pending_) {
      this->next_publish_ms_ = 0;
    }
    return;
  }
  if (!this->cleanup_pending_.load() && (!this->enabled_.load() || !this->setup_complete_gate_.load())) {
    if (this->boot_publish_pending_) {
      this->next_publish_ms_ = 0;
    }
    return;
  }
  if (this->next_publish_ms_ == 0U) {
    if (this->boot_publish_pending_) {
      this->schedule_initial_publish_();
    } else {
      this->schedule_immediate_publish_();
    }
  }
  if (time_reached_(millis(), this->next_publish_ms_)) {
    if (this->cleanup_pending_.load()) {
      if (!retained_cleanup_ready_for_tombstone(this->cleanup_wal_durable_.load(),
                                                this->cleanup_main_durable_.load())) {
        this->schedule_retry_();
        return;
      }
      this->start_publish_session_(PublishKind::TOMBSTONE);
      return;
    }
    const PublishKind kind = this->crash_provider_ != nullptr && this->crash_provider_->has_pending_crash()
                                 ? PublishKind::CRASH
                                 : PublishKind::TELEMETRY;
    this->start_publish_session_(kind);
  }
}

void OpenQuattUsageTelemetry::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt usage statistics:");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", YESNO(this->enabled_.load()));
  ESP_LOGCONFIG(TAG, "  Broker configured: %s", YESNO(this->is_configured()));
  ESP_LOGCONFIG(TAG, "  Transport: %s", this->tls_ ? "MQTT/TLS" : "MQTT");
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  if (!this->tls_ && this->is_configured()) {
    ESP_LOGW(TAG, "Usage statistics transport is not encrypted");
  }
  ESP_LOGCONFIG(TAG, "  Choice configured: %s", YESNO(this->choice_configured_.load()));
  ESP_LOGCONFIG(TAG, "  Quick Start complete: %s", YESNO(this->is_setup_complete_()));
  ESP_LOGCONFIG(TAG, "  Publish interval: %" PRIu32 " seconds", this->interval_ms_ / 1000U);
  ESP_LOGCONFIG(TAG, "  Installation ID present: %s", YESNO(!this->installation_id_.empty()));
  ESP_LOGCONFIG(TAG, "  Retained crash cleanup pending: %s", YESNO(this->cleanup_pending_.load()));
  ESP_LOGCONFIG(TAG, "  Pending crash snapshot: %s",
                YESNO(this->crash_provider_ != nullptr && this->crash_provider_->has_pending_crash()));
  ESP_LOGCONFIG(TAG, "  OpenQuatt worker stack: %u bytes in %s", static_cast<unsigned>(MQTT_WORKER_TASK_STACK_SIZE),
                MQTT_WORKER_STACK_IN_PSRAM ? "PSRAM" : "internal RAM");
}

void OpenQuattUsageTelemetry::write_state(bool state) {
  const bool current_state = this->enabled_.load();
  if (state && this->cleanup_pending_.load()) {
    ESP_LOGW(TAG, "Usage statistics cannot be enabled until retained crash cleanup completes");
    this->publish_state(false);
    return;
  }
  if (!state && this->cleanup_pending_.load()) {
    // Cleanup already owns the immutable installation ID. Avoid rewriting its
    // strings while the MQTT callback may be reading them.
    this->publish_state(false);
    return;
  }
  if (!state && !this->set_consent_publish_blocked_(true)) {
    ESP_LOGE(TAG, "Could not close usage telemetry consent gate");
    this->publish_state(current_state);
    return;
  }
  Storage storage{};
  if (!this->load_storage_(&storage)) {
    storage.magic = STORAGE_MAGIC;
    storage.version = STORAGE_VERSION;
    storage.enabled = current_state ? 1U : 0U;
    storage.choice_configured = this->choice_configured_.load() ? 1U : 0U;
    storage.installation_id_present = this->installation_id_.empty() ? 0U : 1U;
    storage.reserved.fill(0);
    storage.installation_id = this->installation_id_bytes_;
  }
  if (state == current_state && storage.choice_configured != 0U && !this->cleanup_pending_.load()) {
    this->publish_state(current_state);
    return;
  }

  if (state) {
    if (!this->discard_pending_crash_()) {
      ESP_LOGE(TAG, "Could not discard a crash captured without consent; usage statistics remain disabled");
      this->publish_state(false);
      return;
    }
    if (!this->ensure_installation_id_(&storage)) {
      ESP_LOGE(TAG, "Could not generate an installation ID; usage statistics remain disabled");
      this->publish_state(false);
      return;
    }
    storage.enabled = 1U;
    storage.choice_configured = 1U;
    if (!this->save_storage_(storage)) {
      ESP_LOGE(TAG, "Could not persist usage statistics preference");
      this->publish_state(current_state);
      return;
    }
    if (!this->apply_storage_(storage)) {
      ESP_LOGE(TAG, "Could not apply usage statistics preference");
      this->publish_state(current_state);
      return;
    }
    this->consecutive_failures_ = 0;
    this->next_regular_telemetry_ms_ = 0U;
    if (this->is_setup_complete_()) {
      this->boot_publish_pending_ = false;
      this->schedule_immediate_publish_();
    } else {
      this->boot_publish_pending_ = true;
      this->next_publish_ms_ = 0;
    }
    if (!this->is_configured()) {
      ESP_LOGW(TAG, "Usage statistics were enabled, but no telemetry broker is configured in this build");
    }
    return;
  }

  storage.enabled = 0U;
  storage.choice_configured = 1U;
  if (storage.installation_id_present != 0U && uuid_is_present_(storage.installation_id)) {
    storage.reserved[CLEANUP_REQUIRED_RESERVED_INDEX] = 1U;
    this->cleanup_installation_id_bytes_ = storage.installation_id;
    this->cleanup_installation_id_ = format_uuid_(storage.installation_id);
    this->cleanup_pending_.store(true);
    this->cleanup_wal_durable_.store(this->persist_cleanup_wal_());
    if (!this->cleanup_wal_durable_.load()) {
      ESP_LOGE(TAG, "Could not persist retained crash cleanup intent; retrying while this boot remains active");
    }
  }

  const bool storage_saved = this->save_storage_(storage);
  this->cleanup_main_durable_.store(storage_saved && storage.reserved[CLEANUP_REQUIRED_RESERVED_INDEX] != 0U);
  if (!storage_saved) {
    // A failed opt-out write must not reopen telemetry. The in-memory gate was
    // already closed before this write and remains authoritative for this boot.
    ESP_LOGE(TAG,
             "Could not persist disabled usage statistics preference; runtime consent remains fail-closed, but do "
             "not reboot or downgrade until the automatic retry succeeds");
  }
  if (!retained_cleanup_recoverable_after_reboot(this->cleanup_wal_durable_.load(),
                                                 this->cleanup_main_durable_.load())) {
    this->schedule_cleanup_persist_retry_();
  }
  if (!this->apply_storage_(storage)) {
    ESP_LOGE(TAG, "Could not apply disabled usage statistics preference");
    this->enabled_.store(false);
    this->consent_publish_blocked_.store(true);
    this->publish_state(false);
  }
  this->boot_publish_pending_ = this->cleanup_pending_.load();
  this->consecutive_failures_ = 0U;
  this->next_regular_telemetry_ms_ = 0U;
  if (this->cleanup_pending_.load()) {
    this->schedule_immediate_publish_();
  } else {
    this->next_publish_ms_ = 0U;
  }
  if (!this->session_active_.load()) {
    this->clear_session_buffers_();
    this->payload_message_id_.clear();
  }
  App.wake_loop_threadsafe();
}

bool OpenQuattUsageTelemetry::load_storage_(Storage* storage) {
  if (storage == nullptr || !this->pref_.load(storage)) {
    return false;
  }
  if (storage->magic != STORAGE_MAGIC || storage->version != STORAGE_VERSION || storage->enabled > 1U ||
      storage->choice_configured > 1U || storage->installation_id_present > 1U ||
      storage->reserved[CLEANUP_REQUIRED_RESERVED_INDEX] > 1U ||
      storage->reserved[CRASH_PUBLISH_MAY_HAVE_REACHED_RESERVED_INDEX] > 1U ||
      storage->reserved[CRASH_ENDPOINT_GENERATION_RESERVED_INDEX] > CRASH_ENDPOINT_GENERATION ||
      ((storage->reserved[CRASH_PUBLISH_MAY_HAVE_REACHED_RESERVED_INDEX] != 0U) !=
       (storage->reserved[CRASH_ENDPOINT_GENERATION_RESERVED_INDEX] != 0U))) {
    return false;
  }
  const bool id_present = uuid_is_present_(storage->installation_id);
  if ((storage->installation_id_present != 0U) != id_present ||
      (storage->enabled != 0U && (storage->choice_configured == 0U || !id_present))) {
    return false;
  }
  return true;
}

bool OpenQuattUsageTelemetry::load_legacy_storage_(StorageV1* storage) {
  if (storage == nullptr || global_preferences == nullptr) {
    return false;
  }
  auto legacy_pref = global_preferences->make_preference<StorageV1>(STORAGE_KEY, true);
  if (!legacy_pref.load(storage) || storage->magic != STORAGE_MAGIC || storage->version != 1U ||
      storage->enabled > 1U || storage->installation_id_present > 1U) {
    return false;
  }
  const bool id_present = uuid_is_present_(storage->installation_id);
  return (storage->installation_id_present != 0U) == id_present && (storage->enabled == 0U || id_present);
}

bool OpenQuattUsageTelemetry::save_storage_(const Storage& storage) {
  if (!this->pref_.save(&storage) || global_preferences == nullptr || !global_preferences->sync()) {
    return false;
  }
  Storage verify{};
  return this->pref_.load(&verify) && std::memcmp(&storage, &verify, sizeof(storage)) == 0;
}

bool OpenQuattUsageTelemetry::load_cleanup_storage_(CleanupStorage* storage) {
  if (storage == nullptr || !this->cleanup_pref_.load(storage)) {
    return false;
  }
  if (storage->magic != CLEANUP_STORAGE_MAGIC || storage->version != CLEANUP_STORAGE_VERSION || storage->pending > 1U ||
      storage->reserved != 0U) {
    return false;
  }
  const bool id_present = uuid_is_present_(storage->installation_id);
  return (storage->pending != 0U) == id_present;
}

bool OpenQuattUsageTelemetry::save_cleanup_storage_(const CleanupStorage& storage) {
  if (!this->cleanup_pref_.save(&storage) || global_preferences == nullptr || !global_preferences->sync()) {
    return false;
  }
  CleanupStorage verify{};
  return this->cleanup_pref_.load(&verify) && std::memcmp(&storage, &verify, sizeof(storage)) == 0;
}

bool OpenQuattUsageTelemetry::persist_cleanup_wal_() {
  if (!this->cleanup_pending_.load() || !uuid_is_present_(this->cleanup_installation_id_bytes_)) {
    return false;
  }
  CleanupStorage cleanup{};
  cleanup.magic = CLEANUP_STORAGE_MAGIC;
  cleanup.version = CLEANUP_STORAGE_VERSION;
  cleanup.pending = 1U;
  cleanup.reserved = 0U;
  cleanup.installation_id = this->cleanup_installation_id_bytes_;
  const bool saved = this->save_cleanup_storage_(cleanup);
  this->cleanup_wal_durable_.store(saved);
  return saved;
}

bool OpenQuattUsageTelemetry::persist_cleanup_main_intent_() {
  if (!this->cleanup_pending_.load() || !uuid_is_present_(this->cleanup_installation_id_bytes_)) {
    return false;
  }
  Storage storage{};
  if (!this->load_storage_(&storage)) {
    storage.magic = STORAGE_MAGIC;
    storage.version = STORAGE_VERSION;
    storage.reserved.fill(0U);
  }
  storage.enabled = 0U;
  storage.choice_configured = 1U;
  storage.installation_id_present = 1U;
  storage.reserved[CLEANUP_REQUIRED_RESERVED_INDEX] = 1U;
  storage.installation_id = this->cleanup_installation_id_bytes_;
  const bool saved = this->save_storage_(storage);
  this->cleanup_main_durable_.store(saved);
  return saved;
}

void OpenQuattUsageTelemetry::schedule_cleanup_persist_retry_() {
  this->cleanup_persist_failures_ = std::min<uint8_t>(this->cleanup_persist_failures_ + 1U, 8U);
  uint32_t delay_ms = CLEANUP_PERSIST_RETRY_MIN_MS;
  for (uint8_t index = 1U; index < this->cleanup_persist_failures_ && delay_ms < CLEANUP_PERSIST_RETRY_MAX_MS;
       ++index) {
    delay_ms = std::min<uint32_t>(delay_ms * 2U, CLEANUP_PERSIST_RETRY_MAX_MS);
  }
  this->next_cleanup_persist_retry_ms_ = millis() + delay_ms;
}

void OpenQuattUsageTelemetry::retry_cleanup_persistence_() {
  if (!this->cleanup_pending_.load() ||
      (this->next_cleanup_persist_retry_ms_ != 0U && !time_reached_(millis(), this->next_cleanup_persist_retry_ms_))) {
    return;
  }

  if (!this->cleanup_wal_durable_.load()) {
    this->persist_cleanup_wal_();
  }
  if (!this->cleanup_main_durable_.load()) {
    this->persist_cleanup_main_intent_();
  }
  if (this->cleanup_wal_durable_.load() && this->cleanup_main_durable_.load()) {
    this->cleanup_persist_failures_ = 0U;
    this->next_cleanup_persist_retry_ms_ = 0U;
    return;
  }
  this->schedule_cleanup_persist_retry_();
}

bool OpenQuattUsageTelemetry::mark_crash_publish_may_have_reached_broker_() {
  Storage storage{};
  if (!this->load_storage_(&storage) || storage.enabled == 0U || storage.installation_id_present == 0U ||
      storage.installation_id != this->installation_id_bytes_) {
    return false;
  }
  if (storage.reserved[CRASH_PUBLISH_MAY_HAVE_REACHED_RESERVED_INDEX] != 0U) {
    return true;
  }
  storage.reserved[CRASH_PUBLISH_MAY_HAVE_REACHED_RESERVED_INDEX] = 1U;
  storage.reserved[CRASH_ENDPOINT_GENERATION_RESERVED_INDEX] = CRASH_ENDPOINT_GENERATION;
  return this->save_storage_(storage);
}

bool OpenQuattUsageTelemetry::complete_cleanup_wal_() {
  if (!this->discard_pending_crash_()) {
    ESP_LOGE(TAG, "Could not durably discard the pre-opt-out crash snapshot; cleanup remains pending");
    return false;
  }

  // Clear or prove absence of the separate WAL before clearing the legacy
  // main-record marker. A WAL write may have landed even when its earlier sync
  // or readback failed; keeping the main marker intact makes every retry and
  // reboot converge safely.
  CleanupStorage cleared{};
  cleared.magic = CLEANUP_STORAGE_MAGIC;
  cleared.version = CLEANUP_STORAGE_VERSION;
  if (!this->save_cleanup_storage_(cleared)) {
    ESP_LOGE(TAG, "Could not clear retained crash cleanup intent; the tombstone will be retried");
    return false;
  }
  this->cleanup_wal_durable_.store(false);

  Storage storage{};
  if (!this->load_storage_(&storage)) {
    storage.magic = STORAGE_MAGIC;
    storage.version = STORAGE_VERSION;
    storage.enabled = 0U;
    storage.choice_configured = 1U;
    storage.installation_id_present = uuid_is_present_(this->cleanup_installation_id_bytes_) ? 1U : 0U;
    storage.reserved.fill(0U);
    storage.installation_id = this->cleanup_installation_id_bytes_;
  } else {
    storage.enabled = 0U;
    storage.choice_configured = 1U;
  }
  storage.reserved[CLEANUP_REQUIRED_RESERVED_INDEX] = 0U;
  storage.reserved[CRASH_PUBLISH_MAY_HAVE_REACHED_RESERVED_INDEX] = 0U;
  storage.reserved[CRASH_ENDPOINT_GENERATION_RESERVED_INDEX] = 0U;
  if (!this->save_storage_(storage)) {
    ESP_LOGE(TAG, "Could not confirm durable opt-out after retained crash cleanup");
    return false;
  }
  this->cleanup_main_durable_.store(false);

  this->cleanup_pending_.store(false);
  this->cleanup_wal_durable_.store(false);
  this->cleanup_main_durable_.store(false);
  this->cleanup_installation_id_bytes_.fill(0U);
  this->cleanup_installation_id_.clear();
  this->consent_publish_blocked_.store(true);
  this->cleanup_persist_failures_ = 0U;
  this->next_cleanup_persist_retry_ms_ = 0U;
  return true;
}

void OpenQuattUsageTelemetry::apply_pending_cleanup_(const CleanupStorage& cleanup, Storage* storage,
                                                     bool wal_durable) {
  this->cleanup_installation_id_bytes_ = cleanup.installation_id;
  this->cleanup_installation_id_ = format_uuid_(cleanup.installation_id);
  this->cleanup_pending_.store(true);
  this->cleanup_wal_durable_.store(wal_durable);
  if (storage == nullptr) {
    return;
  }
  storage->enabled = 0U;
  storage->choice_configured = 1U;
  storage->reserved[CLEANUP_REQUIRED_RESERVED_INDEX] = 1U;
  if (storage->installation_id_present == 0U || !uuid_is_present_(storage->installation_id)) {
    storage->installation_id_present = 1U;
    storage->installation_id = cleanup.installation_id;
  } else if (storage->installation_id != cleanup.installation_id) {
    ESP_LOGE(TAG, "Retained crash cleanup ID differs from the usage statistics installation ID");
  }
  const bool main_saved = this->save_storage_(*storage);
  this->cleanup_main_durable_.store(main_saved || !wal_durable);
  if (!main_saved) {
    ESP_LOGE(TAG, "Could not repair interrupted opt-out preference; cleanup intent remains authoritative");
  }
  if (!this->cleanup_wal_durable_.load() || !this->cleanup_main_durable_.load()) {
    this->schedule_cleanup_persist_retry_();
  }
}

bool OpenQuattUsageTelemetry::set_consent_publish_blocked_(bool blocked) {
  if (blocked) {
    // Close the gate before waiting for an in-flight start/enqueue critical
    // section. This makes revocation fail closed from the first instruction.
    this->consent_publish_blocked_.store(true);
  }
  if (this->consent_mutex_ == nullptr || xSemaphoreTake(this->consent_mutex_, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  this->consent_publish_blocked_.store(blocked);
  xSemaphoreGive(this->consent_mutex_);
  return true;
}

bool OpenQuattUsageTelemetry::set_setup_complete_gate_(bool setup_complete) {
  if (this->consent_mutex_ == nullptr || xSemaphoreTake(this->consent_mutex_, portMAX_DELAY) != pdTRUE) {
    // A failed transition must never open the data-publish gate.
    this->setup_complete_gate_.store(false);
    return false;
  }
  this->setup_complete_gate_.store(setup_complete);
  xSemaphoreGive(this->consent_mutex_);
  return true;
}

bool OpenQuattUsageTelemetry::ensure_installation_id_(Storage* storage) {
  if (storage == nullptr) {
    return false;
  }
  if (storage->installation_id_present != 0U && uuid_is_present_(storage->installation_id)) {
    return true;
  }

  esp_fill_random(storage->installation_id.data(), storage->installation_id.size());
  storage->installation_id[6] = static_cast<uint8_t>((storage->installation_id[6] & 0x0FU) | 0x40U);
  storage->installation_id[8] = static_cast<uint8_t>((storage->installation_id[8] & 0x3FU) | 0x80U);
  storage->installation_id_present = 1U;
  return uuid_is_present_(storage->installation_id);
}

bool OpenQuattUsageTelemetry::is_setup_complete_() const {
  return this->setup_complete_sensor_ != nullptr && this->setup_complete_sensor_->has_state() &&
         this->setup_complete_sensor_->state;
}

bool OpenQuattUsageTelemetry::apply_storage_(const Storage& storage) {
  if (this->consent_mutex_ == nullptr || xSemaphoreTake(this->consent_mutex_, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  // The ID is immutable across enable/disable writes. Avoid rewriting the string while MQTT callbacks may read it.
  if (this->installation_id_bytes_ != storage.installation_id) {
    this->installation_id_bytes_ = storage.installation_id;
    this->installation_id_ = storage.installation_id_present != 0U ? format_uuid_(storage.installation_id) : "";
  }
  const bool enabled = storage.enabled != 0U && !this->installation_id_.empty();
  const bool choice_configured = storage.choice_configured != 0U;
  this->enabled_.store(enabled);
  this->consent_publish_blocked_.store(!enabled || this->cleanup_pending_.load());
  this->choice_configured_.store(choice_configured);
  xSemaphoreGive(this->consent_mutex_);

  this->publish_state(enabled);
  if (this->installation_id_sensor_ != nullptr) {
    this->installation_id_sensor_->publish_state(this->installation_id_);
  }
  if (this->choice_configured_sensor_ != nullptr) {
    this->choice_configured_sensor_->publish_state(choice_configured);
  }
  return true;
}

void OpenQuattUsageTelemetry::schedule_initial_publish_() {
  // Avoid overlapping the MQTT client and its worker stacks with the first
  // full sensor/API publication wave after connectivity becomes available.
  this->next_publish_ms_ = millis() + INITIAL_PUBLISH_DELAY_MS;
}

void OpenQuattUsageTelemetry::schedule_immediate_publish_() {
  // Defer to the next loop iteration so a runtime opt-in does not start MQTT
  // work inside the switch write callback.
  this->next_publish_ms_ = millis() + 1U;
}

void OpenQuattUsageTelemetry::schedule_regular_publish_() {
  this->next_regular_telemetry_ms_ = millis() + this->interval_ms_;
  this->next_publish_ms_ = this->next_regular_telemetry_ms_;
}

bool OpenQuattUsageTelemetry::regular_telemetry_due_() const {
  return this->next_regular_telemetry_ms_ == 0U || time_reached_(millis(), this->next_regular_telemetry_ms_);
}

void OpenQuattUsageTelemetry::schedule_retry_() {
  this->consecutive_failures_ = std::min<uint8_t>(this->consecutive_failures_ + 1U, 8U);
  uint32_t delay_ms = RETRY_MIN_MS;
  for (uint8_t i = 1; i < this->consecutive_failures_ && delay_ms < RETRY_MAX_MS; i++) {
    delay_ms = std::min<uint32_t>(delay_ms * 2U, RETRY_MAX_MS);
  }
  this->next_publish_ms_ = millis() + delay_ms;
}

void OpenQuattUsageTelemetry::start_publish_session_(PublishKind kind) {
  if (this->session_active_.load() || this->start_task_running_.exchange(true)) {
    return;
  }
  if ((kind != PublishKind::TOMBSTONE && !this->setup_complete_gate_.load()) || !this->session_publish_allowed_(kind)) {
    this->start_task_running_.store(false);
    return;
  }

  this->boot_publish_pending_ = false;
  log_heap_state_("Usage telemetry session begin");
  this->session_client_id_ = kind == PublishKind::TOMBSTONE ? this->cleanup_installation_id_ : this->installation_id_;
  this->clear_session_buffers_();

  bool payload_built = false;
  switch (kind) {
    case PublishKind::TELEMETRY:
      payload_built = this->build_telemetry_payload_();
      break;
    case PublishKind::CRASH:
      payload_built = this->build_crash_payload_();
      if (payload_built && !this->mark_crash_publish_may_have_reached_broker_()) {
        ESP_LOGE(TAG, "Could not durably mark the retained crash publish; blocking that MQTT session");
        payload_built = false;
      }
      if (!payload_built && this->regular_telemetry_due_() && this->session_publish_allowed_(PublishKind::TELEMETRY)) {
        ESP_LOGW(TAG, "Crash telemetry is temporarily unavailable; publishing normal usage statistics first");
        kind = PublishKind::TELEMETRY;
        payload_built = this->build_telemetry_payload_();
      }
      break;
    case PublishKind::TOMBSTONE:
      payload_built = this->build_tombstone_payload_();
      break;
  }
  this->active_publish_kind_.store(kind);
  if (!this->build_publish_topic_(kind, this->session_client_id_) || !payload_built) {
    this->clear_session_buffers_();
    this->start_task_running_.store(false);
    this->schedule_retry_();
    return;
  }

  // The same worker owns both client startup and teardown. On S3 its stack is
  // persistent in PSRAM; classic ESP32 uses a per-session internal stack.
  if (!this->ensure_worker_task_()) {
    this->clear_session_buffers_();
    this->start_task_running_.store(false);
    this->schedule_retry_();
    return;
  }
  log_heap_state_("Usage telemetry worker ready");
  this->publish_succeeded_.store(false);
  this->publish_failed_.store(false);
  this->pending_message_id_.store(-1);
  this->finishing_session_.store(false);
  this->start_task_complete_.store(false);
  this->cleanup_task_complete_.store(false);
  this->cleanup_succeeded_.store(false);
  this->mqtt_connected_seen_.store(false);
  this->mqtt_disconnected_seen_.store(false);
  this->cleanup_stop_failures_ = 0U;
  this->cleanup_disconnect_requested_ = false;
  this->session_started_ms_ = millis();
  this->session_active_.store(true);

  if (!this->notify_worker_(WorkerCommand::START)) {
    this->session_active_.store(false);
    this->start_task_running_.store(false);
    this->clear_session_buffers_();
    this->session_client_id_.clear();
    this->schedule_retry_();
    ESP_LOGE(TAG, "Failed to notify usage telemetry MQTT worker");
  }
}

bool OpenQuattUsageTelemetry::ensure_worker_task_() {
  if (this->worker_task_state_.is_created()) {
    return this->worker_task_region_valid_;
  }
  this->worker_task_region_valid_ = false;
  if (!this->worker_task_state_.create(&OpenQuattUsageTelemetry::worker_task_, "oq_usage_mqtt",
                                       MQTT_WORKER_TASK_STACK_SIZE, this, 4, MQTT_WORKER_STACK_IN_PSRAM)) {
    ESP_LOGE(TAG, "Failed to create %u-byte usage telemetry worker in %s",
             static_cast<unsigned>(MQTT_WORKER_TASK_STACK_SIZE), MQTT_WORKER_STACK_IN_PSRAM ? "PSRAM" : "internal RAM");
    return false;
  }

  const bool stack_is_external = esp_ptr_external_ram(pxTaskGetStackStart(this->worker_task_state_.get_handle()));
  if (stack_is_external != MQTT_WORKER_STACK_IN_PSRAM) {
    ESP_LOGE(TAG,
             "Usage telemetry worker stack was allocated in the wrong "
             "memory region; worker remains parked");
    // StaticTask owns a static stack. It must not free that stack while the
    // freshly created task may still be entering its notification wait on the
    // other core. Treat this impossible allocator-contract violation as a
    // permanent telemetry failure and leave the parked task intact.
    this->mark_failed();
    return false;
  }
  this->worker_task_region_valid_ = true;
  ESP_LOGD(TAG, "Usage telemetry worker stack: %u bytes in %s", static_cast<unsigned>(MQTT_WORKER_TASK_STACK_SIZE),
           stack_is_external ? "PSRAM" : "internal RAM");
  return true;
}

bool OpenQuattUsageTelemetry::notify_worker_(WorkerCommand command) {
  const TaskHandle_t handle = this->worker_task_state_.get_handle();
  if (handle == nullptr) return false;
  return xTaskNotify(handle, static_cast<uint32_t>(command), eSetValueWithOverwrite) == pdPASS;
}

bool OpenQuattUsageTelemetry::start_client_() {
  const PublishKind kind = this->active_publish_kind_.load();
  if (!this->session_active_.load() || !this->is_configured() || this->mqtt_client_ != nullptr ||
      !this->session_publish_allowed_(kind) || this->session_client_id_.empty()) {
    return false;
  }

  esp_mqtt_client_config_t mqtt_config{};
  mqtt_config.broker.address.hostname = this->broker_.c_str();
  mqtt_config.broker.address.port = this->port_;
  mqtt_config.broker.address.transport = this->tls_ ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
  if (this->tls_) {
    mqtt_config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
  }
  mqtt_config.credentials.client_id = this->session_client_id_.c_str();
  mqtt_config.session.keepalive = 30;
  mqtt_config.session.disable_clean_session = false;
  mqtt_config.network.timeout_ms = 10000;
  mqtt_config.network.disable_auto_reconnect = true;
  mqtt_config.task.stack_size = MQTT_TASK_STACK_SIZE;
  mqtt_config.buffer.size = 1024;
  mqtt_config.buffer.out_size = 1024;
  mqtt_config.outbox.limit = 2048;
  if (!this->username_.empty()) {
    mqtt_config.credentials.username = this->username_.c_str();
  }
  if (!this->password_.empty()) {
    mqtt_config.credentials.authentication.password = this->password_.c_str();
  }

  if (this->consent_mutex_ == nullptr || xSemaphoreTake(this->consent_mutex_, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  if (!this->session_active_.load() || !this->session_publish_allowed_(kind)) {
    xSemaphoreGive(this->consent_mutex_);
    return false;
  }

  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);
  if (client == nullptr) {
    xSemaphoreGive(this->consent_mutex_);
    ESP_LOGW(TAG, "Failed to initialize usage telemetry MQTT client");
    return false;
  }

  if (!this->session_active_.load() || !this->session_publish_allowed_(kind)) {
    xSemaphoreGive(this->consent_mutex_);
    esp_mqtt_client_destroy(client);
    return false;
  }
  this->mqtt_client_ = client;
  this->mqtt_client_started_ = false;

  esp_err_t error =
      esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, &OpenQuattUsageTelemetry::mqtt_event_handler_, this);
  if (error == ESP_OK) {
    error = esp_mqtt_client_start(client);
  }
  xSemaphoreGive(this->consent_mutex_);
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "Failed to start usage telemetry MQTT client: %s", esp_err_to_name(error));
    esp_mqtt_client_destroy(client);
    this->mqtt_client_ = nullptr;
    return false;
  }
  this->mqtt_client_started_ = true;
  return true;
}

bool OpenQuattUsageTelemetry::cleanup_client_() {
  esp_mqtt_client_handle_t client = this->mqtt_client_;
  if (client == nullptr) return true;

  if (this->mqtt_client_started_) {
    const esp_err_t error = esp_mqtt_client_stop(client);
    if (error != ESP_OK && error != ESP_FAIL) {
      ESP_LOGE(TAG, "Unexpected usage telemetry MQTT stop error: %s", esp_err_to_name(error));
      return false;
    }
    if (error != ESP_OK) {
      ++this->cleanup_stop_failures_;
      const MqttCleanupDecision decision = mqtt_cleanup_decision(
          false, this->mqtt_connected_seen_.load(), this->mqtt_disconnected_seen_.load(), this->cleanup_stop_failures_);
      if (decision == MqttCleanupDecision::FORCE_DISCONNECT) {
        // A connected client may fail to construct its graceful DISCONNECT
        // packet under memory pressure. Its own task handles DISCONNECT_BIT by
        // aborting the transport without allocating that packet.
        if (!this->cleanup_disconnect_requested_) {
          ESP_LOGW(TAG,
                   "Usage telemetry MQTT stop failed (%s); forcing "
                   "disconnect before retry",
                   esp_err_to_name(error));
        }
        esp_mqtt_client_disconnect(client);
        this->cleanup_disconnect_requested_ = true;
        return false;
      }

      // ESP-IDF also returns ESP_FAIL when the mqtt task has already stopped
      // itself (for example after a transport-allocation failure). Allow one
      // full worker interval for its STOPPED tail to finish before destroy.
      if (decision == MqttCleanupDecision::RETRY_STOP) {
        ESP_LOGD(TAG,
                 "Usage telemetry MQTT task may already be stopped; "
                 "verifying before destroy");
        return false;
      }
      ESP_LOGW(TAG,
               "Usage telemetry MQTT task stopped before cleanup; "
               "releasing its client resources");
    }
  }

  const esp_err_t error = esp_mqtt_client_destroy(client);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Failed to destroy usage telemetry MQTT client: %s", esp_err_to_name(error));
    return false;
  }
  this->mqtt_client_ = nullptr;
  this->mqtt_client_started_ = false;
  this->cleanup_stop_failures_ = 0U;
  this->cleanup_disconnect_requested_ = false;
  return true;
}

void OpenQuattUsageTelemetry::finish_publish_session_(bool succeeded) {
  if (!this->session_active_.load() || this->start_task_running_.load() || this->finishing_session_.exchange(true)) {
    return;
  }
  this->cleanup_succeeded_.store(succeeded);
  if (!this->notify_worker_(WorkerCommand::CLEANUP)) {
    this->finishing_session_.store(false);
    this->publish_failed_.store(true);
    ESP_LOGE(TAG, "Failed to notify usage telemetry MQTT cleanup");
  }
}

void OpenQuattUsageTelemetry::complete_publish_session_() {
  const PublishKind kind = this->active_publish_kind_.load();
  bool succeeded = this->cleanup_succeeded_.load();

  if (succeeded && kind == PublishKind::CRASH) {
    if (!this->enabled_.load() || this->cleanup_pending_.load() || this->crash_provider_ == nullptr ||
        !this->crash_provider_->acknowledge_pending_crash(this->active_crash_id_)) {
      succeeded = false;
    }
  } else if (succeeded && kind == PublishKind::TOMBSTONE && !this->complete_cleanup_wal_()) {
    succeeded = false;
  }

  this->session_active_.store(false);
  this->publish_succeeded_.store(false);
  this->publish_failed_.store(false);
  this->pending_message_id_.store(-1);
  this->finishing_session_.store(false);
  this->cleanup_succeeded_.store(false);
  this->clear_session_buffers_();
  this->session_client_id_.clear();

  if (this->cleanup_pending_.load()) {
    if (kind != PublishKind::TOMBSTONE) {
      this->schedule_immediate_publish_();
    } else {
      this->schedule_retry_();
    }
    this->payload_message_id_.clear();
    return;
  }
  if (!this->enabled_.load()) {
    this->payload_message_id_.clear();
    this->next_publish_ms_ = 0U;
    this->consecutive_failures_ = 0U;
    return;
  }
  if (succeeded) {
    if (kind == PublishKind::TELEMETRY) {
      this->payload_message_id_.clear();
      this->next_regular_telemetry_ms_ = millis() + this->interval_ms_;
    }
    this->active_crash_id_.fill(0U);
    this->consecutive_failures_ = 0U;
    if (kind == PublishKind::TELEMETRY && this->crash_provider_ != nullptr &&
        this->crash_provider_->has_pending_crash()) {
      this->schedule_retry_();
    } else {
      this->schedule_regular_publish_();
    }
    ESP_LOGD(TAG, "%s published successfully", kind == PublishKind::CRASH ? "Crash telemetry" : "Usage statistics");
  } else {
    // Crash snapshots and their IDs remain durable in the provider. Normal
    // telemetry retains its logical message ID for an idempotent QoS 1 retry.
    this->schedule_retry_();
    ESP_LOGW(TAG, "%s publish failed; a bounded retry was scheduled",
             kind == PublishKind::CRASH ? "Crash telemetry" : "Usage statistics");
  }
}

bool OpenQuattUsageTelemetry::build_publish_topic_(PublishKind kind, const std::string& installation_id) {
  const char* const suffix = kind == PublishKind::TELEMETRY ? "/telemetry" : "/crash";
  const size_t publish_topic_size = this->topic_.size() + 1U + installation_id.size() + std::strlen(suffix);
  if (!this->publish_topic_.allocate_external(publish_topic_size + 1U)) {
    ESP_LOGE(TAG, "Failed to allocate %u-byte usage telemetry topic in PSRAM",
             static_cast<unsigned>(publish_topic_size + 1U));
    return false;
  }
  FixedBufferWriter publish_topic(this->publish_topic_.data(), this->publish_topic_.size());
  publish_topic += this->topic_;
  publish_topic += '/';
  publish_topic += installation_id;
  publish_topic += suffix;
  if (!publish_topic.ok()) {
    ESP_LOGE(TAG, "Usage telemetry topic exceeded its PSRAM buffer");
    this->publish_topic_.release();
    return false;
  }
  return true;
}

bool OpenQuattUsageTelemetry::build_telemetry_payload_() {
  const uint64_t uptime_s = static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;
  const std::string hardware_revision = this->read_hardware_revision_();
  if (this->payload_message_id_.empty()) {
    this->payload_message_id_ = random_message_id_();
  }

  this->clear_payload_();
  if (!this->payload_.allocate_external(TELEMETRY_PAYLOAD_CAPACITY + 1U)) {
    ESP_LOGE(TAG, "Failed to allocate %u-byte usage telemetry payload in PSRAM",
             static_cast<unsigned>(TELEMETRY_PAYLOAD_CAPACITY + 1U));
    return false;
  }
  FixedBufferWriter payload(this->payload_.data(), this->payload_.size());
  payload += R"({"schema_version":1,"message_id":")";
  payload += this->payload_message_id_;
  payload += R"(","installation_id":")";
  payload += this->installation_id_;
  payload += '"';
  append_json_key_(payload, "timestamp_s");
  if (this->clock_ == nullptr) {
    payload += "null";
  } else {
    const auto now = this->clock_->now();
    if (now.is_valid()) {
      payload.append_uint(static_cast<uint64_t>(now.timestamp));
    } else {
      payload += "null";
    }
  }
  payload += R"(,"uptime_s":)";
  payload.append_uint(uptime_s);
  payload += R"(,"firmware_version":")";
  append_json_escaped(payload, this->firmware_version_);
  payload += R"(","release_channel":")";
  append_json_escaped(payload, this->release_channel_);
  payload += R"(","hardware_profile":")";
  append_json_escaped(payload, this->hardware_profile_);
  payload += R"(","hardware_revision":)";
  if (hardware_revision.empty()) {
    payload += "null";
  } else {
    payload += '"';
    payload += hardware_revision;
    payload += '"';
  }
  payload += R"(,"topology":")";
  append_json_escaped(payload, this->topology_);
  payload += R"(","connection":")";
  append_json_escaped(payload, this->connection_);
  payload += '"';
  append_json_optional_select_(payload, "quatt_hybrid_generation_config", this->quatt_hybrid_generation_select_,
                               quatt_hybrid_generation_wire_value);
  append_json_flow_source_(payload, this->flow_source_select_, this->q_flow_source_select_);
  append_json_optional_select_(payload, "heating_strategy", this->heating_strategy_select_,
                               heating_strategy_wire_value);
  append_json_optional_select_(payload, "room_temperature_source", this->room_temperature_source_select_,
                               configured_source_wire_value);
  append_json_optional_select_(payload, "room_setpoint_source", this->room_setpoint_source_select_,
                               configured_source_wire_value);
  append_json_optional_select_(payload, "outside_temperature_source", this->outside_temperature_source_select_,
                               configured_source_wire_value);
  append_json_optional_select_(payload, "heating_enable_source", this->heating_enable_source_select_,
                               configured_source_wire_value);
  append_json_optional_select_(payload, "cooling_enable_source", this->cooling_enable_source_select_,
                               configured_source_wire_value);
  append_json_optional_select_(payload, "cooling_dew_point_source", this->cooling_dew_point_source_select_,
                               configured_source_wire_value);
  append_json_uint_(payload, "heap_free_b", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  append_json_uint_(payload, "heap_min_free_b", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  append_json_uint_(payload, "heap_largest_block_b", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  append_json_uint_(payload, "psram_free_b", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  append_json_optional_number_(payload, "loop_time_ms", this->loop_time_sensor_, 0);
  append_json_optional_number_(payload, "esp_internal_temp_c", this->internal_temperature_sensor_, 1);
  append_json_optional_number_(payload, "wifi_rssi_dbm", this->wifi_signal_sensor_, 1);
  append_json_key_(payload, "reset_reason");
  payload += '"';
  payload += reset_reason_name_(esp_reset_reason());
  payload += '"';
  append_json_optional_bool_(payload, "cic_polling_enabled", this->cic_polling_switch_);
  append_json_optional_bool_(payload, "cic_compatibility_enabled", this->cic_compatibility_switch_);
  append_json_optional_bool_(payload, "ot_thermostat_enabled", this->ot_thermostat_switch_);
  append_json_optional_bool_(payload, "boiler_assist_enabled", this->boiler_assist_switch_);
  append_json_boiler_connection_(payload, this->boiler_connection_select_);
  append_json_optional_bool_(payload, "mqtt_inputs_enabled", this->mqtt_config_ != nullptr,
                             this->mqtt_config_ != nullptr && this->mqtt_config_->is_enabled());
  append_json_optional_bool_(payload, "trend_ram_enabled", this->trend_ram_switch_);
  append_json_optional_bool_(payload, "trend_flash_enabled", this->trend_flash_switch_);
  append_json_optional_bool_(payload, "decision_log_flash_enabled", this->decision_log_flash_switch_);
  append_json_optional_bool_(payload, "energy_history_flash_enabled", this->energy_history_flash_switch_);
  append_json_optional_bool_(payload, "ram_log_history_enabled", this->ram_log_history_switch_);
  payload += '}';

  if (!payload.ok()) {
    ESP_LOGE(TAG, "Usage telemetry JSON exceeded its %u-byte PSRAM buffer",
             static_cast<unsigned>(TELEMETRY_PAYLOAD_CAPACITY));
    this->clear_payload_();
    return false;
  }
  this->payload_size_ = payload.size();
  return true;
}

bool OpenQuattUsageTelemetry::build_crash_payload_() {
  using namespace openquatt_log_history;
  if (this->crash_provider_ == nullptr) {
    return false;
  }
  if (!this->crash_snapshot_ && !this->crash_snapshot_.allocate_external(1U)) {
    ESP_LOGE(TAG, "Failed to allocate crash snapshot scratch space in PSRAM");
    return false;
  }
  CrashSnapshot* const snapshot = this->crash_snapshot_.data();
  if (!this->crash_provider_->copy_pending_crash(snapshot) || !crash_snapshot_is_pending(*snapshot)) {
    ESP_LOGW(TAG, "Pending crash snapshot became unavailable before serialization");
    return false;
  }
  this->active_crash_id_ = snapshot->crash_id;
  const std::string crash_id = OpenQuattLogHistory::format_crash_id(snapshot->crash_id);
  const CrashBuildIdentity& captured = snapshot->captured_build;
  char current_elf_sha256[CRASH_ELF_SHA256_HEX_LENGTH + 1U]{};
  esp_app_get_elf_sha256(current_elf_sha256, sizeof(current_elf_sha256));
  const bool current_build_id_valid = fixed_hex_string_is_valid_(current_elf_sha256, CRASH_ELF_SHA256_HEX_LENGTH);

  this->clear_payload_();
  if (!this->payload_.allocate_external(CRASH_PAYLOAD_CAPACITY + 1U)) {
    ESP_LOGE(TAG, "Failed to allocate %u-byte crash telemetry payload in PSRAM",
             static_cast<unsigned>(CRASH_PAYLOAD_CAPACITY + 1U));
    return false;
  }
  FixedBufferWriter payload(this->payload_.data(), this->payload_.size());
  payload += R"({"schema_version":1,"message_id":")";
  payload += crash_id;
  payload += R"(","installation_id":")";
  payload += this->installation_id_;
  payload += R"(","event":"crash")";
  append_json_optional_uint64_(payload, "timestamp_s", snapshot->timestamp_s,
                               (snapshot->flags & CRASH_SNAPSHOT_TIMESTAMP_VALID) != 0U);
  append_json_uint_(payload, "uptime_s", snapshot->uptime_s);
  append_json_optional_string_(payload, "firmware_version", this->firmware_version_);
  append_json_optional_string_(payload, "release_channel", this->release_channel_);
  append_json_optional_fixed_string_(payload, "current_build_id", current_elf_sha256, current_build_id_valid);
  append_json_optional_string_(payload, "hardware_profile", this->hardware_profile_);
  append_json_optional_string_(payload, "topology", this->topology_);
  append_json_optional_string_(payload, "connection", this->connection_);
  append_json_key_(payload, "reset_reason");
  payload += '"';
  payload += reset_reason_name_(static_cast<esp_reset_reason_t>(snapshot->reset_reason));
  payload += '"';

  payload += R"(,"crash":{"captured_build_id":)";
  const bool captured_build_id_valid = (captured.flags & CRASH_BUILD_IDENTITY_ELF_SHA256_VALID) != 0U &&
                                       fixed_hex_string_is_valid_(captured.elf_sha256, CRASH_ELF_SHA256_HEX_LENGTH);
  append_json_optional_fixed_value_(payload, captured.elf_sha256, captured_build_id_valid);
  append_json_optional_fixed_string_(payload, "captured_source_repository", captured.source_repository,
                                     (captured.flags & CRASH_BUILD_IDENTITY_SOURCE_REPOSITORY_VALID) != 0U);
  append_json_optional_fixed_string_(payload, "captured_source_commit", captured.source_commit,
                                     (captured.flags & CRASH_BUILD_IDENTITY_SOURCE_COMMIT_VALID) != 0U);
  append_json_optional_uint64_(payload, "captured_build_epoch", captured.build_epoch,
                               (captured.flags & CRASH_BUILD_IDENTITY_BUILD_EPOCH_VALID) != 0U);
  append_json_optional_fixed_string_(payload, "captured_build_target", captured.build_target,
                                     (captured.flags & CRASH_BUILD_IDENTITY_BUILD_TARGET_VALID) != 0U);
  append_json_optional_fixed_string_(payload, "captured_firmware_version", captured.firmware_version,
                                     (captured.flags & CRASH_BUILD_IDENTITY_FIRMWARE_VERSION_VALID) != 0U);
  append_json_optional_fixed_string_(payload, "captured_release_channel", captured.release_channel,
                                     (captured.flags & CRASH_BUILD_IDENTITY_RELEASE_CHANNEL_VALID) != 0U);
  append_json_key_(payload, "captured_by_current_build");
  const bool captured_by_current_build =
      current_build_id_valid && captured_build_id_valid && std::strcmp(current_elf_sha256, captured.elf_sha256) == 0;
  payload += captured_by_current_build ? "true" : "false";
  append_json_optional_fixed_string_(payload, "exception_type", snapshot->exception_type_name,
                                     snapshot->exception_type_name[0] != '\0');
  append_json_optional_fixed_string_(payload, "reason", snapshot->reason, snapshot->reason[0] != '\0');
  append_json_key_(payload, "raw_cause");
  if ((snapshot->flags & CRASH_SNAPSHOT_RAW_CAUSE_VALID) != 0U) {
    payload.append_uint(snapshot->raw_cause);
  } else {
    payload += "null";
  }
  append_json_uint_(payload, "core", snapshot->crashed_core);
  append_json_address_(payload, "pc", snapshot->pc, snapshot->pc != 0U);
  append_json_address_(payload, "fault_addr", snapshot->fault_addr,
                       (snapshot->flags & CRASH_SNAPSHOT_FAULT_ADDR_VALID) != 0U);
  append_json_backtrace_(payload, "backtrace", snapshot->crashed_core_backtrace,
                         snapshot->crashed_core_backtrace.count != 0U, CRASH_TELEMETRY_BACKTRACE_CAPACITY);
  append_json_backtrace_(payload, "other_core_backtrace", snapshot->other_core_backtrace,
                         (snapshot->flags & CRASH_SNAPSHOT_OTHER_CORE_BACKTRACE_VALID) != 0U,
                         CRASH_TELEMETRY_BACKTRACE_CAPACITY);
  append_json_key_(payload, "backtrace_truncated");
  const bool backtrace_truncated = snapshot->crashed_core_backtrace.count > CRASH_TELEMETRY_BACKTRACE_CAPACITY ||
                                   snapshot->other_core_backtrace.count > CRASH_TELEMETRY_BACKTRACE_CAPACITY;
  payload += backtrace_truncated ? "true" : "false";
  payload += "}}";

  if (!payload.ok()) {
    ESP_LOGE(TAG, "Crash telemetry JSON exceeded its %u-byte PSRAM buffer",
             static_cast<unsigned>(CRASH_PAYLOAD_CAPACITY));
    this->clear_payload_();
    return false;
  }
  this->payload_size_ = payload.size();
  return true;
}

bool OpenQuattUsageTelemetry::build_tombstone_payload_() {
  this->clear_payload_();
  return this->cleanup_pending_.load() && !this->cleanup_installation_id_.empty();
}

bool OpenQuattUsageTelemetry::discard_pending_crash_() {
  if (this->crash_provider_ == nullptr || !this->crash_provider_->has_pending_crash()) {
    return true;
  }
  if (!this->crash_snapshot_ && !this->crash_snapshot_.allocate_external(1U)) {
    return false;
  }
  openquatt_log_history::CrashSnapshot* const snapshot = this->crash_snapshot_.data();
  return this->crash_provider_->copy_pending_crash(snapshot) &&
         this->crash_provider_->discard_pending_crash(snapshot->crash_id);
}

bool OpenQuattUsageTelemetry::session_publish_allowed_(PublishKind kind) const {
  if (kind == PublishKind::TOMBSTONE && this->cleanup_installation_id_.empty()) {
    return false;
  }
  return data_publish_allowed(kind, this->enabled_.load(), this->setup_complete_gate_.load(),
                              this->consent_publish_blocked_.load(), this->cleanup_pending_.load());
}

void OpenQuattUsageTelemetry::clear_session_buffers_() {
  this->publish_topic_.release();
  this->clear_payload_();
}

void OpenQuattUsageTelemetry::clear_payload_() {
  this->payload_.release();
  this->payload_size_ = 0U;
}

std::string OpenQuattUsageTelemetry::read_hardware_revision_() const {
  if (this->hardware_profile_ != "heatpump_controller_q") {
    return "";
  }
#if defined(OPENQUATT_HAS_Q_HARDWARE_REVISION)
  const auto revision = oq_hardware::read_hardware_revision_efuse();
  if (revision.error != ESP_OK || !revision.programmed) {
    return "";
  }
  char revision_text[32];
  std::snprintf(revision_text, sizeof(revision_text), "%u.%u (batch %u)", static_cast<unsigned>(revision.major),
                static_cast<unsigned>(revision.minor), static_cast<unsigned>(revision.batch));
  return revision_text;
#else
  return "";
#endif
}

bool OpenQuattUsageTelemetry::time_reached_(uint32_t now_ms, uint32_t target_ms) {
  return static_cast<int32_t>(now_ms - target_ms) >= 0;
}

std::string OpenQuattUsageTelemetry::format_uuid_(const std::array<uint8_t, 16>& bytes) {
  char uuid[37];
  std::snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0],
                bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
                bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return uuid;
}

std::string OpenQuattUsageTelemetry::random_message_id_() {
  std::array<uint8_t, 16> bytes{};
  esp_fill_random(bytes.data(), bytes.size());
  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x40U);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);
  return format_uuid_(bytes);
}

void OpenQuattUsageTelemetry::worker_task_(void* arg) {
  auto* self = static_cast<OpenQuattUsageTelemetry*>(arg);
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
        self->publish_failed_.store(true);
      }
      self->start_task_complete_.store(true);
      log_heap_state_("Usage telemetry MQTT start complete");
      ESP_LOGD(TAG, "Usage telemetry worker stack free after start: %u bytes",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
      App.wake_loop_threadsafe();
      continue;
    }

    if (command == WorkerCommand::CLEANUP) {
      while (!self->cleanup_client_()) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
      }
      ESP_LOGD(TAG, "Usage telemetry worker stack free after cleanup: %u bytes",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
      log_heap_state_("Usage telemetry MQTT cleanup complete");
      self->cleanup_task_complete_.store(true);
      App.wake_loop_threadsafe();
      if (!MQTT_WORKER_STACK_IN_PSRAM) {
        vTaskSuspend(nullptr);
      }
      continue;
    }

    ESP_LOGE(TAG, "Usage telemetry worker received invalid command: %u", static_cast<unsigned>(command_value));
  }
}

void OpenQuattUsageTelemetry::mqtt_event_handler_(void* handler_args, esp_event_base_t base, int32_t event_id,
                                                  void* event_data) {
  (void)base;
  auto* self = static_cast<OpenQuattUsageTelemetry*>(handler_args);
  auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
  if (self == nullptr || event == nullptr) {
    return;
  }
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
      if (self->consent_mutex_ == nullptr || xSemaphoreTake(self->consent_mutex_, portMAX_DELAY) != pdTRUE) {
        self->publish_failed_.store(true);
        break;
      }
      int message_id = -1;
      const PublishKind kind = self->active_publish_kind_.load();
      if (self->session_active_.load() && !self->finishing_session_.load() && self->session_publish_allowed_(kind)) {
        const MqttPublishPolicy policy = mqtt_publish_policy(kind);
        static const char EMPTY_PAYLOAD[] = "";
        const char* const payload = policy.empty_payload ? EMPTY_PAYLOAD : self->payload_.data();
        const size_t payload_size = policy.empty_payload ? 0U : self->payload_size_;
        message_id = esp_mqtt_client_enqueue(event->client, self->publish_topic_.data(), payload,
                                             static_cast<int>(payload_size), policy.qos, policy.retain, true);
      }
      xSemaphoreGive(self->consent_mutex_);
      ESP_LOGD(TAG, "esp-mqtt task stack free after enqueue: %u bytes",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
      if (message_id < 0) {
        self->publish_failed_.store(true);
      } else {
        self->pending_message_id_.store(message_id);
      }
      App.wake_loop_threadsafe();
      break;
    }
    case MQTT_EVENT_PUBLISHED:
      if (event->msg_id == self->pending_message_id_.load()) {
        ESP_LOGD(TAG, "esp-mqtt task stack free after publish: %u bytes",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        self->publish_succeeded_.store(true);
        App.wake_loop_threadsafe();
      }
      break;
    case MQTT_EVENT_ERROR:
      self->publish_failed_.store(true);
      App.wake_loop_threadsafe();
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (!self->publish_succeeded_.load()) {
        self->publish_failed_.store(true);
        App.wake_loop_threadsafe();
      }
      break;
    default:
      break;
  }
}

}  // namespace openquatt_usage_telemetry
}  // namespace esphome
