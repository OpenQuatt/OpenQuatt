#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "PsramBuffer.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_debug_recorder {

using openquatt_common::PsramBuffer;

class OpenQuattDebugRecorder : public Component {
 public:
  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool configure(const std::string& entities, bool reset);
  bool start(uint32_t duration_s);
  bool start_rolling();
  void freeze();
  void stop();
  const std::string& get_csrf_token() const { return this->csrf_token_; }
  void write_status(httpd_req_t* req) const;
  void write_recording(httpd_req_t* req) const;

 protected:
  static constexpr uint32_t SAMPLE_INTERVAL_MS = 10000;
  static constexpr uint32_t DEFAULT_DURATION_S = 15 * 60;
  static constexpr uint32_t MIN_DURATION_S = 60;
  static constexpr uint32_t MAX_DURATION_S = 60 * 60;
  static constexpr size_t BUFFER_BYTES = 1024U * 1024U;
  static constexpr size_t FIELD_CAPACITY = 224;
  static constexpr size_t SYSTEM_FIELD_COUNT = 5;
  static constexpr size_t FIELD_KEY_BYTES = 40;
  static constexpr size_t FIELD_NAME_BYTES = 48;
  static constexpr size_t FIELD_UNIT_BYTES = 24;
  static constexpr size_t STRING_ENTRY_CAPACITY = 1024;
  static constexpr size_t STRING_BUCKET_CAPACITY = 256;
  static constexpr size_t STRING_DATA_BYTES = 64U * 1024U;
  static constexpr size_t SAMPLE_HEADER_BYTES = 8;
  static constexpr uint32_t MISSING_VALUE = UINT32_MAX;
  static constexpr uint16_t INVALID_STRING_INDEX = UINT16_MAX;

  enum class FieldType : uint8_t {
    UNKNOWN = 0,
    SENSOR,
    NUMBER,
    BINARY_SENSOR,
    SWITCH,
    TEXT_SENSOR,
    SELECT,
    SYSTEM_UPTIME_MS,
    SYSTEM_FREE_HEAP,
    SYSTEM_FREE_PSRAM,
    SYSTEM_MIN_FREE_HEAP,
    SYSTEM_LARGEST_FREE_HEAP_BLOCK,
  };

  struct DebugField {
    char key[FIELD_KEY_BYTES]{};
    char name[FIELD_NAME_BYTES]{};
    char unit[FIELD_UNIT_BYTES]{};
    FieldType type{FieldType::UNKNOWN};
    uint16_t value_offset{0};
    uint8_t value_size{0};
    void* source{nullptr};
  };

  struct StringEntry {
    uint32_t hash{0};
    uint32_t offset{0};
    uint32_t ref_count{0};
    uint16_t length{0};
    uint16_t next_index{INVALID_STRING_INDEX};
  };

  struct RecordingSnapshot {
    PsramBuffer<uint8_t> samples{};
    PsramBuffer<DebugField> fields{};
    PsramBuffer<StringEntry> string_entries{};
    PsramBuffer<char> string_data{};
    bool available{false};
    bool active{false};
    bool rolling{false};
    bool frozen{false};
    bool string_overflow{false};
    uint64_t recording_id{0};
    uint64_t exported_at_ms{0};
    uint64_t started_at_ms{0};
    uint64_t ended_at_ms{0};
    uint32_t duration_s{0};
    uint32_t retained_duration_s{0};
    uint32_t retention_capacity_s{0};
    uint32_t event_count{0};
    size_t count{0};
    size_t sample_capacity{0};
    size_t sample_stride{0};
    size_t field_count{0};
    size_t missing_field_count{0};
    size_t string_data_used{0};

    bool allocate();
    const uint8_t* sample_at(size_t index) const;
    const StringEntry* string_at(uint32_t index) const;
  };

  time::RealTimeClock* clock_{nullptr};
  PsramBuffer<uint8_t> samples_{};
  PsramBuffer<DebugField> fields_{};
  PsramBuffer<DebugField> pending_fields_{};
  PsramBuffer<StringEntry> string_entries_{};
  PsramBuffer<uint16_t> string_buckets_{};
  PsramBuffer<uint16_t> string_compaction_order_{};
  PsramBuffer<char> string_data_{};
  bool active_{false};
  bool rolling_{false};
  bool frozen_{false};
  bool configuration_pending_{false};
  bool string_overflow_{false};
  mutable bool export_in_progress_{false};
  uint64_t recording_id_{0};
  uint32_t started_ms_{0};
  uint32_t stopped_ms_{0};
  uint32_t duration_s_{DEFAULT_DURATION_S};
  uint32_t last_sample_ms_{0};
  uint32_t total_change_count_{0};
  uint32_t total_event_count_{0};
  size_t count_{0};
  size_t write_index_{0};
  size_t sample_stride_{0};
  size_t sample_capacity_{0};
  size_t field_count_{0};
  size_t missing_field_count_{0};
  size_t pending_field_count_{0};
  size_t pending_requested_field_count_{0};
  size_t pending_missing_field_count_{0};
  size_t string_count_{0};
  size_t string_data_used_{0};
  std::string csrf_token_{};
  StaticSemaphore_t state_mutex_storage_{};
  mutable SemaphoreHandle_t state_mutex_{nullptr};

  bool available_() const {
    return this->state_mutex_ != nullptr && static_cast<bool>(this->samples_) && this->samples_.is_external() &&
           static_cast<bool>(this->fields_) && this->fields_.is_external() &&
           static_cast<bool>(this->pending_fields_) && this->pending_fields_.is_external() &&
           static_cast<bool>(this->string_entries_) && this->string_entries_.is_external() &&
           static_cast<bool>(this->string_buckets_) && this->string_buckets_.is_external() &&
           static_cast<bool>(this->string_compaction_order_) && this->string_compaction_order_.is_external() &&
           static_cast<bool>(this->string_data_) && this->string_data_.is_external();
  }
  bool lock_state_(TickType_t wait_ticks = portMAX_DELAY) const;
  void unlock_state_() const;
  bool begin_export_() const;
  void end_export_() const;
  bool time_is_valid_() const;
  uint64_t current_time_ms_() const;
  uint64_t started_time_ms_() const;
  uint64_t ended_time_ms_() const;
  uint32_t elapsed_s_() const;
  uint32_t remaining_s_() const;
  uint32_t retained_duration_s_() const;
  uint32_t retention_capacity_s_() const;
  uint32_t estimated_size_() const;
  uint32_t sanitize_duration_s_(uint32_t duration_s) const;
  void clear_();
  void clear_strings_();
  void abort_pending_configuration_();
  bool activate_pending_configuration_();
  bool compact_strings_();
  void capture_sample_();
  uint32_t capture_value_(const DebugField& field);
  uint32_t intern_string_(const char* value, size_t length, bool preserve_unknown = false);
  void retain_string_(uint32_t index);
  void release_sample_strings_(const uint8_t* sample);
  static uint8_t value_size_for_type_(FieldType type);
  static bool event_type_(FieldType type);
  static uint32_t read_value_(const uint8_t* sample, const DebugField& field);
  static void write_value_(uint8_t* sample, const DebugField& field, uint32_t value);
  static uint32_t sample_offset_(const uint8_t* sample);
  static uint16_t sample_change_count_(const uint8_t* sample);
  static uint16_t sample_event_count_(const uint8_t* sample);
  static void write_sample_header_(uint8_t* sample, uint32_t offset_s, uint16_t change_count, uint16_t event_count);
  uint8_t* writable_sample_at_(size_t physical_index);
  const uint8_t* sample_at_(size_t index) const;
  bool capture_snapshot_(RecordingSnapshot* snapshot) const;
  void write_recording_export_(httpd_req_t* req) const;
  void rotate_csrf_token_();
};

}  // namespace openquatt_debug_recorder
}  // namespace esphome
