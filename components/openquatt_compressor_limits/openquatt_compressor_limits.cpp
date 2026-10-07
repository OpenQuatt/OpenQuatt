#include "openquatt_compressor_limits.h"

#include <cinttypes>
#include <cstdio>

#include "nvs.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_compressor_limits {

static constexpr const char* TAG = "openquatt.compressor_limits";
static constexpr const char* NVS_NAMESPACE = "esphome";
static constexpr const char* BUNDLE_KEY = "oq_cycle_limits";

ReadResult NvsLimitsStorage::read_blob_(const char* key, void* data, size_t expected_size) {
  nvs_handle_t handle{};
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ReadResult::MISSING;
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "NVS read open failed: %s", esp_err_to_name(err));
    return ReadResult::ERROR;
  }
  size_t bytes = 0U;
  err = nvs_get_blob(handle, key, nullptr, &bytes);
  if (err == ESP_OK && bytes == expected_size) err = nvs_get_blob(handle, key, data, &bytes);
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ReadResult::MISSING;
  if (err != ESP_OK || bytes != expected_size) {
    ESP_LOGW(TAG, "Keeping unreadable NVS key %s: %s, bytes=%u", key, esp_err_to_name(err),
             static_cast<unsigned>(bytes));
    return ReadResult::ERROR;
  }
  return ReadResult::FOUND;
}

ReadResult NvsLimitsStorage::read_bundle(LimitsRecord& record) {
  return read_blob_(BUNDLE_KEY, &record, sizeof(record));
}

ReadResult NvsLimitsStorage::read_legacy(uint8_t index, float& value) {
  char key[12];
  std::snprintf(key, sizeof(key), "%" PRIu32, legacy_keys_[index]);
  return read_blob_(key, &value, sizeof(value));
}

bool NvsLimitsStorage::write_bundle(const LimitsRecord& record) {
  nvs_handle_t handle{};
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_blob(handle, BUNDLE_KEY, &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
  }
  if (err != ESP_OK) ESP_LOGW(TAG, "Warning limits remain pending: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

bool NvsLimitsStorage::erase_legacy() {
  // Inspect each legacy key before opening for erase. Unexpected blobs are
  // retained; a valid bundle remains authoritative even if cleanup fails.
  bool present[2]{};
  for (uint8_t index = 0U; index < 2U; ++index) {
    float ignored{};
    const ReadResult result = read_legacy(index, ignored);
    if (result == ReadResult::ERROR) return false;
    present[index] = result == ReadResult::FOUND;
  }
  if (!present[0] && !present[1]) return true;
  nvs_handle_t handle{};
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return false;
  bool success = true;
  for (uint8_t index = 0U; index < 2U; ++index) {
    if (!present[index]) continue;
    char key[12];
    std::snprintf(key, sizeof(key), "%" PRIu32, legacy_keys_[index]);
    err = nvs_erase_key(handle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) success = false;
  }
  if (nvs_commit(handle) != ESP_OK) success = false;
  nvs_close(handle);
  return success;
}

void WarningLimitNumber::control(float value) { this->parent_->request(this->index_, value); }

void CompressorLimits::setup() {
  owner_task_ = xTaskGetCurrentTaskHandle();
  for (uint8_t index = 0U; index < 2U; ++index) {
    // Same hash as the former TemplateNumber preference, including devices.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    storage_.set_legacy_key(index, limits_[index]->get_preference_hash());
#pragma GCC diagnostic pop
  }
  if (!state_.restore(storage_)) {
    this->status_set_warning("Warning limits could not be restored; existing NVS keys retained");
    ESP_LOGW(TAG, "Using warning defaults; storage unavailable or unsupported");
  }
  publish_();
  last_attempt_ms_ = millis();
}

void CompressorLimits::publish_() {
  for (uint8_t index = 0U; index < 2U; ++index) limits_[index]->publish_state(state_.value(index));
}

void CompressorLimits::request(uint8_t index, float value) {
  if (!valid_limit(index, value)) return;
  portENTER_CRITICAL(&request_lock_);
  const bool accepted = accepting_requests_;
  if (accepted) {
    requested_[index] = value;
    request_mask_ |= static_cast<uint8_t>(1U << index);
  }
  portEXIT_CRITICAL(&request_lock_);
  if (!accepted) ESP_LOGW(TAG, "Rejected warning limit during shutdown");
  // Preserve synchronous Number semantics on the main task (including two
  // number.increment calls in one automation). Off-task calls use the mailbox.
  if (accepted && xTaskGetCurrentTaskHandle() == owner_task_) drain_requests_();
}

void CompressorLimits::drain_requests_(bool close) {
  float values[2];
  portENTER_CRITICAL(&request_lock_);
  if (close) accepting_requests_ = false;
  const uint8_t mask = request_mask_;
  values[0] = requested_[0];
  values[1] = requested_[1];
  request_mask_ = 0U;
  portEXIT_CRITICAL(&request_lock_);
  uint8_t applied = 0U;
  for (uint8_t index = 0U; index < 2U; ++index) {
    if ((mask & (1U << index)) == 0U) continue;
    if (!state_.set(index, values[index])) {
      ESP_LOGW(TAG, "Rejected warning limit: storage not restored");
      continue;
    }
    applied |= static_cast<uint8_t>(1U << index);
  }
  // Apply the whole mailbox before callbacks run. An on_value callback may
  // set the other limit; never overwrite it with a stale second slot afterward.
  for (uint8_t index = 0U; index < 2U; ++index) {
    if ((applied & (1U << index)) != 0U) limits_[index]->publish_state(state_.value(index));
  }
}

void CompressorLimits::persist_() {
  if (!state_.ready()) {
    if (!state_.restore(storage_)) return;
    publish_();
  }
  if (!state_.flush(storage_)) {
    this->status_set_warning("Warning limits persistence or legacy cleanup pending");
    ESP_LOGW(TAG, "Warning limits persistence or legacy cleanup failed; retrying later");
    return;
  }
  this->status_clear_warning();
}

void CompressorLimits::loop() {
  drain_requests_();
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - last_attempt_ms_) < write_interval_ms_) return;
  last_attempt_ms_ = now;
  if (!state_.ready() || state_.pending()) persist_();
}

void CompressorLimits::on_shutdown() {
  // Freeze admission and drain accepted requests even when reboot/OTA was
  // requested in the same loop turn as number.set.
  drain_requests_(true);
  if (state_.ready() && state_.pending()) persist_();
}

void CompressorLimits::dump_config() {
  ESP_LOGCONFIG(TAG, "Bundled compressor warning limits (16-byte NVS record)");
  ESP_LOGCONFIG(TAG, "  Write interval: %u ms", static_cast<unsigned>(write_interval_ms_));
  LOG_NUMBER("  ", "2h warning limit", limits_[0]);
  LOG_NUMBER("  ", "72h warning limit", limits_[1]);
}

}  // namespace openquatt_compressor_limits
}  // namespace esphome
