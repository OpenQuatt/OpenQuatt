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
  bool stream_storage_available() const;
  void write_recent_logs(httpd_req_t* req) const;
  esp_err_t handle_log_stream(httpd_req_t* req);

 protected:
  static constexpr size_t ENTRY_CAPACITY = 250;
  static constexpr size_t RAW_MAX_LEN = 224;
  // Dedicated SSE log stream bounds: a slow or disconnected client must never
  // cause unbounded RAM growth or block logging/control. History stays the
  // backfill source; the stream only forwards entries newer than last_seq.
  static constexpr size_t STREAM_MAX_CLIENTS = 2;
  static constexpr size_t STREAM_EVENT_BUFFER_SIZE = 2048;
  static constexpr uint32_t STREAM_HEARTBEAT_INTERVAL_MS = 15000UL;
  static constexpr uint32_t STREAM_SEND_TIMEOUT_MS = 30000UL;
  static constexpr uint8_t STREAM_MAX_EVENTS_PER_LOOP = 8;

  struct LogEntry {
    uint16_t seq{0};
    uint32_t timestamp_s{0};
    uint8_t raw_len{0};
    uint8_t level{0};
    char raw[RAW_MAX_LEN]{};
  };

  struct LogStreamSession {
    bool active{false};
    bool ready{false};
    bool closing{false};
    bool need_gap{false};
    httpd_handle_t hd{nullptr};
    std::atomic<int> fd{0};
    uint16_t last_seq{0};
    uint16_t fail_count{0};
    uint32_t last_activity_ms{0};
    uint32_t first_fail_ms{0};
    uint32_t close_request_ms{0};
    uint16_t gap_oldest{0};
    uint16_t gap_newest{0};
    size_t pend_len{0};
    size_t pend_sent{0};
    PsramBuffer<char> pend_buf{};

    LogStreamSession() = default;
    LogStreamSession(const LogStreamSession&) = delete;
    LogStreamSession& operator=(const LogStreamSession&) = delete;
  };

  bool time_rebased_{false};
  time::RealTimeClock* clock_{nullptr};
  PsramBuffer<LogEntry> entries_{};
  size_t head_{0};
  size_t count_{0};
  uint32_t next_seq_{1};
  std::string csrf_token_;
  SemaphoreHandle_t history_mutex_{nullptr};
  std::array<LogStreamSession, STREAM_MAX_CLIENTS> streams_{};

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
  static bool seq_is_newer_(uint16_t seq, uint16_t base);
  static void stream_free_ctx_(void* ctx);
  bool parse_stream_since_(httpd_req_t* req, bool* has_since, uint16_t* since) const;
  void loop_streams_();
  void request_stream_close_(size_t index, const char* reason);
  bool build_stream_log_event_(const LogEntry& entry, char* out, size_t out_size, size_t* out_len) const;
  bool build_stream_gap_event_(uint16_t oldest, uint16_t newest, const char* reason, char* out, size_t out_size,
                               size_t* out_len) const;
  bool build_stream_heartbeat_(char* out, size_t out_size, size_t* out_len) const;
  bool flush_stream_pending_(size_t index, uint32_t now_ms);
  bool send_stream_buffered_(size_t index, const char* data, size_t len, uint32_t now_ms);
  void pump_stream_session_(size_t index, uint32_t now_ms);
};

}  // namespace openquatt_log_history
}  // namespace esphome
