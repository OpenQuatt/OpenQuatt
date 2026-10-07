#pragma once

#include <cinttypes>
#include <cstdio>

#include "nvs.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/preferences.h"
#include "oq_supply_calibration_migration_logic.h"

namespace oq_supply_calibration_migration {

class NvsStorage {
 public:
  explicit NvsStorage(esphome::EntityBase* offset_entity) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    offset_key_ = offset_entity->get_preference_hash();
#pragma GCC diagnostic pop
    open_error_ = nvs_open("esphome", NVS_READWRITE, &handle_);
  }
  ~NvsStorage() {
    if (open_error_ == ESP_OK) nvs_close(handle_);
  }
  NvsStorage(const NvsStorage&) = delete;
  NvsStorage& operator=(const NvsStorage&) = delete;

  ReadResult read_word(uint32_t key, uint32_t& word) { return read(key, &word, sizeof(word)); }
  ReadResult read_offset(float& offset) { return read(offset_key_, &offset, sizeof(offset)); }
  ReadResult read_record(uint32_t key, uint32_t (&record)[oq_supply_calibration::kRecordStorageWords]) {
    return read(key, record, sizeof(record));
  }
  bool sync() { return esphome::global_preferences != nullptr && esphome::global_preferences->sync(); }
  bool write_record(uint32_t key, const uint32_t (&record)[oq_supply_calibration::kRecordStorageWords]) {
    if (open_error_ != ESP_OK) return false;
    char text[12];
    key_text(key, text);
    // ESPHome's ESP32 preferences backend stores raw bytes, with no additional
    // blob header/CRC. The source-bound calibration checksum guards this record.
    return nvs_set_blob(handle_, text, record, sizeof(record)) == ESP_OK && nvs_commit(handle_) == ESP_OK;
  }
  bool erase_word(uint32_t key, ReadResult expected_result, uint32_t expected) {
    uint32_t actual = 0U;
    const ReadResult result = read_word(key, actual);
    if (result == ReadResult::ABSENT) return true;
    if (expected_result != ReadResult::FOUND || result != ReadResult::FOUND || actual != expected) return false;
    char text[12];
    key_text(key, text);
    return nvs_erase_key(handle_, text) == ESP_OK && nvs_commit(handle_) == ESP_OK;
  }

 private:
  static void key_text(uint32_t key, char (&text)[12]) { std::snprintf(text, sizeof(text), "%" PRIu32, key); }
  ReadResult read(uint32_t key, void* data, size_t size) {
    if (open_error_ == ESP_ERR_NVS_NOT_FOUND) return ReadResult::ABSENT;
    if (open_error_ != ESP_OK) return ReadResult::ERROR;
    char text[12];
    key_text(key, text);
    size_t length = 0U;
    esp_err_t error = nvs_get_blob(handle_, text, nullptr, &length);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ReadResult::ABSENT;
    if (error == ESP_ERR_NVS_TYPE_MISMATCH || (error == ESP_OK && length != size)) return ReadResult::UNEXPECTED;
    if (error != ESP_OK) return ReadResult::ERROR;
    error = nvs_get_blob(handle_, text, data, &length);
    return error == ESP_OK && length == size ? ReadResult::FOUND : ReadResult::ERROR;
  }

  uint32_t offset_key_{0U};
  nvs_handle_t handle_{};
  esp_err_t open_error_{ESP_FAIL};
};

}  // namespace oq_supply_calibration_migration
