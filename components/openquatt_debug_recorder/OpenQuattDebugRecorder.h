#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "PsramBuffer.h"
#include "warmup_diagnostics.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace openquatt_debug_recorder {

using openquatt_common::PsramBuffer;

class OpenQuattDebugRecorder : public Component {
 public:
  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }
  // Providers/setters run on the ESPHome loop, never an HTTP worker. Exports
  // copy recorded samples only; they do not read these live diagnostics.
  void set_warmup_progress_provider(uint32_t (*provider)()) { this->warmup_progress_provider_ = provider; }
  void invalidate_warmup_power() {
    this->warmup_power_ = {};
    this->warmup_power_.path = WarmupPath::UNAVAILABLE;
  }
  bool set_warmup_power(uint32_t log_key, const WarmupPowerSnapshot& power) {
    const bool changed = log_key != this->warmup_log_key_;
    this->warmup_log_key_ = log_key;
    this->warmup_power_ = power;
    return changed;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool configure(const std::string& entities, bool reset);
  bool start_rolling();
  bool restart_rolling();
  void stop();
  // Persistent user opt-out for the always-on rolling recorder. Only writes
  // to flash when the user changes the setting, never periodically.
  // Disabling stops sampling but keeps the current buffer downloadable until
  // reboot or restart. Enabling starts a new rolling recording.
  // Returns false when the preference could not be persisted; the in-memory
  // state is then left untouched so a failed opt-out is never reported as
  // successful. A failed sync may already have changed flash; an unchanged
  // retry must confirm persistence before reporting success again.
  bool set_enabled(bool enabled);
  bool enabled() const { return this->enabled_; }
  const std::string& get_csrf_token() const { return this->csrf_token_; }
  void write_status(httpd_req_t* req) const;
  void write_recording(httpd_req_t* req, uint32_t last_minutes) const;

 protected:
  static constexpr uint32_t SAMPLE_INTERVAL_MS = 10000;
  // The full default schema retains over six hours at the 10-second interval.
  static constexpr size_t BUFFER_BYTES = 2U * 1024U * 1024U;
  // Keep at least 12 spare slots after warmup and sensor-source diagnostics. Both
  // field tables remain strict PSRAM; the 48 extra slots per table cost
  // 2 x 48 x sizeof(DebugField) external bytes, with no internal fallback.
  static constexpr size_t FIELD_CAPACITY = 336;
  // Memory/configuration plus warmup progress and five W values/path code.
  static constexpr size_t SYSTEM_FIELD_COUNT = 14;
  static constexpr size_t CONFIGURATION_FIELD_CAPACITY = 16;
  static constexpr size_t CONFIGURATION_SCRATCH_BYTES = 1024;
  static constexpr size_t FIELD_KEY_BYTES = 40;
  static constexpr size_t FIELD_NAME_BYTES = 48;
  static constexpr size_t FIELD_UNIT_BYTES = 24;
  static constexpr size_t STRING_ENTRY_CAPACITY = 4096;
  static constexpr size_t STRING_BUCKET_CAPACITY = 1024;
  static constexpr size_t STRING_DATA_BYTES = 256U * 1024U;
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
    TIME_HHMM,
    SELECT,
    CONFIGURATION_SNAPSHOT,
    SYSTEM_UPTIME_MS,
    SYSTEM_FREE_HEAP,
    SYSTEM_FREE_PSRAM,
    SYSTEM_MIN_FREE_HEAP,
    SYSTEM_LARGEST_FREE_HEAP_BLOCK,
    WARMUP_STATUS,
    WARMUP_ELAPSED,
    WARMUP_BASE_W,
    WARMUP_MINIMUM_W,
    WARMUP_EXTRA_W,
    WARMUP_BEFORE_CAPS_W,
    WARMUP_AFTER_CAPS_W,
    WARMUP_PATH,
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
    bool string_overflow{false};
    bool configuration_snapshot_overflow{false};
    uint64_t recording_id{0};
    uint64_t exported_at_ms{0};
    uint64_t started_at_ms{0};
    uint64_t ended_at_ms{0};
    uint32_t retention_capacity_s{0};
    uint32_t event_count{0};
    size_t count{0};
    size_t sample_capacity{0};
    size_t sample_stride{0};
    size_t field_count{0};
    size_t missing_field_count{0};
    size_t string_data_used{0};

    bool allocate(size_t sample_bytes, size_t fields_count, size_t string_bytes);
    const uint8_t* sample_at(size_t index) const;
    const StringEntry* string_at(uint32_t index) const;
  };

  time::RealTimeClock* clock_{nullptr};
  uint32_t (*warmup_progress_provider_)() = nullptr;
  WarmupPowerSnapshot warmup_power_;
  uint32_t warmup_log_key_ = 0;
  ESPPreferenceObject enabled_pref_{};
  PsramBuffer<uint8_t> samples_{};
  PsramBuffer<DebugField> fields_{};
  PsramBuffer<DebugField> pending_fields_{};
  PsramBuffer<StringEntry> string_entries_{};
  PsramBuffer<uint16_t> string_buckets_{};
  PsramBuffer<uint16_t> string_compaction_order_{};
  PsramBuffer<char> string_data_{};
  PsramBuffer<DebugField> configuration_fields_{};
  PsramBuffer<char> configuration_scratch_{};
  size_t configuration_field_count_{0};
  bool configuration_snapshot_overflow_{false};
  // enabled_ is the persistent user preference (default on). active_ reports
  // whether sampling is actually running. available_() reports whether the
  // recorder is technically usable (PSRAM allocation succeeded).
  bool enabled_{true};
  bool enabled_preference_dirty_{false};
  bool enabled_preference_initialized_{false};
  bool active_{false};
  bool configuration_pending_{false};
  bool string_overflow_{false};
  mutable bool export_in_progress_{false};
  uint64_t recording_id_{0};
  // All recording timing runs on a 64-bit monotonic clock extended from
  // 32-bit millis(), so an always-on recording survives the ~49.7 day
  // millis() wrap with monotonic sample offsets.
  uint64_t started_monotonic_ms_{0};
  uint64_t stopped_monotonic_ms_{0};
  uint64_t last_sample_monotonic_ms_{0};
  uint32_t last_millis_32_{0};
  uint32_t millis_wrap_count_{0};
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
           static_cast<bool>(this->string_data_) && this->string_data_.is_external() &&
           static_cast<bool>(this->configuration_fields_) && this->configuration_fields_.is_external() &&
           static_cast<bool>(this->configuration_scratch_) && this->configuration_scratch_.is_external();
  }
  bool lock_state_(TickType_t wait_ticks = portMAX_DELAY) const;
  void unlock_state_() const;
  bool begin_export_() const;
  void end_export_() const;
  bool initialize_enabled_preference_();
  bool load_enabled_preference_();
  bool save_enabled_preference_();
  // Builds the firmware-owned default recording schema from the generated
  // field list, resolving entities by name. Missing entities are counted and
  // skipped, so partial topologies still record.
  bool configure_default_schema_();
  bool configure_configuration_fields_();
  uint32_t capture_configuration_();
  // Starts rolling recording; caller must hold the state lock and the field
  // configuration must already be active.
  void start_rolling_locked_();
  bool time_is_valid_() const;
  uint64_t current_time_ms_() const;
  // Extends a 32-bit millis() reading to the tracked 64-bit monotonic clock
  // without mutating the wrap tracking (for const contexts).
  uint64_t extend_millis_(uint32_t now_ms) const;
  // Records the current millis() for wrap detection; callers must hold the
  // state lock. loop() calls this on every iteration so no wrap is missed.
  void track_millis_(uint32_t now_ms);
  // Tracked 64-bit monotonic now.
  uint64_t monotonic_ms_(uint32_t now_ms);
  uint64_t started_time_ms_() const;
  uint64_t ended_time_ms_() const;
  uint32_t elapsed_s_() const;
  uint32_t retained_duration_s_() const;
  uint32_t retention_capacity_s_() const;
  uint32_t estimated_size_() const;
  void clear_();
  void clear_strings_();
  void abort_pending_configuration_();
  bool activate_pending_configuration_();
  bool compact_strings_();
  void capture_sample_();
  uint32_t capture_value_(const DebugField& field, uint32_t warmup_progress, const WarmupPowerSnapshot& warmup_power);
  uint32_t intern_string_(const char* value, size_t length, bool preserve_unknown = false);
  void retain_string_(uint32_t index);
  void release_sample_strings_(const uint8_t* sample);
  static uint8_t value_size_for_type_(FieldType type);
  static bool string_type_(FieldType type);
  static bool event_type_(FieldType type);
  static uint32_t read_value_(const uint8_t* sample, const DebugField& field);
  static void write_value_(uint8_t* sample, const DebugField& field, uint32_t value);
  static uint32_t sample_offset_(const uint8_t* sample);
  // Sample offset in whole seconds since recording start for a monotonic now.
  uint32_t sample_offset_s_(uint64_t now_ms) const;
  static uint16_t sample_change_count_(const uint8_t* sample);
  static uint16_t sample_event_count_(const uint8_t* sample);
  static void write_sample_header_(uint8_t* sample, uint32_t offset_s, uint16_t change_count, uint16_t event_count);
  uint8_t* writable_sample_at_(size_t physical_index);
  const uint8_t* sample_at_(size_t index) const;
  bool capture_snapshot_(RecordingSnapshot* snapshot, uint32_t last_minutes) const;
  void write_recording_export_(httpd_req_t* req, uint32_t last_minutes) const;
  void rotate_csrf_token_();
};

}  // namespace openquatt_debug_recorder
}  // namespace esphome
