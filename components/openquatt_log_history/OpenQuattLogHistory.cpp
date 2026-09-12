#include "OpenQuattLogHistory.h"
#include "OpenQuattCrashTimeBreadcrumb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <cerrno>
#include <freertos/task.h>
#include <sys/socket.h>

#include "esp_random.h"
#include "esphome/core/defines.h"
#ifdef USE_ESP32_CRASH_HANDLER
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "esphome/components/esp32/crash_handler.h"
#endif
#include "esphome/components/logger/logger.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

namespace esphome {
namespace openquatt_log_history {

static const char* const TAG = "openquatt.log_history";

namespace {

static bool epoch_is_sane(uint32_t epoch_s) { return crash_epoch_is_sane(epoch_s); }

static std::string base64_encode_bytes_(const uint8_t* data, size_t length) {
  static constexpr char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((length + 2U) / 3U) * 4U);

  for (size_t index = 0; index < length; index += 3U) {
    const uint32_t byte_a = data[index];
    const uint32_t byte_b = index + 1U < length ? data[index + 1U] : 0U;
    const uint32_t byte_c = index + 2U < length ? data[index + 2U] : 0U;
    const uint32_t triple = (byte_a << 16U) | (byte_b << 8U) | byte_c;

    out.push_back(TABLE[(triple >> 18U) & 0x3FU]);
    out.push_back(TABLE[(triple >> 12U) & 0x3FU]);
    out.push_back(index + 1U < length ? TABLE[(triple >> 6U) & 0x3FU] : '=');
    out.push_back(index + 2U < length ? TABLE[triple & 0x3FU] : '=');
  }

  return out;
}

static void fill_random_token_(std::array<uint8_t, 32>* token) {
  if (token == nullptr) {
    return;
  }
  for (size_t index = 0; index < token->size(); index += sizeof(uint32_t)) {
    const uint32_t random = esp_random();
    for (size_t byte_index = 0; byte_index < sizeof(uint32_t) && index + byte_index < token->size(); ++byte_index) {
      (*token)[index + byte_index] = static_cast<uint8_t>(random >> (byte_index * 8U));
    }
  }
}

static bool header_matches_host_(const std::string& header_value, const std::string& host) {
  if (host.empty() || header_value.empty()) {
    return false;
  }

  size_t authority_start = 0;
  const size_t scheme_pos = header_value.find("://");
  if (scheme_pos != std::string::npos) {
    authority_start = scheme_pos + 3U;
  }
  const size_t authority_end = header_value.find_first_of("/?#", authority_start);
  const std::string authority = header_value.substr(
      authority_start, authority_end == std::string::npos ? std::string::npos : authority_end - authority_start);
  return authority == host;
}

#ifdef USE_ESP32_CRASH_HANDLER
static constexpr uint32_t CRASH_TIME_BREADCRUMB_UPDATE_INTERVAL_MS = 15000UL;
static constexpr uint32_t CRASH_REPORT_WAIT_TIMEOUT_MS = 120000UL;

RTC_NOINIT_ATTR static CrashTimeBreadcrumb crash_time_breadcrumb;
static CrashTimeBreadcrumbBootCache crash_time_breadcrumb_boot_cache;

static const char* reset_reason_to_string(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:
      return "UNKNOWN";
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "OTHER";
  }
}
#endif

static bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) {
    return false;
  }
  const size_t path_len = std::strlen(path);
  return std::strncmp(url, path, path_len) == 0 && (url[path_len] == '\0' || url[path_len] == '?');
}

// Non-blocking socket send for SSE sessions. Prevents the main loop from blocking
// on a slow client; EAGAIN is reported as HTTPD_SOCK_ERR_TIMEOUT so the caller can
// retry later without growing an unbounded per-client queue.
static int log_stream_nonblocking_send_(httpd_handle_t hd, int sockfd, const char* buf, size_t buf_len, int flags) {
  (void)hd;
  if (buf == nullptr) {
    return HTTPD_SOCK_ERR_INVALID;
  }
  const int ret = send(sockfd, buf, buf_len, flags | MSG_DONTWAIT);
  if (ret < 0) {
    const int err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK) {
      return HTTPD_SOCK_ERR_TIMEOUT;
    }
    return HTTPD_SOCK_ERR_FAIL;
  }
  return ret;
}

static bool sse_buffer_append_(char* out, size_t out_size, size_t* pos, const char* data, size_t len) {
  if (out == nullptr || pos == nullptr || (data == nullptr && len != 0)) {
    return false;
  }
  if (len == 0) {
    return true;
  }
  if (*pos >= out_size || len > out_size - *pos) {
    return false;
  }
  std::memcpy(out + *pos, data, len);
  *pos += len;
  return true;
}

static bool sse_buffer_append_literal_(char* out, size_t out_size, size_t* pos, const char* text) {
  if (text == nullptr) {
    return true;
  }
  return sse_buffer_append_(out, out_size, pos, text, std::strlen(text));
}

static bool sse_buffer_append_json_string_(char* out, size_t out_size, size_t* pos, const char* value, size_t len) {
  if (!sse_buffer_append_(out, out_size, pos, "\"", 1)) {
    return false;
  }
  for (size_t index = 0; index < len; ++index) {
    const unsigned char c = static_cast<unsigned char>(value[index]);
    switch (c) {
      case '\\':
        if (!sse_buffer_append_literal_(out, out_size, pos, "\\\\")) {
          return false;
        }
        break;
      case '"':
        if (!sse_buffer_append_literal_(out, out_size, pos, "\\\"")) {
          return false;
        }
        break;
      case '\b':
        if (!sse_buffer_append_literal_(out, out_size, pos, "\\b")) {
          return false;
        }
        break;
      case '\f':
        if (!sse_buffer_append_literal_(out, out_size, pos, "\\f")) {
          return false;
        }
        break;
      case '\n':
        if (!sse_buffer_append_literal_(out, out_size, pos, "\\n")) {
          return false;
        }
        break;
      case '\r':
        if (!sse_buffer_append_literal_(out, out_size, pos, "\\r")) {
          return false;
        }
        break;
      case '\t':
        if (!sse_buffer_append_literal_(out, out_size, pos, "\\t")) {
          return false;
        }
        break;
      default:
        if (c < 0x20) {
          char buffer[7];
          const int written = std::snprintf(buffer, sizeof(buffer), "\\u%04X", c);
          if (written < 0 || !sse_buffer_append_(out, out_size, pos, buffer, static_cast<size_t>(written))) {
            return false;
          }
        } else {
          const char ch = static_cast<char>(c);
          if (!sse_buffer_append_(out, out_size, pos, &ch, 1)) {
            return false;
          }
        }
        break;
    }
  }
  return sse_buffer_append_(out, out_size, pos, "\"", 1);
}

class ChunkedJsonWriter {
 public:
  explicit ChunkedJsonWriter(httpd_req_t* req) : req_(req) { this->buffer_.allocate(BUFFER_SIZE); }

  bool write_char(char c) { return this->write_bytes_(&c, 1); }

  bool write_literal(const char* text) {
    if (text == nullptr) {
      return true;
    }
    return this->write_bytes_(text, std::strlen(text));
  }

  bool write_uint64(uint64_t value) {
    char buffer[32];
    const int len = std::snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    return len >= 0 && this->write_bytes_(buffer, static_cast<size_t>(len));
  }

  bool write_uint32(uint32_t value) {
    char buffer[24];
    const int len = std::snprintf(buffer, sizeof(buffer), "%" PRIu32, value);
    return len >= 0 && this->write_bytes_(buffer, static_cast<size_t>(len));
  }

  bool write_json_string(const char* value, size_t len) {
    if (!this->write_char('"')) {
      return false;
    }
    for (size_t index = 0; index < len; ++index) {
      const unsigned char c = static_cast<unsigned char>(value[index]);
      switch (c) {
        case '\\':
          if (!this->write_literal("\\\\")) {
            return false;
          }
          break;
        case '"':
          if (!this->write_literal("\\\"")) {
            return false;
          }
          break;
        case '\b':
          if (!this->write_literal("\\b")) {
            return false;
          }
          break;
        case '\f':
          if (!this->write_literal("\\f")) {
            return false;
          }
          break;
        case '\n':
          if (!this->write_literal("\\n")) {
            return false;
          }
          break;
        case '\r':
          if (!this->write_literal("\\r")) {
            return false;
          }
          break;
        case '\t':
          if (!this->write_literal("\\t")) {
            return false;
          }
          break;
        default:
          if (c < 0x20) {
            char buffer[7];
            const int written = std::snprintf(buffer, sizeof(buffer), "\\u%04X", c);
            if (written < 0 || !this->write_bytes_(buffer, static_cast<size_t>(written))) {
              return false;
            }
          } else {
            if (!this->write_char(static_cast<char>(c))) {
              return false;
            }
          }
          break;
      }
    }
    return this->write_char('"');
  }

  bool flush() {
    if (this->used_ == 0) {
      return true;
    }
    if (!this->buffer_ ||
        httpd_resp_send_chunk(this->req_, this->buffer_.data(), static_cast<ssize_t>(this->used_)) != ESP_OK) {
      return false;
    }
    this->used_ = 0;
    return true;
  }

 private:
  static constexpr size_t BUFFER_SIZE = 512;

  bool write_bytes_(const char* data, size_t len) {
    if (!this->buffer_) {
      return false;
    }
    if (data == nullptr || len == 0) {
      return true;
    }

    size_t remaining = len;
    const char* cursor = data;
    while (remaining > 0) {
      if (this->used_ == BUFFER_SIZE && !this->flush()) {
        return false;
      }

      const size_t space = BUFFER_SIZE - this->used_;
      const size_t to_copy = std::min(space, remaining);
      std::memcpy(this->buffer_.data() + this->used_, cursor, to_copy);
      this->used_ += to_copy;
      cursor += to_copy;
      remaining -= to_copy;
    }

    return true;
  }

  httpd_req_t* req_;
  PsramBuffer<char> buffer_{};
  size_t used_{0};
};

class OpenQuattLogHistoryRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattLogHistoryRequestHandler(OpenQuattLogHistory* parent) : parent_(parent) {}

  bool passes_same_origin_(AsyncWebServerRequest* request) const {
    const auto host = request->get_header("Host");
    if (!host.has_value() || host->empty()) {
      return false;
    }

    const auto origin = request->get_header("Origin");
    if (origin.has_value() && !header_matches_host_(origin.value(), host.value())) {
      return false;
    }

    const auto referer = request->get_header("Referer");
    if (referer.has_value() && !header_matches_host_(referer.value(), host.value())) {
      return false;
    }

    return true;
  }

  bool passes_csrf_(AsyncWebServerRequest* request) const {
    const std::string csrf_token = request->arg("csrf_token");
    return !csrf_token.empty() && csrf_token == this->parent_->get_csrf_token();
  }

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    if (url_path_matches(url_buf, "/openquatt/logs/recent") || url_path_matches(url_buf, "/openquatt/logs/stream")) {
      return request->method() == HTTP_GET;
    }
    return url_path_matches(url_buf, "/openquatt/logs/clear") && request->method() == HTTP_POST;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    if (url_path_matches(url_buf, "/openquatt/logs/clear")) {
      if (!this->passes_same_origin_(request) || !this->passes_csrf_(request)) {
        request->send(403, "application/json", R"({"ok":false,"error":"forbidden"})");
        return;
      }
      if (!this->parent_->storage_available()) {
        request->send(503, "application/json", R"({"ok":false,"available":false,"error":"psram_unavailable"})");
        return;
      }
      this->parent_->clear_history();
      request->send(200, "application/json", R"({"ok":true})");
      return;
    }
    if (url_path_matches(url_buf, "/openquatt/logs/stream")) {
      httpd_req_t* stream_req = *request;
      this->parent_->handle_log_stream(stream_req);
      return;
    }

    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    this->parent_->write_recent_logs(req);
  }

 protected:
  OpenQuattLogHistory* parent_;
};

}  // namespace

#ifdef USE_ESP32_CRASH_HANDLER
bool consume_crash_time_breadcrumb(CrashTimeBreadcrumbSnapshot* snapshot) {
  return consume_crash_time_breadcrumb_state(&crash_time_breadcrumb, &crash_time_breadcrumb_boot_cache, snapshot);
}

void invalidate_crash_time_breadcrumb() {
  // Invalidate the aligned marker so a reset during cleanup fails closed instead
  // of exposing a stale timestamp from an earlier normal boot.
  invalidate_crash_time_breadcrumb_state(&crash_time_breadcrumb, &crash_time_breadcrumb_boot_cache);
}
#endif

float OpenQuattLogHistory::get_setup_priority() const { return setup_priority::WIFI; }

bool OpenQuattLogHistory::time_is_valid_() const {
  if (this->clock_ == nullptr) {
    return false;
  }
  const auto now = this->clock_->now();
  return now.is_valid() && epoch_is_sane(static_cast<uint32_t>(now.timestamp));
}

uint64_t OpenQuattLogHistory::current_time_ms_() const {
  if (this->time_is_valid_()) {
    const auto now = this->clock_->now();
    return static_cast<uint64_t>(now.timestamp) * 1000ULL;
  }
  return static_cast<uint64_t>(millis());
}

uint64_t OpenQuattLogHistory::current_epoch_offset_ms_() const {
  if (!this->time_is_valid_()) {
    return 0;
  }

  const uint64_t now_ms = this->current_time_ms_();
  const uint64_t mono_ms = static_cast<uint64_t>(millis());
  return now_ms >= mono_ms ? (now_ms - mono_ms) : 0;
}

uint8_t OpenQuattLogHistory::normalize_level_(uint8_t level) {
  if (level > 7) {
    return 7;
  }
  return level;
}

const char* OpenQuattLogHistory::level_to_string_(uint8_t level) {
  switch (normalize_level_(level)) {
    case 1:
      return "E";
    case 2:
      return "W";
    case 3:
      return "I";
    case 4:
      return "C";
    case 5:
      return "D";
    case 6:
      return "V";
    case 7:
      return "VV";
    default:
      return "N";
  }
}

void OpenQuattLogHistory::copy_sanitized_log_line_(const char* message, size_t message_len, char* out,
                                                   size_t out_size) {
  if (out == nullptr || out_size == 0) {
    return;
  }

  size_t write_pos = 0;
  bool truncated = false;
  for (size_t index = 0; index < message_len; ++index) {
    const char c = message[index];
    if (c == '\r' || c == '\n') {
      continue;
    }

    if (c == '\033' && (index + 1) < message_len && message[index + 1] == '[') {
      index += 2;
      while (index < message_len && message[index] != 'm') {
        ++index;
      }
      continue;
    }

    if ((write_pos + 1) >= out_size) {
      truncated = true;
      break;
    }

    out[write_pos++] = c;
  }

  if (truncated && out_size > 4 && write_pos <= (out_size - 4)) {
    out[write_pos++] = '.';
    out[write_pos++] = '.';
    out[write_pos++] = '.';
  }

  out[write_pos] = '\0';
}

void OpenQuattLogHistory::split_log_fields_(const char* raw, const char** tag_start, size_t* tag_len,
                                            const char** message_start, size_t* message_len) {
  if (tag_start != nullptr) {
    *tag_start = raw;
  }
  if (tag_len != nullptr) {
    *tag_len = 0;
  }
  if (message_start != nullptr) {
    *message_start = raw;
  }
  if (message_len != nullptr) {
    *message_len = raw != nullptr ? std::strlen(raw) : 0;
  }

  if (raw == nullptr || raw[0] == '\0') {
    return;
  }

  const char* first_close = std::strchr(raw, ']');
  if (first_close == nullptr) {
    return;
  }

  const char* tag_open = std::strchr(first_close + 1, '[');
  if (tag_open == nullptr) {
    return;
  }

  const char* tag_close = std::strchr(tag_open + 1, ']');
  if (tag_close == nullptr) {
    return;
  }

  const char* resolved_message_start = std::strstr(tag_close + 1, ": ");
  if (resolved_message_start != nullptr) {
    resolved_message_start += 2;
  } else {
    resolved_message_start = tag_close + 1;
  }

  const char* resolved_tag_end = std::strchr(tag_open + 1, ':');
  if (resolved_tag_end == nullptr || resolved_tag_end > tag_close) {
    resolved_tag_end = tag_close;
  }

  if (tag_start != nullptr) {
    *tag_start = tag_open + 1;
  }
  if (tag_len != nullptr) {
    *tag_len = static_cast<size_t>(resolved_tag_end - (tag_open + 1));
  }
  if (message_start != nullptr) {
    *message_start = resolved_message_start;
  }
  if (message_len != nullptr) {
    *message_len = std::strlen(resolved_message_start);
  }
}

void OpenQuattLogHistory::push_entry_locked_(const LogEntry& entry) {
  if (!this->entries_) {
    return;
  }
  if (ENTRY_CAPACITY == 0) {
    return;
  }

  const size_t insert_index = (this->head_ + this->count_) % ENTRY_CAPACITY;
  this->entries_[insert_index] = entry;
  if (this->count_ < ENTRY_CAPACITY) {
    ++this->count_;
  } else {
    this->head_ = (this->head_ + 1) % ENTRY_CAPACITY;
  }
}

void OpenQuattLogHistory::rebase_history_(uint32_t offset_s) {
  if (offset_s == 0 || !this->lock_history_()) {
    return;
  }

  for (size_t index = 0; index < this->count_; ++index) {
    const size_t entry_index = (this->head_ + index) % ENTRY_CAPACITY;
    this->entries_[entry_index].timestamp_s += offset_s;
  }
  this->unlock_history_();
}

void OpenQuattLogHistory::sync_time_state_() {
  const bool valid = this->time_is_valid_();
  if (valid && !this->time_rebased_) {
    const uint64_t offset_ms = this->current_epoch_offset_ms_();
    if (offset_ms > 0) {
      this->rebase_history_(static_cast<uint32_t>(offset_ms / 1000ULL));
    }
    this->time_rebased_ = true;
  }
#ifdef USE_ESP32_CRASH_HANDLER
  if (valid) {
    this->update_crash_time_breadcrumb_();
  }
#endif
}

#ifdef USE_ESP32_CRASH_HANDLER
void OpenQuattLogHistory::load_crash_time_breadcrumb_() {
  this->pending_crash_breadcrumb_valid_ = false;
  this->pending_crash_epoch_s_ = 0;
  this->pending_crash_uptime_s_ = 0;
  this->pending_crash_breadcrumb_sequence_ = 0;

  CrashTimeBreadcrumbSnapshot snapshot{};
  if (!consume_crash_time_breadcrumb(&snapshot)) return;

  this->pending_crash_breadcrumb_valid_ = true;
  this->pending_crash_epoch_s_ = snapshot.epoch_s;
  this->pending_crash_uptime_s_ = snapshot.uptime_s;
  this->pending_crash_breadcrumb_sequence_ = snapshot.sequence;
}

void OpenQuattLogHistory::update_crash_time_breadcrumb_() {
  if (!this->time_is_valid_()) {
    return;
  }

  const uint32_t now_ms = millis();
  if (this->last_crash_breadcrumb_update_ms_ != 0 &&
      (now_ms - this->last_crash_breadcrumb_update_ms_) < CRASH_TIME_BREADCRUMB_UPDATE_INTERVAL_MS) {
    return;
  }

  const auto now = this->clock_->now();
  CrashTimeBreadcrumb next{};
  next.magic = CRASH_TIME_BREADCRUMB_MAGIC;
  next.version = CRASH_TIME_BREADCRUMB_VERSION;
  next.reserved = 0;
  next.epoch_s = static_cast<uint32_t>(now.timestamp);
  next.uptime_s = crash_uptime_seconds_from_microseconds(static_cast<uint64_t>(esp_timer_get_time()));
  next.sequence = crash_time_breadcrumb_is_valid(crash_time_breadcrumb) ? (crash_time_breadcrumb.sequence + 1) : 1;
  next.crc = crash_time_breadcrumb_checksum(next);
  crash_time_breadcrumb = next;
  this->last_crash_breadcrumb_update_ms_ = now_ms;
}

void OpenQuattLogHistory::format_epoch_(uint32_t epoch_s, char* out, size_t out_size) {
  if (out == nullptr || out_size == 0) {
    return;
  }

  const auto time = ESPTime::from_epoch_local(static_cast<time_t>(epoch_s));
  if (!time.is_valid()) {
    std::snprintf(out, out_size, "epoch %" PRIu32, epoch_s);
    return;
  }

  std::snprintf(out, out_size, "%04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(time.year),
                static_cast<unsigned>(time.month), static_cast<unsigned>(time.day_of_month),
                static_cast<unsigned>(time.hour), static_cast<unsigned>(time.minute),
                static_cast<unsigned>(time.second));
}

void OpenQuattLogHistory::maybe_log_pending_crash_report_() {
  if (!this->pending_crash_report_) {
    return;
  }

  const bool time_ready = this->time_is_valid_();
  const uint32_t now_ms = millis();
  if (!time_ready && (now_ms - this->pending_crash_report_since_ms_) < CRASH_REPORT_WAIT_TIMEOUT_MS) {
    return;
  }

  if (this->pending_crash_breadcrumb_valid_) {
    char timestamp[32];
    format_epoch_(this->pending_crash_epoch_s_, timestamp, sizeof(timestamp));
    ESP_LOGE(TAG,
             "Previous boot crashed; last known controller time before reset: %s (uptime %" PRIu32
             "s, breadcrumb seq %" PRIu32 ")",
             timestamp, this->pending_crash_uptime_s_, this->pending_crash_breadcrumb_sequence_);
  } else {
    ESP_LOGE(TAG, "Previous boot crashed; no retained pre-crash timestamp was available");
  }
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  ESP_LOGE(TAG, "Current boot reset reason: %s (%d)", reset_reason_to_string(reset_reason),
           static_cast<int>(reset_reason));

  if (!time_ready) {
    ESP_LOGW(TAG, "Replaying crash report after waiting without a sane controller clock");
  }
  ESP_LOGE(TAG, "ESPHome crash report follows; log timestamps below are replay timestamps after reboot");
  esp32::crash_handler_log();
  esp32::crash_handler_clear();
  this->pending_crash_report_ = false;
  this->pending_crash_breadcrumb_valid_ = false;
}
#endif

void OpenQuattLogHistory::on_log_(uint8_t level, const char* tag, const char* message, size_t message_len) {
  if (!this->entries_ || message == nullptr || message_len == 0) {
    return;
  }

  LogEntry entry{};
  // The API uses this compact sequence only for relative ordering; wrapping at
  // uint16_t is intentional.
  entry.timestamp_s = static_cast<uint32_t>(this->current_time_ms_() / 1000ULL);
  entry.level = normalize_level_(level);
  copy_sanitized_log_line_(message, message_len, entry.raw, sizeof(entry.raw));
  entry.raw_len = static_cast<uint8_t>(std::strlen(entry.raw));

  if (entry.raw_len == 0) {
    return;
  }

  (void)tag;
  if (!this->lock_history_()) {
    return;
  }
  entry.seq = static_cast<uint16_t>(this->next_seq_++);
  this->push_entry_locked_(entry);
  this->unlock_history_();
}

void OpenQuattLogHistory::clear_history() {
  if (!this->lock_history_()) {
    return;
  }
  this->head_ = 0;
  this->count_ = 0;
  this->next_seq_ = 1;
  this->unlock_history_();
}

void OpenQuattLogHistory::rotate_csrf_token_() {
  std::array<uint8_t, 32> token_bytes{};
  fill_random_token_(&token_bytes);
  this->csrf_token_ = base64_encode_bytes_(token_bytes.data(), token_bytes.size());
}

bool OpenQuattLogHistory::lock_history_() const {
  return this->history_mutex_ != nullptr && xSemaphoreTake(this->history_mutex_, portMAX_DELAY) == pdTRUE;
}

void OpenQuattLogHistory::unlock_history_() const { xSemaphoreGive(this->history_mutex_); }

void OpenQuattLogHistory::setup() {
#ifdef USE_ESP32_CRASH_HANDLER
  this->load_crash_time_breadcrumb_();
  this->pending_crash_report_ = esp32::crash_handler_has_data();
  this->pending_crash_report_since_ms_ = millis();
  if (!this->pending_crash_report_) {
    invalidate_crash_time_breadcrumb();
    this->pending_crash_breadcrumb_valid_ = false;
  }
#endif

  if (logger::global_logger == nullptr) {
    ESP_LOGE(TAG, "global_logger is unavailable");
    return;
  }
  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    return;
  }
  this->history_mutex_ = xSemaphoreCreateMutex();
  if (this->history_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate log history mutex");
    return;
  }

  if (!this->entries_.allocate_external(ENTRY_CAPACITY)) {
    ESP_LOGE(TAG, "Failed to allocate log history buffer in PSRAM");
  }
  bool streams_ready = true;
  for (auto& session : this->streams_) {
    if (!session.pend_buf.allocate_external(STREAM_EVENT_BUFFER_SIZE)) {
      streams_ready = false;
    }
  }
  if (!streams_ready) {
    ESP_LOGE(TAG, "Failed to allocate log stream buffers in PSRAM; /openquatt/logs/stream unavailable");
  }
  this->rotate_csrf_token_();

  logger::global_logger->add_log_callback(
      this, [](void* self, uint8_t level, const char* tag, const char* message, size_t message_len) {
        static_cast<OpenQuattLogHistory*>(self)->on_log_(level, tag, message, message_len);
      });

  web_server_base::global_web_server_base->add_handler(new OpenQuattLogHistoryRequestHandler(this));
  this->sync_time_state_();
#ifdef USE_ESP32_CRASH_HANDLER
  this->maybe_log_pending_crash_report_();
#endif
}

void OpenQuattLogHistory::loop() {
  this->note_loop_stack_watermark_();
  this->sync_time_state_();
#ifdef USE_ESP32_CRASH_HANDLER
  this->maybe_log_pending_crash_report_();
#endif
  this->loop_streams_();
}

void OpenQuattLogHistory::note_loop_stack_watermark_() {
  const uint32_t now_ms = millis();
  if (this->last_stack_watermark_ms_ != 0 && (now_ms - this->last_stack_watermark_ms_) < 1000U) {
    return;
  }
  this->last_stack_watermark_ms_ = now_ms;
  // ESP-IDF returns the remaining high-watermark directly in bytes.
  const uint32_t free_bytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
  uint32_t observed = this->loop_stack_min_free_bytes_.load(std::memory_order_relaxed);
  while ((observed == 0 || free_bytes < observed) &&
         !this->loop_stack_min_free_bytes_.compare_exchange_weak(observed, free_bytes, std::memory_order_relaxed)) {
  }
}

void OpenQuattLogHistory::dump_config() {
  size_t entry_count = 0;
  size_t stream_count = 0;
  if (this->lock_history_()) {
    entry_count = this->count_;
    for (const auto& session : this->streams_) {
      if (session.active && session.ready) {
        ++stream_count;
      }
    }
    this->unlock_history_();
  }

  ESP_LOGCONFIG(TAG, "OpenQuatt log history");
  ESP_LOGCONFIG(TAG, "  Clock: %s", this->clock_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Entries: %u / %u", static_cast<unsigned>(entry_count), static_cast<unsigned>(ENTRY_CAPACITY));
  ESP_LOGCONFIG(TAG, "  History buffer: %s",
                !this->entries_ ? "missing" : (this->entries_.is_external() ? "PSRAM" : "internal"));
  ESP_LOGCONFIG(TAG, "  Log stream: %u / %u clients", static_cast<unsigned>(stream_count),
                static_cast<unsigned>(STREAM_MAX_CLIENTS));
#ifdef USE_ESP32_CRASH_HANDLER
  ESP_LOGCONFIG(TAG, "  Pending crash report: %s", YESNO(this->pending_crash_report_));
#endif
}

void OpenQuattLogHistory::write_recent_logs(httpd_req_t* req) const {
  if (req == nullptr) {
    return;
  }
  if (!this->storage_available()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, R"({"ok":false,"available":false,"error":"psram_unavailable"})");
    return;
  }

  size_t snapshot_capacity = 0;
  if (!this->lock_history_()) {
    ESP_LOGW(TAG, "Failed to lock recent log history");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to lock recent logs");
    return;
  }
  snapshot_capacity = this->count_;
  this->unlock_history_();

  PsramBuffer<LogEntry> snapshot;
  if (!snapshot.allocate_external(snapshot_capacity)) {
    ESP_LOGW(TAG, "Failed to allocate recent log snapshot");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to snapshot recent logs");
    return;
  }

  if (!this->lock_history_()) {
    ESP_LOGW(TAG, "Failed to lock recent log history");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to lock recent logs");
    return;
  }
  const size_t snapshot_count = std::min(this->count_, snapshot_capacity);
  const size_t start_offset = this->count_ - snapshot_count;
  for (size_t index = 0; index < snapshot_count; ++index) {
    const size_t entry_index = (this->head_ + start_offset + index) % ENTRY_CAPACITY;
    snapshot[index] = this->entries_[entry_index];
  }
  this->unlock_history_();

  ChunkedJsonWriter writer(req);
  if (!writer.write_literal("{\"enabled\":true") || !writer.write_literal(",\"csrf_token\":") ||
      !writer.write_json_string(this->csrf_token_.c_str(), this->csrf_token_.size()) ||
      !writer.write_literal(",\"stream\":{\"eagain\":") ||
      !writer.write_uint32(this->stream_eagain_count_.load(std::memory_order_relaxed)) ||
      !writer.write_literal(",\"partial_sends\":") ||
      !writer.write_uint32(this->stream_partial_send_count_.load(std::memory_order_relaxed)) ||
      !writer.write_literal(",\"send_timeout_closes\":") ||
      !writer.write_uint32(this->stream_send_timeout_close_count_.load(std::memory_order_relaxed)) ||
      !writer.write_literal(",\"send_error_closes\":") ||
      !writer.write_uint32(this->stream_send_error_close_count_.load(std::memory_order_relaxed)) ||
      !writer.write_literal(",\"loop_stack_min_free_bytes\":") ||
      !writer.write_uint32(this->loop_stack_min_free_bytes_.load(std::memory_order_relaxed)) ||
      !writer.write_literal("}") || !writer.write_literal(",\"entries\":[")) {
    ESP_LOGW(TAG, "Failed to start recent log response");
    return;
  }

  auto write_json_entry = [&](const LogEntry& entry) -> bool {
    const char* tag_start = "";
    size_t tag_len = 0;
    const char* message_start = entry.raw;
    size_t message_len = entry.raw_len;
    split_log_fields_(entry.raw, &tag_start, &tag_len, &message_start, &message_len);

    const char* level = level_to_string_(entry.level);

    if (!writer.write_char('{')) {
      return false;
    }
    if (!writer.write_literal("\"ts\":") || !writer.write_uint64(static_cast<uint64_t>(entry.timestamp_s) * 1000ULL) ||
        !writer.write_literal(",\"seq\":") || !writer.write_uint32(static_cast<uint32_t>(entry.seq)) ||
        !writer.write_literal(",\"level\":") || !writer.write_json_string(level, std::strlen(level)) ||
        !writer.write_literal(",\"tag\":") || !writer.write_json_string(tag_start, tag_len) ||
        !writer.write_literal(",\"message\":") || !writer.write_json_string(message_start, message_len) ||
        !writer.write_literal(",\"raw\":") || !writer.write_json_string(entry.raw, entry.raw_len) ||
        !writer.write_char('}')) {
      return false;
    }

    return true;
  };

  for (size_t index = 0; index < snapshot_count; ++index) {
    if (index > 0) {
      if (!writer.write_char(',')) {
        ESP_LOGW(TAG, "Failed to stream recent log separator");
        return;
      }
    }
    if (!write_json_entry(snapshot[index])) {
      ESP_LOGW(TAG, "Failed to stream recent log entry");
      return;
    }
  }

  if (!writer.write_literal("]}")) {
    ESP_LOGW(TAG, "Failed to finish recent log response");
    return;
  }

  if (!writer.flush()) {
    ESP_LOGW(TAG, "Failed to flush recent log response");
    return;
  }

  if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to terminate recent log response");
  }
}

bool OpenQuattLogHistory::stream_storage_available() const {
  for (const auto& session : this->streams_) {
    if (!session.pend_buf) {
      return false;
    }
  }
  return true;
}

bool OpenQuattLogHistory::seq_is_newer_(uint16_t seq, uint16_t base) {
  return log_stream_logic::seq_is_newer(seq, base);
}

void OpenQuattLogHistory::stream_free_ctx_(void* ctx) {
  auto* session = static_cast<LogStreamSession*>(ctx);
  if (session == nullptr) {
    return;
  }
  const int fd = session->fd.exchange(0);
  ESP_LOGD(TAG, "Log stream closed (fd: %d)", fd);
}

bool OpenQuattLogHistory::parse_stream_since_(httpd_req_t* req, bool* has_since, uint16_t* since, bool* invalid) const {
  if (has_since != nullptr) {
    *has_since = false;
  }
  if (since != nullptr) {
    *since = 0;
  }
  if (invalid != nullptr) {
    *invalid = false;
  }
  if (req == nullptr || has_since == nullptr || since == nullptr || invalid == nullptr) {
    return false;
  }

  const size_t query_len = httpd_req_get_url_query_len(req);
  const size_t header_len = httpd_req_get_hdr_value_len(req, "Last-Event-ID");
  if (log_stream_logic::cursor_carrier_oversized(query_len, header_len)) {
    // Fail closed: an oversized query string or cursor header can neither be
    // parsed nor safely truncated, so reject it instead of ignoring a cursor
    // that may hide inside the unreadable tail.
    *invalid = true;
    return true;
  }

  if (query_len > 0) {
    char query[log_stream_logic::CURSOR_QUERY_BUF_SIZE];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
      *invalid = true;
      return true;
    }
    char value[log_stream_logic::CURSOR_VALUE_BUF_SIZE];
    static constexpr const char* KEYS[] = {"since", "last_seq", "lastEventId", "last_event_id"};
    for (const char* key : KEYS) {
      const esp_err_t found = httpd_query_key_value(query, key, value, sizeof(value));
      if (found == ESP_ERR_HTTPD_RESULT_TRUNC) {
        // Cursor key present but value does not fit: reject, do not mask.
        *invalid = true;
        return true;
      }
      if (found == ESP_OK) {
        uint16_t seq = 0;
        if (!log_stream_logic::parse_seq_strict(value, &seq)) {
          *invalid = true;
          return true;
        }
        *has_since = true;
        *since = seq;
        return true;
      }
    }
  }

  if (header_len > 0) {
    char value[log_stream_logic::CURSOR_HEADER_BUF_SIZE];
    if (httpd_req_get_hdr_value_str(req, "Last-Event-ID", value, sizeof(value)) != ESP_OK) {
      *invalid = true;
      return true;
    }
    uint16_t seq = 0;
    if (!log_stream_logic::parse_seq_strict(value, &seq)) {
      *invalid = true;
      return true;
    }
    *has_since = true;
    *since = seq;
    return true;
  }

  return true;
}

bool OpenQuattLogHistory::build_stream_log_event_(const LogEntry& entry, char* out, size_t out_size,
                                                  size_t* out_len) const {
  if (out == nullptr || out_len == nullptr || out_size < 128) {
    return false;
  }
  static constexpr size_t CHUNK_HEADER_LEN = 10;
  size_t pos = CHUNK_HEADER_LEN;

  char num[32];
  int written = std::snprintf(num, sizeof(num), "id: %u\n", static_cast<unsigned>(entry.seq));
  if (written <= 0 || !sse_buffer_append_(out, out_size, &pos, num, static_cast<size_t>(written))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "event: log\n")) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "data: ")) {
    return false;
  }

  const char* tag_start = "";
  size_t tag_len = 0;
  const char* message_start = entry.raw;
  size_t message_len = entry.raw_len;
  split_log_fields_(entry.raw, &tag_start, &tag_len, &message_start, &message_len);
  const char* level = level_to_string_(entry.level);

  if (!sse_buffer_append_literal_(out, out_size, &pos, "{\"seq\":")) {
    return false;
  }
  written = std::snprintf(num, sizeof(num), "%u", static_cast<unsigned>(entry.seq));
  if (written <= 0 || !sse_buffer_append_(out, out_size, &pos, num, static_cast<size_t>(written))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, ",\"ts\":")) {
    return false;
  }
  written = std::snprintf(num, sizeof(num), "%" PRIu64, static_cast<uint64_t>(entry.timestamp_s) * 1000ULL);
  if (written <= 0 || !sse_buffer_append_(out, out_size, &pos, num, static_cast<size_t>(written))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, ",\"level\":")) {
    return false;
  }
  if (!sse_buffer_append_json_string_(out, out_size, &pos, level, std::strlen(level))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, ",\"tag\":")) {
    return false;
  }
  if (!sse_buffer_append_json_string_(out, out_size, &pos, tag_start, tag_len)) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, ",\"message\":")) {
    return false;
  }
  if (!sse_buffer_append_json_string_(out, out_size, &pos, message_start, message_len)) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, ",\"raw\":")) {
    return false;
  }
  if (!sse_buffer_append_json_string_(out, out_size, &pos, entry.raw, entry.raw_len)) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "}\n\n")) {
    return false;
  }

  const size_t payload_len = pos - CHUNK_HEADER_LEN;
  if (payload_len + CHUNK_HEADER_LEN + 2 > out_size) {
    return false;
  }
  char header[CHUNK_HEADER_LEN + 1];
  std::snprintf(header, sizeof(header), "%08X\r\n", static_cast<unsigned>(payload_len));
  std::memcpy(out, header, CHUNK_HEADER_LEN);
  out[pos++] = '\r';
  out[pos++] = '\n';
  *out_len = pos;
  return true;
}

bool OpenQuattLogHistory::build_stream_log_event_truncated_(const LogEntry& entry, char* out, size_t out_size,
                                                            size_t* out_len) const {
  // Fallback for pathological records that do not fit even the enlarged event
  // buffer (raw + tag/message with worst-case JSON escaping). Never skip a
  // sequence: deliver seq/ts/level plus a bounded raw prefix with an explicit
  // truncated flag so diagnostics stay gap-free.
  if (out == nullptr || out_len == nullptr || out_size < 256) {
    return false;
  }
  static constexpr size_t CHUNK_HEADER_LEN = 10;
  static constexpr size_t RAW_PREFIX_MAX = 64;
  size_t pos = CHUNK_HEADER_LEN;

  char num[32];
  int written = std::snprintf(num, sizeof(num), "id: %u\n", static_cast<unsigned>(entry.seq));
  if (written <= 0 || !sse_buffer_append_(out, out_size, &pos, num, static_cast<size_t>(written))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "event: log\n")) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "data: ")) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "{\"seq\":")) {
    return false;
  }
  written = std::snprintf(num, sizeof(num), "%u", static_cast<unsigned>(entry.seq));
  if (written <= 0 || !sse_buffer_append_(out, out_size, &pos, num, static_cast<size_t>(written))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, ",\"ts\":")) {
    return false;
  }
  written = std::snprintf(num, sizeof(num), "%" PRIu64, static_cast<uint64_t>(entry.timestamp_s) * 1000ULL);
  if (written <= 0 || !sse_buffer_append_(out, out_size, &pos, num, static_cast<size_t>(written))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, ",\"level\":")) {
    return false;
  }
  const char* level = level_to_string_(entry.level);
  if (!sse_buffer_append_json_string_(out, out_size, &pos, level, std::strlen(level))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, ",\"truncated\":true,\"raw\":")) {
    return false;
  }
  const size_t prefix_len = entry.raw_len < RAW_PREFIX_MAX ? entry.raw_len : RAW_PREFIX_MAX;
  if (!sse_buffer_append_json_string_(out, out_size, &pos, entry.raw, prefix_len)) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "}\n\n")) {
    return false;
  }

  const size_t payload_len = pos - CHUNK_HEADER_LEN;
  if (payload_len + CHUNK_HEADER_LEN + 2 > out_size) {
    return false;
  }
  char header[CHUNK_HEADER_LEN + 1];
  std::snprintf(header, sizeof(header), "%08X\r\n", static_cast<unsigned>(payload_len));
  std::memcpy(out, header, CHUNK_HEADER_LEN);
  out[pos++] = '\r';
  out[pos++] = '\n';
  *out_len = pos;
  return true;
}

bool OpenQuattLogHistory::build_stream_gap_event_(uint16_t oldest, uint16_t newest, const char* reason, char* out,
                                                  size_t out_size, size_t* out_len) const {
  if (out == nullptr || out_len == nullptr || out_size < 128) {
    return false;
  }
  static constexpr size_t CHUNK_HEADER_LEN = 10;
  size_t pos = CHUNK_HEADER_LEN;

  char num[32];
  int written = std::snprintf(num, sizeof(num), "id: %u\n", static_cast<unsigned>(newest));
  if (written <= 0 || !sse_buffer_append_(out, out_size, &pos, num, static_cast<size_t>(written))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "event: gap\n")) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "data: {\"resync\":true")) {
    return false;
  }
  if (reason != nullptr && reason[0] != '\0') {
    if (!sse_buffer_append_literal_(out, out_size, &pos, ",\"reason\":")) {
      return false;
    }
    if (!sse_buffer_append_json_string_(out, out_size, &pos, reason, std::strlen(reason))) {
      return false;
    }
  }
  written = std::snprintf(num, sizeof(num), ",\"oldest\":%u", static_cast<unsigned>(oldest));
  if (written <= 0 || !sse_buffer_append_(out, out_size, &pos, num, static_cast<size_t>(written))) {
    return false;
  }
  written = std::snprintf(num, sizeof(num), ",\"newest\":%u", static_cast<unsigned>(newest));
  if (written <= 0 || !sse_buffer_append_(out, out_size, &pos, num, static_cast<size_t>(written))) {
    return false;
  }
  if (!sse_buffer_append_literal_(out, out_size, &pos, "}\n\n")) {
    return false;
  }

  const size_t payload_len = pos - CHUNK_HEADER_LEN;
  if (payload_len + CHUNK_HEADER_LEN + 2 > out_size) {
    return false;
  }
  char header[CHUNK_HEADER_LEN + 1];
  std::snprintf(header, sizeof(header), "%08X\r\n", static_cast<unsigned>(payload_len));
  std::memcpy(out, header, CHUNK_HEADER_LEN);
  out[pos++] = '\r';
  out[pos++] = '\n';
  *out_len = pos;
  return true;
}

bool OpenQuattLogHistory::build_stream_heartbeat_(char* out, size_t out_size, size_t* out_len) const {
  if (out == nullptr || out_len == nullptr || out_size < 64) {
    return false;
  }
  static constexpr size_t CHUNK_HEADER_LEN = 10;
  static constexpr const char* PAYLOAD = ": heartbeat\n\n";
  static constexpr size_t PAYLOAD_LEN = 13;
  if (CHUNK_HEADER_LEN + PAYLOAD_LEN + 2 > out_size) {
    return false;
  }
  char header[CHUNK_HEADER_LEN + 1];
  std::snprintf(header, sizeof(header), "%08X\r\n", static_cast<unsigned>(PAYLOAD_LEN));
  std::memcpy(out, header, CHUNK_HEADER_LEN);
  std::memcpy(out + CHUNK_HEADER_LEN, PAYLOAD, PAYLOAD_LEN);
  out[CHUNK_HEADER_LEN + PAYLOAD_LEN] = '\r';
  out[CHUNK_HEADER_LEN + PAYLOAD_LEN + 1] = '\n';
  *out_len = CHUNK_HEADER_LEN + PAYLOAD_LEN + 2;
  return true;
}

bool OpenQuattLogHistory::request_stream_close_(size_t index, const char* reason) {
  // Main-loop only. Terminal: once closing is set, pump_stream_session_()
  // attempts no further socket sends and only drives the async close below.
  // Performs no HTTPD calls itself, so repeated requests are harmless: the
  // single choke point for queueing is maybe_queue_stream_close_().
  if (index >= this->streams_.size()) {
    return false;
  }
  auto& session = this->streams_[index];
  if (session.closing) {
    return false;
  }
  session.closing = true;
  ESP_LOGW(TAG, "Closing log stream client %u (%s)", static_cast<unsigned>(index), reason != nullptr ? reason : "slow");
  // The slot is only recycled after free_ctx confirms the real socket close
  // (see loop_streams_()). Never clear fd here: a late free_ctx must still find
  // this session's fd, otherwise it could wipe a replacement connection that
  // reused the same static slot.
  return true;
}

void OpenQuattLogHistory::stream_close_work_(void* arg) {
  // Runs on the HTTPD task via httpd_queue_work(). Shuts the socket down only
  // when this exact session still owns it: httpd_sess_get_ctx() is evaluated
  // here, at execution time, so a recycled fd number or a reused session slot
  // can never cause a replacement connection to be closed (the flaw that rules
  // out httpd_sess_trigger_close(), which queues a bare sock_db pointer).
  auto* session = static_cast<LogStreamSession*>(arg);
  if (session == nullptr) {
    return;
  }
  const httpd_handle_t hd = session->close_hd;
  const int fd = session->close_fd;
  void* const expected = session->close_expected;
  if (hd != nullptr && fd > 0 && expected != nullptr && httpd_sess_get_ctx(hd, fd) == expected) {
    (void)shutdown(fd, SHUT_RDWR);
  }
  session->close_work_queued.store(false, std::memory_order_release);
}

void OpenQuattLogHistory::maybe_queue_stream_close_(size_t index, uint32_t now_ms) {
  // Main-loop only. Queues at most one identity-checked close work item per
  // session: re-queue only when queueing previously failed (flag was released)
  // or the previous callback finished (flag released by stream_close_work_())
  // while the session demonstrably still exists (fd still set, no free_ctx).
  if (index >= this->streams_.size()) {
    return;
  }
  auto& session = this->streams_[index];
  if (!session.closing) {
    return;
  }
  const int sockfd = session.fd.load(std::memory_order_acquire);
  if (session.hd == nullptr || sockfd <= 0) {
    return;
  }
  if (session.close_request_ms != 0 && (now_ms - session.close_request_ms) < STREAM_CLOSE_RETRY_INTERVAL_MS) {
    return;
  }
  bool expected_queued = false;
  if (!session.close_work_queued.compare_exchange_strong(expected_queued, true, std::memory_order_acq_rel)) {
    return;
  }
  session.close_hd = session.hd;
  session.close_fd = sockfd;
  session.close_expected = &session;
  session.close_request_ms = now_ms;
  if (httpd_queue_work(session.close_hd, &OpenQuattLogHistory::stream_close_work_, &session) != ESP_OK) {
    session.close_work_queued.store(false, std::memory_order_release);
  }
}

bool OpenQuattLogHistory::flush_stream_pending_(size_t index, uint32_t now_ms) {
  if (index >= this->streams_.size()) {
    return false;
  }
  auto& session = this->streams_[index];
  if (!session.pend_buf || session.pend_len <= session.pend_sent) {
    return true;
  }
  const int sockfd = session.fd.load();
  if (session.hd == nullptr || sockfd <= 0) {
    return false;
  }
  const char* base = session.pend_buf.data();
  if (base == nullptr) {
    return false;
  }
  const size_t remaining = session.pend_len - session.pend_sent;
  const int sent = httpd_socket_send(session.hd, sockfd, base + session.pend_sent, remaining, 0);
  if (sent == HTTPD_SOCK_ERR_TIMEOUT) {
    this->stream_eagain_count_.fetch_add(1, std::memory_order_relaxed);
    if (session.first_fail_ms == 0) {
      session.first_fail_ms = now_ms;
    }
    if (session.fail_count < 0xFFFFU) {
      ++session.fail_count;
    }
    if ((now_ms - session.first_fail_ms) >= STREAM_SEND_TIMEOUT_MS) {
      if (this->request_stream_close_(index, "send-timeout")) {
        this->stream_send_timeout_close_count_.fetch_add(1, std::memory_order_relaxed);
      }
    }
    return false;
  }
  if (sent == HTTPD_SOCK_ERR_FAIL || sent == HTTPD_SOCK_ERR_INVALID || sent <= 0) {
    // Note: sent==0 for a non-empty non-blocking send is not the normal
    // backpressure signal (that is HTTPD_SOCK_ERR_TIMEOUT above); it indicates
    // a broken peer. Fail closed so the client resyncs via /recent instead of
    // spinning forever on zero-progress partial sends.
    if (this->request_stream_close_(index, "send-error")) {
      this->stream_send_error_close_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return false;
  }
  if (static_cast<size_t>(sent) < remaining) {
    this->stream_partial_send_count_.fetch_add(1, std::memory_order_relaxed);
    session.pend_sent += static_cast<size_t>(sent);
    session.fail_count = 0;
    session.first_fail_ms = 0;
    return false;
  }
  session.pend_len = 0;
  session.pend_sent = 0;
  session.pending_kind = StreamPendingKind::NONE;
  session.pending_seq = 0;
  session.fail_count = 0;
  session.first_fail_ms = 0;
  session.last_activity_ms = now_ms;
  return true;
}

void OpenQuattLogHistory::pump_stream_session_(size_t index, uint32_t now_ms) {
  if (index >= this->streams_.size()) {
    return;
  }
  auto& session = this->streams_[index];

  if (session.closing) {
    // Terminal: never attempt socket sends for a closing session, not even to
    // drain a stuck pending frame. Just drive the identity-checked async close
    // and wait for free_ctx to confirm the real socket close, which is the only
    // path that recycles the slot.
    this->maybe_queue_stream_close_(index, now_ms);
    return;
  }
  if (!this->flush_stream_pending_(index, now_ms)) {
    return;
  }

  if (session.need_gap) {
    char* buf = session.pend_buf.data();
    if (buf == nullptr) {
      this->request_stream_close_(index, "no-buffer");
      return;
    }
    size_t frame_len = 0;
    if (!this->build_stream_gap_event_(session.gap_oldest, session.gap_newest, "resync", buf, STREAM_EVENT_BUFFER_SIZE,
                                       &frame_len)) {
      this->request_stream_close_(index, "gap-too-large");
      return;
    }
    // Commit at queue time: even if the socket only accepts part of this frame
    // now, the remainder stays pending and need_gap stays cleared, so the gap
    // is never queued twice.
    session.pending_kind = StreamPendingKind::GAP;
    session.pending_seq = session.gap_newest;
    session.need_gap = false;
    session.pend_len = frame_len;
    session.pend_sent = 0;
    if (!this->flush_stream_pending_(index, now_ms)) {
      return;
    }
  }

  // Snapshot one entry at a time under the history lock so socket I/O never
  // blocks the logger callback. Bounded per-loop batch keeps control latency safe.
  for (uint8_t sent = 0; sent < STREAM_MAX_EVENTS_PER_LOOP; ++sent) {
    bool have_entry = false;
    LogEntry entry{};
    bool history_empty = false;
    bool lagged = false;
    uint32_t next_seq = 0;

    if (!this->lock_history_()) {
      return;
    }
    if (this->count_ == 0) {
      history_empty = true;
      next_seq = this->next_seq_;
    } else {
      const size_t oldest_index = this->head_ % ENTRY_CAPACITY;
      const size_t newest_index = (this->head_ + this->count_ - 1) % ENTRY_CAPACITY;
      const uint16_t oldest = this->entries_[oldest_index].seq;
      const uint16_t newest = this->entries_[newest_index].seq;
      if (!log_stream_logic::cursor_in_window(session.last_seq, oldest, newest)) {
        lagged = true;
      } else {
        // Find the oldest entry newer than last_seq.
        for (size_t offset = 0; offset < this->count_; ++offset) {
          const size_t entry_index = (this->head_ + offset) % ENTRY_CAPACITY;
          const LogEntry& candidate = this->entries_[entry_index];
          if (seq_is_newer_(candidate.seq, session.last_seq)) {
            entry = candidate;
            have_entry = true;
            break;
          }
        }
      }
    }
    this->unlock_history_();

    if (lagged) {
      this->request_stream_close_(index, "behind-history");
      return;
    }
    if (history_empty) {
      const uint16_t current = static_cast<uint16_t>((next_seq - 1U) & 0xFFFFUL);
      if (session.last_seq != current) {
        session.last_seq = current;
        session.need_gap = true;
        session.gap_oldest = current;
        session.gap_newest = current;
      }
      break;
    }
    if (!have_entry) {
      break;
    }

    char* buf = session.pend_buf.data();
    if (buf == nullptr) {
      this->request_stream_close_(index, "no-buffer");
      return;
    }
    size_t frame_len = 0;
    if (!this->build_stream_log_event_(entry, buf, STREAM_EVENT_BUFFER_SIZE, &frame_len)) {
      // Pathological record: fall back to a bounded truncated frame so the
      // sequence is still delivered instead of skipped.
      ESP_LOGW(TAG, "Log stream event truncated (seq %u)", static_cast<unsigned>(entry.seq));
      if (!this->build_stream_log_event_truncated_(entry, buf, STREAM_EVENT_BUFFER_SIZE, &frame_len)) {
        this->request_stream_close_(index, "event-too-large");
        return;
      }
    }
    // Commit at queue time: last_seq advances now, so a later EAGAIN/partial
    // send resumes with the remainder of this frame and never re-queues it.
    session.pending_kind = StreamPendingKind::LOG;
    session.pending_seq = entry.seq;
    session.last_seq = entry.seq;
    session.pend_len = frame_len;
    session.pend_sent = 0;
    if (!this->flush_stream_pending_(index, now_ms)) {
      return;
    }
  }

  if (!session.closing && session.pend_len == 0 &&
      (now_ms - session.last_activity_ms) >= STREAM_HEARTBEAT_INTERVAL_MS) {
    char* buf = session.pend_buf.data();
    if (buf == nullptr) {
      return;
    }
    size_t frame_len = 0;
    if (!this->build_stream_heartbeat_(buf, STREAM_EVENT_BUFFER_SIZE, &frame_len)) {
      return;
    }
    session.pending_kind = StreamPendingKind::HEARTBEAT;
    session.pending_seq = 0;
    session.pend_len = frame_len;
    session.pend_sent = 0;
    (void)this->flush_stream_pending_(index, now_ms);
  }
}

void OpenQuattLogHistory::loop_streams_() {
  bool any_active = false;
  if (this->lock_history_()) {
    for (const auto& session : this->streams_) {
      if (session.active) {
        any_active = true;
        break;
      }
    }
    this->unlock_history_();
  } else {
    return;
  }
  if (!any_active) {
    return;
  }
  if (!this->storage_available() || !this->stream_storage_available()) {
    return;
  }
  const uint32_t now_ms = millis();
  httpd_handle_t current_hd = nullptr;
  if (web_server_base::global_web_server_base != nullptr &&
      web_server_base::global_web_server_base->get_server() != nullptr) {
    current_hd = web_server_base::global_web_server_base->get_server()->get_server();
  }
  for (size_t index = 0; index < this->streams_.size(); ++index) {
    const int sockfd = this->streams_[index].fd.load(std::memory_order_acquire);
    const bool work_queued = this->streams_[index].close_work_queued.load(std::memory_order_acquire);
    bool active = false;
    bool ready = false;
    if (this->lock_history_()) {
      auto& session = this->streams_[index];
      active = session.active;
      ready = session.ready;
      if (active && session.hd != nullptr && session.hd != current_hd) {
        // A restarted HTTPD instance must not recycle this static slot before
        // the old session's free_ctx has cleared fd. Otherwise that late
        // callback could exchange the fd of a replacement session to zero.
        session.ready = false;
        ready = false;
        if (sockfd <= 0) {
          // free_ctx confirms the old socket is gone. Its work queue is gone
          // too, so any stale queued-close marker can now be discarded.
          session.active = false;
          session.closing = false;
          session.need_gap = false;
          session.close_work_queued.store(false, std::memory_order_release);
          session.hd = nullptr;
          session.close_hd = nullptr;
          session.close_fd = -1;
          session.close_expected = nullptr;
          session.close_request_ms = 0;
          session.pending_kind = StreamPendingKind::NONE;
          session.pending_seq = 0;
          session.pend_len = 0;
          session.pend_sent = 0;
          active = false;
        }
      } else if (active && ready && sockfd <= 0 && !work_queued) {
        // Reclaim exclusively after the real close confirmation (free_ctx
        // cleared fd) with no close work outstanding, so a late close callback
        // can never wipe a replacement connection reusing this static slot.
        session.active = false;
        session.ready = false;
        session.closing = false;
        session.need_gap = false;
        session.close_hd = nullptr;
        session.close_fd = -1;
        session.close_expected = nullptr;
        session.close_request_ms = 0;
        session.pending_kind = StreamPendingKind::NONE;
        session.pending_seq = 0;
        session.pend_len = 0;
        session.pend_sent = 0;
        active = false;
      }
      this->unlock_history_();
    }
    if (!active || !ready || sockfd <= 0) {
      continue;
    }
    this->pump_stream_session_(index, now_ms);
  }
}

esp_err_t OpenQuattLogHistory::handle_log_stream(httpd_req_t* req) {
  if (req == nullptr) {
    return ESP_FAIL;
  }
  if (!this->storage_available()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, R"({"ok":false,"available":false,"error":"psram_unavailable"})");
    return ESP_OK;
  }
  if (!this->stream_storage_available()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, R"({"ok":false,"available":false,"error":"stream_unavailable"})");
    return ESP_OK;
  }

  bool has_since = false;
  uint16_t since = 0;
  bool cursor_invalid = false;
  this->parse_stream_since_(req, &has_since, &since, &cursor_invalid);
  if (cursor_invalid) {
    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, R"({"ok":false,"error":"bad_since"})");
    return ESP_OK;
  }

  size_t slot = STREAM_MAX_CLIENTS;
  uint16_t initial_seq = 0;
  bool need_gap = false;
  uint16_t gap_oldest = 0;
  uint16_t gap_newest = 0;

  if (!this->lock_history_()) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to lock log history");
    return ESP_OK;
  }
  size_t active_count = 0;
  for (const auto& session : this->streams_) {
    if (session.active) {
      ++active_count;
    }
  }
  if (active_count >= STREAM_MAX_CLIENTS) {
    this->unlock_history_();
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, R"({"ok":false,"error":"busy"})");
    return ESP_OK;
  }

  {
    const bool history_empty = this->count_ == 0;
    uint16_t oldest = 0;
    uint16_t newest = 0;
    if (!history_empty) {
      const size_t oldest_index = this->head_ % ENTRY_CAPACITY;
      const size_t newest_index = (this->head_ + this->count_ - 1) % ENTRY_CAPACITY;
      oldest = this->entries_[oldest_index].seq;
      newest = this->entries_[newest_index].seq;
      gap_oldest = oldest;
      gap_newest = newest;
    }
    const uint16_t next_minus_one = static_cast<uint16_t>((this->next_seq_ - 1U) & 0xFFFFUL);
    const log_stream_logic::CursorResolution resolved =
        log_stream_logic::resolve_initial_seq(has_since, since, history_empty, oldest, newest, next_minus_one);
    initial_seq = resolved.initial_seq;
    need_gap = resolved.need_gap;
  }

  for (size_t index = 0; index < this->streams_.size(); ++index) {
    if (!this->streams_[index].active) {
      slot = index;
      auto& session = this->streams_[index];
      session.active = true;
      session.ready = false;
      session.closing = false;
      session.need_gap = need_gap;
      session.gap_oldest = gap_oldest;
      session.gap_newest = gap_newest;
      session.hd = nullptr;
      session.fd.store(0);
      session.last_seq = initial_seq;
      session.fail_count = 0;
      session.first_fail_ms = 0;
      session.close_request_ms = 0;
      // Reclaim gate guarantees no close work is outstanding for this slot
      // (loop_streams_() only recycles with fd==0 and !close_work_queued).
      session.close_work_queued.store(false, std::memory_order_release);
      session.close_hd = nullptr;
      session.close_fd = -1;
      session.close_expected = nullptr;
      session.pending_kind = StreamPendingKind::NONE;
      session.pending_seq = 0;
      session.pend_len = 0;
      session.pend_sent = 0;
      break;
    }
  }
  this->unlock_history_();

  if (slot >= STREAM_MAX_CLIENTS) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, R"({"ok":false,"error":"busy"})");
    return ESP_OK;
  }

  httpd_resp_set_status(req, HTTPD_200);
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");
  if (httpd_resp_send_chunk(req, "retry: 3000\n\n", 13) != ESP_OK) {
    if (this->lock_history_()) {
      this->streams_[slot].active = false;
      this->streams_[slot].ready = false;
      this->unlock_history_();
    }
    return ESP_OK;
  }

  const int sockfd = httpd_req_to_sockfd(req);
  if (sockfd < 0) {
    if (this->lock_history_()) {
      this->streams_[slot].active = false;
      this->streams_[slot].ready = false;
      this->unlock_history_();
    }
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
  }
  httpd_sess_set_send_override(req->handle, sockfd, log_stream_nonblocking_send_);
  req->sess_ctx = &this->streams_[slot];
  req->free_ctx = stream_free_ctx_;

  if (this->lock_history_()) {
    auto& session = this->streams_[slot];
    session.hd = req->handle;
    session.fd.store(sockfd);
    session.last_activity_ms = millis();
    session.ready = true;
    this->unlock_history_();
  }
  ESP_LOGI(TAG, "Log stream client connected (%u/%u)", static_cast<unsigned>(active_count + 1),
           static_cast<unsigned>(STREAM_MAX_CLIENTS));
  // Intentionally no terminating zero chunk: the connection stays open and
  // loop_streams_() forwards new history entries via raw socket sends.
  return ESP_OK;
}

}  // namespace openquatt_log_history
}  // namespace esphome
