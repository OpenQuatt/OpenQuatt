#include "OpenQuattCrashTelemetry.h"
#include "OpenQuattCrashTelemetryAnsi.h"
#include "OpenQuattCrashTelemetryHelpers.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esphome/components/esp32/crash_handler.h"
#include "esphome/components/logger/logger.h"
#include "esphome/core/build_info_data.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

namespace esphome::openquatt_crash_telemetry {
namespace {

static const char* const TAG = "openquatt.crash_telemetry";
static const uint32_t CRASH_STATE_STORAGE_KEY = fnv1_hash("openquatt_crash_telemetry_state");
static const char* const FLASH_PARTITION_LABEL = "openquatt_data";

using detail::valid_installation_id;

}  // namespace

float OpenQuattCrashTelemetry::get_setup_priority() const {
  // Capture the ESPHome crash replay before OpenQuattLogHistory clears its marker.
  return setup_priority::WIFI + 10.0f;
}

uint32_t OpenQuattCrashTelemetry::checksum_(const void* data, size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 2166136261UL;
  for (size_t index = 0U; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

bool OpenQuattCrashTelemetry::copy_text_(char* destination, size_t destination_size, const std::string& source) {
  if (source.size() >= destination_size) return false;
  std::memcpy(destination, source.data(), source.size());
  destination[source.size()] = '\0';
  return true;
}

bool OpenQuattCrashTelemetry::copy_text_(char* destination, size_t destination_size, const char* source) {
  if (source == nullptr) return false;
  const size_t length = std::strlen(source);
  if (length >= destination_size) return false;
  std::memcpy(destination, source, length + 1U);
  return true;
}

bool OpenQuattCrashTelemetry::valid_record_(const CrashRecord& record) {
  return record.magic == CRASH_RECORD_MAGIC && record.version == CRASH_RECORD_VERSION && record.pending <= 1U &&
         record.truncated <= 1U && record.captured_by_reporting_build <= 1U && record.sequence != 0U &&
         record.report_length < CRASH_REPORT_CAPACITY && record.report[record.report_length] == '\0' &&
         record.checksum == checksum_(&record, offsetof(CrashRecord, checksum));
}

void OpenQuattCrashTelemetry::random_uuid_(char* destination, size_t destination_size) {
  if (destination == nullptr || destination_size < 37U) return;
  std::array<uint8_t, 16U> bytes{};
  esp_fill_random(bytes.data(), bytes.size());
  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x40U);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);
  std::snprintf(destination, destination_size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9],
                bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

const char* OpenQuattCrashTelemetry::extract_message_body_(const char* message) {
  if (message == nullptr) return "";
  const char* first_close = std::strchr(message, ']');
  if (first_close == nullptr) return message;
  const char* tag_open = std::strchr(first_close + 1, '[');
  if (tag_open == nullptr) return message;
  const char* tag_close = std::strchr(tag_open + 1, ']');
  if (tag_close == nullptr) return message;
  const char* separator = std::strstr(tag_close + 1, ": ");
  return separator == nullptr ? tag_close + 1 : separator + 2;
}

bool OpenQuattCrashTelemetry::load_record_() {
  if (!this->record_ || this->flash_partition_ == nullptr) return false;
  bool found = false;
  uint32_t newest_sequence = 0U;
  uint8_t newest_slot = 0U;
  for (uint8_t slot = 0U; slot < openquatt_common::OpenQuattFlashLayout::CRASH_TELEMETRY_SECTOR_COUNT; ++slot) {
    const size_t offset = openquatt_common::OpenQuattFlashLayout::CRASH_TELEMETRY_OFFSET +
                          (static_cast<size_t>(slot) * openquatt_common::OpenQuattFlashLayout::SECTOR_SIZE);
    if (esp_partition_read(this->flash_partition_, offset, this->record_.data(), sizeof(CrashRecord)) != ESP_OK ||
        !valid_record_(*this->record_.data())) {
      continue;
    }
    if (!found || flash_sequence_is_newer(this->record_.data()->sequence, newest_sequence)) {
      found = true;
      newest_sequence = this->record_.data()->sequence;
      newest_slot = slot;
    }
  }
  if (!found) {
    std::memset(this->record_.data(), 0, sizeof(CrashRecord));
    this->active_record_slot_ = -1;
    return false;
  }
  const size_t newest_offset = openquatt_common::OpenQuattFlashLayout::CRASH_TELEMETRY_OFFSET +
                               (static_cast<size_t>(newest_slot) * openquatt_common::OpenQuattFlashLayout::SECTOR_SIZE);
  if (esp_partition_read(this->flash_partition_, newest_offset, this->record_.data(), sizeof(CrashRecord)) != ESP_OK ||
      !valid_record_(*this->record_.data())) {
    std::memset(this->record_.data(), 0, sizeof(CrashRecord));
    this->active_record_slot_ = -1;
    return false;
  }
  this->active_record_slot_ = static_cast<int8_t>(newest_slot);
  this->record_loaded_ = true;
  return this->record_.data()->pending != 0U;
}

bool OpenQuattCrashTelemetry::save_record_() {
  if (!this->record_ || this->flash_partition_ == nullptr) return false;
  CrashRecord* record = this->record_.data();
  const uint32_t previous_sequence = record->sequence;
  record->sequence++;
  if (record->sequence == 0U) record->sequence = 1U;
  record->magic = CRASH_RECORD_MAGIC;
  record->version = CRASH_RECORD_VERSION;
  record->checksum = 0U;
  record->checksum = checksum_(record, offsetof(CrashRecord, checksum));
  const int8_t previous_slot = this->active_record_slot_;
  const uint8_t target_slot =
      this->active_record_slot_ < 0
          ? 0U
          : static_cast<uint8_t>((this->active_record_slot_ + 1) %
                                 openquatt_common::OpenQuattFlashLayout::CRASH_TELEMETRY_SECTOR_COUNT);
  const size_t target_offset = openquatt_common::OpenQuattFlashLayout::CRASH_TELEMETRY_OFFSET +
                               (static_cast<size_t>(target_slot) * openquatt_common::OpenQuattFlashLayout::SECTOR_SIZE);
  bool saved = esp_partition_erase_range(this->flash_partition_, target_offset,
                                         openquatt_common::OpenQuattFlashLayout::SECTOR_SIZE) == ESP_OK &&
               esp_partition_write(this->flash_partition_, target_offset, record, sizeof(CrashRecord)) == ESP_OK;
  std::array<uint8_t, 64U> verify_buffer{};
  const auto* expected = reinterpret_cast<const uint8_t*>(record);
  for (size_t verified = 0U; saved && verified < sizeof(CrashRecord); verified += verify_buffer.size()) {
    const size_t chunk_size = std::min(verify_buffer.size(), sizeof(CrashRecord) - verified);
    saved = esp_partition_read(this->flash_partition_, target_offset + verified, verify_buffer.data(), chunk_size) ==
                ESP_OK &&
            std::memcmp(verify_buffer.data(), expected + verified, chunk_size) == 0;
  }
  if (!saved) {
    record->sequence = previous_sequence;
    record->checksum = 0U;
    record->checksum = checksum_(record, offsetof(CrashRecord, checksum));
    return false;
  }
  this->active_record_slot_ = static_cast<int8_t>(target_slot);
  this->record_loaded_ = true;
  if (record->pending == 0U && previous_slot >= 0) {
    const size_t previous_offset =
        openquatt_common::OpenQuattFlashLayout::CRASH_TELEMETRY_OFFSET +
        (static_cast<size_t>(previous_slot) * openquatt_common::OpenQuattFlashLayout::SECTOR_SIZE);
    if (esp_partition_erase_range(this->flash_partition_, previous_offset,
                                  openquatt_common::OpenQuattFlashLayout::SECTOR_SIZE) != ESP_OK) {
      ESP_LOGW(TAG, "Could not erase the stale crash telemetry slot after persisting a cleared record");
    }
  }
  return true;
}

bool OpenQuattCrashTelemetry::clear_record_() {
  if (!this->record_) return false;
  CrashRecord* record = this->record_.data();
  const uint32_t previous_sequence = record->sequence;
  std::memset(record, 0, sizeof(*record));
  record->sequence = previous_sequence;
  if (this->save_record_()) return true;
  this->load_record_();
  return false;
}

bool OpenQuattCrashTelemetry::discard_record_() {
  if (!this->record_) return false;
  CrashRecord* record = this->record_.data();
  const uint32_t previous_sequence = record->sequence;
  std::memset(record, 0, sizeof(*record));
  record->sequence = previous_sequence;
  // Opt-out must fail closed for the current boot. Unlike acknowledged crash
  // publication, do not reload the old flash slot when persistence fails.
  return this->save_record_();
}

bool OpenQuattCrashTelemetry::lock_gate_() const {
  return this->gate_mutex_ != nullptr && xSemaphoreTake(this->gate_mutex_, portMAX_DELAY) == pdTRUE;
}

void OpenQuattCrashTelemetry::unlock_gate_() const { xSemaphoreGive(this->gate_mutex_); }

bool OpenQuattCrashTelemetry::load_state_() {
  if (!this->state_ || !this->state_pref_.load(this->state_.data())) return false;
  const StateStorage& state = *this->state_.data();
  const bool valid = state.magic == STATE_MAGIC && state.version == STATE_VERSION && state.tombstone_pending <= 1U &&
                     state.consent_known <= 1U && state.consent_enabled <= 1U &&
                     state.checksum == checksum_(&state, offsetof(StateStorage, checksum));
  if (!valid) {
    std::memset(this->state_.data(), 0, sizeof(StateStorage));
    return false;
  }
  this->state_loaded_ = true;
  return true;
}

bool OpenQuattCrashTelemetry::save_state_() {
  if (!this->state_ || global_preferences == nullptr) return false;
  StateStorage* state = this->state_.data();
  state->magic = STATE_MAGIC;
  state->version = STATE_VERSION;
  state->checksum = 0U;
  state->checksum = checksum_(state, offsetof(StateStorage, checksum));
  const bool saved = this->state_pref_.save(state) && global_preferences->sync();
  this->state_loaded_ = saved;
  return saved;
}

void OpenQuattCrashTelemetry::setup() {
  this->gate_mutex_ = xSemaphoreCreateMutexStatic(&this->gate_mutex_storage_);
  if (this->gate_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Could not initialize crash telemetry publish gate");
    this->mark_failed();
    return;
  }
  if (!this->record_.allocate_external(1U) || !this->state_.allocate_external(1U)) {
    ESP_LOGE(TAG, "Crash telemetry requires PSRAM for its bounded record buffers");
    this->mark_failed();
    return;
  }
  std::memset(this->record_.data(), 0, sizeof(CrashRecord));
  std::memset(this->state_.data(), 0, sizeof(StateStorage));

  this->flash_partition_ =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, FLASH_PARTITION_LABEL);
  if (this->flash_partition_ == nullptr ||
      openquatt_common::OpenQuattFlashLayout::CRASH_TELEMETRY_END_OFFSET > this->flash_partition_->size) {
    ESP_LOGE(TAG, "Crash telemetry flash window is unavailable in '%s'", FLASH_PARTITION_LABEL);
    this->mark_failed();
    return;
  }

  if (global_preferences == nullptr) {
    ESP_LOGE(TAG, "Preferences backend is unavailable; crash marker will be preserved");
    this->mark_failed();
    return;
  }
  this->state_pref_ = global_preferences->make_preference<StateStorage>(CRASH_STATE_STORAGE_KEY, true);
  this->load_record_();
  if (!this->load_state_()) this->save_state_();

  if (logger::global_logger != nullptr) {
    logger::global_logger->add_log_callback(
        this, [](void* self, uint8_t, const char* tag, const char* message, size_t message_len) {
          static_cast<OpenQuattCrashTelemetry*>(self)->on_log_(tag, message, message_len);
        });
  } else {
    ESP_LOGE(TAG, "Logger is unavailable; ESPHome crash marker will be preserved");
  }

  // Capture before observing consent. An already-available OFF state must not
  // discard an old record and then allow this replay to recreate it.
  this->capture_pending_crash_();

  if (this->usage_switch_ != nullptr) {
    this->usage_switch_->add_on_state_callback([this](bool enabled) { this->on_consent_state_(enabled); });
  }
  if (this->installation_id_sensor_ != nullptr) {
    this->installation_id_sensor_->add_on_state_callback(
        [this](const std::string& value) { this->on_installation_id_(value); });
  }
  if (this->setup_complete_sensor_ != nullptr) {
    this->setup_complete_sensor_->add_on_state_callback([this](bool complete) { this->on_setup_complete_(complete); });
    if (this->setup_complete_sensor_->has_state()) {
      this->on_setup_complete_(this->setup_complete_sensor_->state);
    }
  }
  if (this->installation_id_sensor_ != nullptr && this->installation_id_sensor_->has_state()) {
    this->on_installation_id_(this->installation_id_sensor_->state);
  }
}

void OpenQuattCrashTelemetry::capture_pending_crash_() {
#ifdef USE_ESP32_CRASH_HANDLER
  if (!esp32::crash_handler_has_data() || logger::global_logger == nullptr || !this->record_) return;

  CrashRecord* record = this->record_.data();
  const uint32_t previous_sequence = record->sequence;
  std::memset(record, 0, sizeof(*record));
  record->sequence = previous_sequence;
  record->pending = 1U;
  record->captured_by_reporting_build = 1U;
  record->build_epoch = static_cast<uint32_t>(ESPHOME_BUILD_TIME);
  record->config_hash = static_cast<uint32_t>(ESPHOME_CONFIG_HASH);
  record->reset_reason = static_cast<uint32_t>(esp_reset_reason());
  random_uuid_(record->crash_id, sizeof(record->crash_id));
  esp_app_get_elf_sha256(record->build_id, sizeof(record->build_id));
  copy_text_(record->source_repository, sizeof(record->source_repository), this->source_repository_);
  copy_text_(record->source_commit, sizeof(record->source_commit), this->source_commit_);
  copy_text_(record->build_target, sizeof(record->build_target), this->build_target_);
  copy_text_(record->release_manifest_url, sizeof(record->release_manifest_url), this->release_manifest_url_);
  copy_text_(record->firmware_version, sizeof(record->firmware_version), this->firmware_version_);
  copy_text_(record->release_channel, sizeof(record->release_channel), this->release_channel_);
  copy_text_(record->esphome_version, sizeof(record->esphome_version), ESPHOME_VERSION);
  copy_text_(record->hardware_profile, sizeof(record->hardware_profile), this->hardware_profile_);
  copy_text_(record->topology, sizeof(record->topology), this->topology_);
  copy_text_(record->connection, sizeof(record->connection), this->connection_);

  this->capture_active_ = true;
  esp32::crash_handler_log();
  this->capture_active_ = false;

  if (record->report_length == 0U) {
    ESP_LOGE(TAG, "ESPHome crash replay produced no capturable lines; preserving its marker");
    return;
  }
  record->report[record->report_length] = '\0';
  if (!this->save_record_()) {
    ESP_LOGE(TAG, "Could not persist crash report; preserving the ESPHome crash marker");
    return;
  }

  // OpenQuattLogHistory can still replay this record during the current boot:
  // ESPHome intentionally keeps its in-RAM valid flag after clearing the NOINIT marker.
  esp32::crash_handler_clear();
  ESP_LOGI(TAG, "Stored crash %s for retained publication", record->crash_id);
#endif
}

void OpenQuattCrashTelemetry::on_log_(const char* tag, const char* message, size_t message_len) {
  (void)message_len;
  if (!this->capture_active_ || !this->record_ || tag == nullptr || message == nullptr ||
      std::strcmp(tag, "esp32.crash") != 0) {
    return;
  }

  CrashRecord* record = this->record_.data();
  const char* body = extract_message_body_(message);
  if (std::strstr(body, "Captured by a different firmware build") != nullptr) {
    record->captured_by_reporting_build = 0U;
  }

  detail::AnsiSequenceFilter ansi_filter;
  for (const char* cursor = body; *cursor != '\0'; ++cursor) {
    const unsigned char c = static_cast<unsigned char>(*cursor);
    if (ansi_filter.should_skip(c)) continue;
    if (c == '\r' || c == '\n') continue;
    if (record->report_length + 2U >= CRASH_REPORT_CAPACITY) {
      record->truncated = 1U;
      break;
    }
    record->report[record->report_length++] = c < 0x20U && c != '\t' ? ' ' : static_cast<char>(c);
  }
  if (record->report_length + 2U < CRASH_REPORT_CAPACITY) {
    record->report[record->report_length++] = '\n';
    record->report[record->report_length] = '\0';
  } else {
    record->truncated = 1U;
  }
}

void OpenQuattCrashTelemetry::on_installation_id_(const std::string& installation_id) {
  if (!this->state_ || installation_id.size() != 36U) return;
  if (std::strcmp(this->state_.data()->installation_id, installation_id.c_str()) != 0) {
    if (!copy_text_(this->state_.data()->installation_id, sizeof(this->state_.data()->installation_id),
                    installation_id)) {
      return;
    }
    this->save_state_();
  }
  if (this->state_.data()->tombstone_pending != 0U) this->schedule_immediate_();
}

void OpenQuattCrashTelemetry::on_setup_complete_(bool complete) {
  if (!complete) this->setup_complete_.store(false);
  if (!this->lock_gate_()) return;
  this->setup_complete_.store(complete);
  this->unlock_gate_();
  if (complete && this->consent_enabled_.load() && this->record_ && this->record_.data()->pending != 0U) {
    this->next_attempt_ms_ = millis() + INITIAL_PUBLISH_DELAY_MS;
  }
}

void OpenQuattCrashTelemetry::on_consent_state_(bool enabled) {
  if (!enabled) this->consent_enabled_.store(false);
  if (!this->state_) return;
  StateStorage* state = this->state_.data();
  const bool discard_before_enabling =
      enabled && persisted_consent_blocks_crash(state->consent_known != 0U, state->consent_enabled != 0U);
  if (discard_before_enabling && this->record_ && this->record_.data()->pending != 0U && !this->discard_record_()) {
    ESP_LOGW(TAG, "Could not persist crash discard before restoring consent; publication remains blocked this boot");
  }
  bool state_changed = false;
  if (this->installation_id_sensor_ != nullptr && this->installation_id_sensor_->has_state()) {
    const std::string& id = this->installation_id_sensor_->state;
    if (id.size() == 36U && std::strcmp(state->installation_id, id.c_str()) != 0) {
      state_changed = copy_text_(state->installation_id, sizeof(state->installation_id), id);
    }
  }

  const bool request_tombstone = should_request_tombstone(state->consent_known != 0U, state->consent_enabled != 0U,
                                                          enabled, valid_installation_id(state->installation_id));
  if (request_tombstone) {
    state->tombstone_pending = 1U;
    state_changed = true;
  }
  if (state->consent_known == 0U || (state->consent_enabled != 0U) != enabled) state_changed = true;
  state->consent_known = 1U;
  state->consent_enabled = enabled ? 1U : 0U;
  if (state_changed) this->save_state_();

  this->consent_seen_ = true;
  if (!this->lock_gate_()) return;
  this->consent_enabled_.store(enabled);
  this->unlock_gate_();
  if (!enabled) {
    if (this->record_ && this->record_.data()->pending != 0U && !this->discard_record_()) {
      ESP_LOGW(TAG, "Could not discard an unpublished crash after opt-out");
    }
    if (this->active_kind_.load() == CrashPublishKind::CRASH) this->session_failed_.store(true);
    if (state->tombstone_pending != 0U) this->schedule_immediate_();
    return;
  }

  if (this->setup_complete_.load() && this->record_ && this->record_.data()->pending != 0U) {
    this->next_attempt_ms_ = millis() + INITIAL_PUBLISH_DELAY_MS;
  }
}

}  // namespace esphome::openquatt_crash_telemetry
