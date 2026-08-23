#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "PsramBuffer.h"
#include "OpenQuattCrashSnapshot.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace openquatt_log_history {

using openquatt_common::PsramBuffer;

class OpenQuattLogHistory : public Component {
 public:
  void set_enabled_switch(switch_::Switch* enabled_switch) { this->enabled_switch_ = enabled_switch; }
  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }
  void set_build_source_commit(const char* value) { this->build_source_commit_ = value; }
  void set_build_source_repository(const char* value) { this->build_source_repository_ = value; }
  void set_build_target(const char* value) { this->build_target_ = value; }
  void set_build_epoch(uint64_t value) { this->build_epoch_ = value; }
  void set_firmware_version(const char* value) { this->firmware_version_ = value; }
  void set_release_channel(const char* value) { this->release_channel_ = value; }
  void set_hardware_profile(const char* value) { this->hardware_profile_ = value; }
  void set_topology(const char* value) { this->topology_ = value; }
  void set_connection(const char* value) { this->connection_ = value; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_enabled(bool enabled);
  void clear_history();
  const std::string& get_csrf_token() const { return this->csrf_token_; }
  bool storage_available() const { return static_cast<bool>(this->entries_); }
  void write_recent_logs(httpd_req_t* req) const;

  bool has_pending_crash() const;
  bool copy_pending_crash(CrashSnapshot* out) const;
  bool acknowledge_pending_crash(const std::array<uint8_t, 16U>& crash_id);
  bool discard_pending_crash(const std::array<uint8_t, 16U>& crash_id);
  static std::string format_crash_id(const std::array<uint8_t, 16U>& crash_id);

 protected:
  static constexpr size_t ENTRY_CAPACITY = 250;
  static constexpr size_t RAW_MAX_LEN = 224;
  static constexpr uint32_t CRASH_PERSIST_RETRY_INTERVAL_MS = 5000UL;

  struct LogEntry {
    uint16_t seq{0};
    uint32_t timestamp_s{0};
    uint8_t raw_len{0};
    uint8_t level{0};
    char raw[RAW_MAX_LEN]{};
  };

  bool enabled_{true};
  bool time_rebased_{false};
  switch_::Switch* enabled_switch_{nullptr};
  time::RealTimeClock* clock_{nullptr};
  PsramBuffer<LogEntry> entries_{};
  size_t head_{0};
  size_t count_{0};
  uint32_t next_seq_{1};
  std::string csrf_token_;
  SemaphoreHandle_t history_mutex_{nullptr};
  StaticSemaphore_t crash_mutex_storage_{};
  SemaphoreHandle_t crash_mutex_{nullptr};
  ESPPreferenceObject crash_pref_{};
  PsramBuffer<CrashSnapshot> crash_snapshot_{};
  PsramBuffer<CrashSnapshot> crash_candidate_{};
  PsramBuffer<CrashSnapshot> crash_verify_{};
  PsramBuffer<CrashSnapshot> crash_clear_{};
  const char* build_source_commit_{nullptr};
  const char* build_source_repository_{nullptr};
  const char* build_target_{nullptr};
  uint64_t build_epoch_{0U};
  const char* firmware_version_{nullptr};
  const char* release_channel_{nullptr};
  const char* hardware_profile_{nullptr};
  const char* topology_{nullptr};
  const char* connection_{nullptr};

#ifdef USE_ESP32_CRASH_HANDLER
  bool pending_crash_report_{false};
  std::atomic<bool> crash_replay_active_{false};
  bool crash_candidate_ready_{false};
  bool current_crash_breadcrumb_initialized_{false};
  bool pending_crash_breadcrumb_loaded_{false};
  bool pending_crash_breadcrumb_valid_{false};
  uint32_t pending_crash_epoch_s_{0};
  uint32_t pending_crash_uptime_s_{0};
  uint32_t pending_crash_breadcrumb_sequence_{0};
  uint32_t last_crash_breadcrumb_update_ms_{0};
  uint32_t next_crash_persist_retry_ms_{0};
  PsramBuffer<CrashBuildIdentity> pending_crash_build_{};
  PsramBuffer<EspHomeCrashLogParser> crash_log_parser_{};
#endif

  bool capture_enabled_() const;
  bool time_is_valid_() const;
  uint64_t current_time_ms_() const;
  uint64_t current_epoch_offset_ms_() const;
  void sync_time_state_();
  void rebase_history_(uint32_t offset_s);
  void rotate_csrf_token_();
  bool lock_history_() const;
  void unlock_history_() const;
  bool lock_crash_() const;
  void unlock_crash_() const;
  bool load_crash_snapshot_();
  bool persist_crash_snapshot_(const CrashSnapshot& snapshot);
  bool clear_pending_crash_(const std::array<uint8_t, 16U>& crash_id, const char* action);
  void load_current_build_identity_(CrashBuildIdentity* identity) const;

#ifdef USE_ESP32_CRASH_HANDLER
  void load_crash_time_breadcrumb_();
  void initialize_current_crash_time_breadcrumb_();
  void update_crash_time_breadcrumb_();
  void capture_pending_crash_report_();
  void retry_pending_crash_persist_();
  bool complete_crash_candidate_(CrashSnapshot* snapshot);
  bool fingerprint_crash_candidate_(CrashSnapshot* snapshot);
  void consume_crash_log_line_(const char* tag, const char* message);
  static void format_epoch_(uint32_t epoch_s, char* out, size_t out_size);
#endif

  void on_log_(uint8_t level, const char* tag, const char* message, size_t message_len);
  void push_entry_locked_(const LogEntry& entry);
  static uint8_t normalize_level_(uint8_t level);
  static const char* level_to_string_(uint8_t level);
  static void copy_sanitized_log_line_(const char* message, size_t message_len, char* out, size_t out_size);
  static void split_log_fields_(const char* raw, const char** tag_start, size_t* tag_len, const char** message_start,
                                size_t* message_len);
};

}  // namespace openquatt_log_history
}  // namespace esphome
