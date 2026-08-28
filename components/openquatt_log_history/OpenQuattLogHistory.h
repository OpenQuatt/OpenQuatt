#pragma once

#include <array>
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
namespace openquatt_log_history {

using openquatt_common::PsramBuffer;

class OpenQuattLogHistory : public Component {
 public:
  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void clear_history();
  const std::string& get_csrf_token() const { return this->csrf_token_; }
  bool storage_available() const { return static_cast<bool>(this->entries_); }
  void write_recent_logs(httpd_req_t* req) const;

 protected:
  static constexpr size_t ENTRY_CAPACITY = 250;
  static constexpr size_t RAW_MAX_LEN = 224;

  struct LogEntry {
    uint16_t seq{0};
    uint32_t timestamp_s{0};
    uint8_t raw_len{0};
    uint8_t level{0};
    char raw[RAW_MAX_LEN]{};
  };

  bool time_rebased_{false};
  time::RealTimeClock* clock_{nullptr};
  PsramBuffer<LogEntry> entries_{};
  size_t head_{0};
  size_t count_{0};
  uint32_t next_seq_{1};
  std::string csrf_token_;
  SemaphoreHandle_t history_mutex_{nullptr};

#ifdef USE_ESP32_CRASH_HANDLER
  bool pending_crash_report_{false};
  bool pending_crash_breadcrumb_valid_{false};
  uint32_t pending_crash_report_since_ms_{0};
  uint32_t pending_crash_epoch_s_{0};
  uint32_t pending_crash_uptime_s_{0};
  uint32_t pending_crash_breadcrumb_sequence_{0};
  uint32_t last_crash_breadcrumb_update_ms_{0};
#endif

  bool time_is_valid_() const;
  uint64_t current_time_ms_() const;
  uint64_t current_epoch_offset_ms_() const;
  void sync_time_state_();
  void rebase_history_(uint32_t offset_s);
  void rotate_csrf_token_();
  bool lock_history_() const;
  void unlock_history_() const;

#ifdef USE_ESP32_CRASH_HANDLER
  void load_crash_time_breadcrumb_();
  void update_crash_time_breadcrumb_();
  void maybe_log_pending_crash_report_();
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
