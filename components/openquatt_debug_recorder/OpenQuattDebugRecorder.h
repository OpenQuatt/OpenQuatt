#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_http_server.h>

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
  void write_status(httpd_req_t* req) const;
  void write_recording(httpd_req_t* req) const;

 protected:
  static constexpr uint32_t SAMPLE_INTERVAL_MS = 10000;
  static constexpr uint32_t DEFAULT_DURATION_S = 15 * 60;
  static constexpr uint32_t MIN_DURATION_S = 60;
  static constexpr uint32_t MAX_DURATION_S = 60 * 60;
  static constexpr size_t BUFFER_BYTES = 1024U * 1024U;
  static constexpr size_t FIELD_CAPACITY = 192;
  static constexpr size_t FIELD_KEY_BYTES = 40;
  static constexpr size_t FIELD_NAME_BYTES = 48;
  static constexpr size_t FIELD_UNIT_BYTES = 24;
  static constexpr size_t STRING_ENTRY_CAPACITY = 1024;
  static constexpr size_t STRING_DATA_BYTES = 64U * 1024U;
  static constexpr uint32_t MISSING_VALUE = UINT32_MAX;

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
  };

  struct DebugField {
    char key[FIELD_KEY_BYTES]{};
    char name[FIELD_NAME_BYTES]{};
    char unit[FIELD_UNIT_BYTES]{};
    FieldType type{FieldType::UNKNOWN};
    void* source{nullptr};
  };

  struct StringEntry {
    uint32_t hash{0};
    uint32_t offset{0};
    uint16_t length{0};
  };

  struct DebugSample {
    uint32_t offset_s{0};
    uint32_t values[FIELD_CAPACITY]{};
  };

  static constexpr size_t SAMPLE_CAPACITY = BUFFER_BYTES / sizeof(DebugSample);

  time::RealTimeClock* clock_{nullptr};
  PsramBuffer<DebugSample> samples_{};
  PsramBuffer<DebugField> fields_{};
  PsramBuffer<StringEntry> string_entries_{};
  PsramBuffer<char> string_data_{};
  bool active_{false};
  bool rolling_{false};
  bool frozen_{false};
  uint64_t recording_id_{0};
  uint32_t started_ms_{0};
  uint32_t stopped_ms_{0};
  uint32_t duration_s_{DEFAULT_DURATION_S};
  uint32_t last_sample_ms_{0};
  size_t count_{0};
  size_t write_index_{0};
  size_t field_count_{0};
  size_t missing_field_count_{0};
  size_t string_count_{0};
  size_t string_data_used_{0};
  bool capture_in_progress_{false};
  size_t capture_index_{0};
  bool string_overflow_{false};

  bool available_() const {
    return static_cast<bool>(this->samples_) && this->samples_.is_external() && static_cast<bool>(this->fields_) &&
           this->fields_.is_external() && static_cast<bool>(this->string_entries_) &&
           this->string_entries_.is_external() && static_cast<bool>(this->string_data_) &&
           this->string_data_.is_external();
  }
  bool time_is_valid_() const;
  uint64_t current_time_ms_() const;
  uint64_t started_time_ms_() const;
  uint64_t ended_time_ms_() const;
  uint32_t elapsed_s_() const;
  uint32_t remaining_s_() const;
  uint32_t retained_duration_s_() const;
  uint32_t retention_capacity_s_() const;
  uint32_t sanitize_duration_s_(uint32_t duration_s) const;
  void clear_();
  void clear_strings_();
  bool compact_strings_();
  void capture_sample_();
  uint32_t capture_value_(const DebugField& field);
  uint32_t intern_string_(const char* value, size_t length, bool preserve_unknown = false);
  const StringEntry* string_at_(uint32_t index) const;
  const DebugSample* sample_at_(size_t index) const;
};

}  // namespace openquatt_debug_recorder
}  // namespace esphome
