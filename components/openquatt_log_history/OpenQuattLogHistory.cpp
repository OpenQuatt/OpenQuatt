#include "OpenQuattLogHistory.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "esp_random.h"
#include "esphome/core/defines.h"
#ifdef USE_ESP32_CRASH_HANDLER
#include <esp_attr.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

#include "esphome/components/esp32/crash_handler.h"
#include "esphome/core/build_info_data.h"
#endif
#include "esphome/components/logger/logger.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

namespace esphome {
namespace openquatt_log_history {

static const char* const TAG = "openquatt.log_history";

namespace {

static constexpr uint32_t MIN_VALID_EPOCH_S = 1704067200UL;  // 2024-01-01 00:00:00 UTC
static constexpr uint32_t MAX_VALID_EPOCH_S = 2082758400UL;  // 2036-01-01 00:00:00 UTC

static bool epoch_is_sane(uint32_t epoch_s) { return epoch_s >= MIN_VALID_EPOCH_S && epoch_s < MAX_VALID_EPOCH_S; }

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
static constexpr uint32_t CRASH_TIME_BREADCRUMB_MAGIC = 0x4F514348UL;  // OQCH
static constexpr uint16_t CRASH_TIME_BREADCRUMB_VERSION = 2U;
static constexpr uint32_t CRASH_TIME_BREADCRUMB_UPDATE_INTERVAL_MS = 15000UL;
static constexpr uint16_t CRASH_TIME_BREADCRUMB_TIMESTAMP_VALID = 1U << 0U;
static const uint32_t CRASH_SNAPSHOT_STORAGE_KEY = fnv1_hash("openquatt_crash_snapshot_store");

struct CrashTimeBreadcrumbV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t epoch_s;
  uint32_t uptime_s;
  uint32_t sequence;
  uint32_t crc;
};

struct CrashTimeBreadcrumb {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t epoch_s;
  uint32_t uptime_s;
  uint32_t sequence;
  uint32_t legacy_crc_reserved;
  CrashBuildIdentity build;
  uint32_t crc;
};

RTC_NOINIT_ATTR static CrashTimeBreadcrumb crash_time_breadcrumb;

static uint32_t fnv1a32(const void* data, size_t len) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < len; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

static uint32_t crash_time_breadcrumb_v1_crc(const CrashTimeBreadcrumbV1& breadcrumb) {
  CrashTimeBreadcrumbV1 copy = breadcrumb;
  copy.crc = 0U;
  return fnv1a32(&copy, sizeof(copy));
}

static uint32_t crash_time_breadcrumb_crc(const CrashTimeBreadcrumb& breadcrumb) {
  return crash_crc32(&breadcrumb, offsetof(CrashTimeBreadcrumb, crc));
}

static bool crash_time_breadcrumb_v1_is_valid(const CrashTimeBreadcrumb& breadcrumb) {
  CrashTimeBreadcrumbV1 legacy{};
  std::memcpy(&legacy, &breadcrumb, sizeof(legacy));
  return legacy.magic == CRASH_TIME_BREADCRUMB_MAGIC && legacy.version == 1U && epoch_is_sane(legacy.epoch_s) &&
         legacy.crc == crash_time_breadcrumb_v1_crc(legacy);
}

static bool crash_time_breadcrumb_is_valid(const CrashTimeBreadcrumb& breadcrumb) {
  return breadcrumb.magic == CRASH_TIME_BREADCRUMB_MAGIC && breadcrumb.version == CRASH_TIME_BREADCRUMB_VERSION &&
         breadcrumb.crc == crash_time_breadcrumb_crc(breadcrumb) &&
         ((breadcrumb.flags & CRASH_TIME_BREADCRUMB_TIMESTAMP_VALID) == 0U || epoch_is_sane(breadcrumb.epoch_s));
}

static bool copy_optional_text_(char* destination, size_t destination_size, const char* source) {
  if (destination == nullptr || destination_size == 0U || source == nullptr || source[0] == '\0') {
    return false;
  }
  const size_t length = std::strlen(source);
  return crash_copy_text(destination, destination_size, source, length);
}

static bool is_reconstructable_build_target_(const char* target) {
  if (target == nullptr || std::strncmp(target, "configs/", 8U) != 0 || std::strstr(target, "..") != nullptr) {
    return false;
  }
  const size_t length = std::strlen(target);
  return (length > 5U && std::strcmp(target + length - 5U, ".yaml") == 0) ||
         (length > 4U && std::strcmp(target + length - 4U, ".yml") == 0);
}

static void copy_build_identity_canonical_(CrashBuildIdentity* destination, const CrashBuildIdentity& source) {
  if (destination == nullptr) {
    return;
  }
  destination->flags = source.flags;
  destination->build_epoch = source.build_epoch;
  std::memcpy(destination->elf_sha256, source.elf_sha256, sizeof(destination->elf_sha256));
  std::memcpy(destination->source_commit, source.source_commit, sizeof(destination->source_commit));
  std::memcpy(destination->source_repository, source.source_repository, sizeof(destination->source_repository));
  std::memcpy(destination->build_target, source.build_target, sizeof(destination->build_target));
  std::memcpy(destination->firmware_version, source.firmware_version, sizeof(destination->firmware_version));
  std::memcpy(destination->release_channel, source.release_channel, sizeof(destination->release_channel));
  std::memcpy(destination->hardware_profile, source.hardware_profile, sizeof(destination->hardware_profile));
  std::memcpy(destination->topology, source.topology, sizeof(destination->topology));
  std::memcpy(destination->connection, source.connection, sizeof(destination->connection));
}

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
    if (url_path_matches(url_buf, "/openquatt/logs/recent")) {
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

float OpenQuattLogHistory::get_setup_priority() const { return setup_priority::WIFI; }

bool OpenQuattLogHistory::capture_enabled_() const { return this->enabled_ && this->entries_; }

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
  if (!this->pending_crash_report_) {
    this->update_crash_time_breadcrumb_();
  }
#endif
}

void OpenQuattLogHistory::load_current_build_identity_(CrashBuildIdentity* identity) const {
  if (identity == nullptr) {
    return;
  }
  *identity = CrashBuildIdentity{};
#ifdef USE_ESP32_CRASH_HANDLER
  char elf_sha256[CRASH_ELF_SHA256_HEX_LENGTH + 1U]{};
  esp_app_get_elf_sha256(elf_sha256, sizeof(elf_sha256));
  if (crash_is_hex_string(elf_sha256, CRASH_ELF_SHA256_HEX_LENGTH) &&
      crash_copy_text(identity->elf_sha256, sizeof(identity->elf_sha256), elf_sha256, CRASH_ELF_SHA256_HEX_LENGTH)) {
    identity->flags |= CRASH_BUILD_IDENTITY_ELF_SHA256_VALID;
  }
  if (crash_is_hex_string(this->build_source_commit_, CRASH_SOURCE_COMMIT_LENGTH) &&
      crash_copy_text(identity->source_commit, sizeof(identity->source_commit), this->build_source_commit_,
                      CRASH_SOURCE_COMMIT_LENGTH)) {
    identity->flags |= CRASH_BUILD_IDENTITY_SOURCE_COMMIT_VALID;
  }
  if (copy_optional_text_(identity->source_repository, sizeof(identity->source_repository),
                          this->build_source_repository_)) {
    identity->flags |= CRASH_BUILD_IDENTITY_SOURCE_REPOSITORY_VALID;
  }
  if (is_reconstructable_build_target_(this->build_target_) &&
      copy_optional_text_(identity->build_target, sizeof(identity->build_target), this->build_target_)) {
    identity->flags |= CRASH_BUILD_IDENTITY_BUILD_TARGET_VALID;
  }
  if (this->build_epoch_ != 0U) {
    identity->build_epoch = this->build_epoch_;
    identity->flags |= CRASH_BUILD_IDENTITY_BUILD_EPOCH_VALID;
  }
  if (copy_optional_text_(identity->firmware_version, sizeof(identity->firmware_version), this->firmware_version_)) {
    identity->flags |= CRASH_BUILD_IDENTITY_FIRMWARE_VERSION_VALID;
  }
  if (copy_optional_text_(identity->release_channel, sizeof(identity->release_channel), this->release_channel_)) {
    identity->flags |= CRASH_BUILD_IDENTITY_RELEASE_CHANNEL_VALID;
  }
  if (copy_optional_text_(identity->hardware_profile, sizeof(identity->hardware_profile), this->hardware_profile_)) {
    identity->flags |= CRASH_BUILD_IDENTITY_HARDWARE_PROFILE_VALID;
  }
  if (copy_optional_text_(identity->topology, sizeof(identity->topology), this->topology_)) {
    identity->flags |= CRASH_BUILD_IDENTITY_TOPOLOGY_VALID;
  }
  if (copy_optional_text_(identity->connection, sizeof(identity->connection), this->connection_)) {
    identity->flags |= CRASH_BUILD_IDENTITY_CONNECTION_VALID;
  }
#endif
}

bool OpenQuattLogHistory::lock_crash_() const {
  return this->crash_mutex_ != nullptr && xSemaphoreTake(this->crash_mutex_, portMAX_DELAY) == pdTRUE;
}

void OpenQuattLogHistory::unlock_crash_() const { xSemaphoreGive(this->crash_mutex_); }

bool OpenQuattLogHistory::load_crash_snapshot_() {
  if (!this->crash_snapshot_ || !this->crash_pref_.load(this->crash_snapshot_.data())) {
    return false;
  }
  if (!crash_snapshot_is_valid(*this->crash_snapshot_.data())) {
    ESP_LOGW(TAG, "Ignoring invalid persisted crash snapshot");
    *this->crash_snapshot_.data() = CrashSnapshot{};
    crash_snapshot_finalize(this->crash_snapshot_.data());
    return false;
  }
  return crash_snapshot_is_pending(*this->crash_snapshot_.data());
}

bool OpenQuattLogHistory::persist_crash_snapshot_(const CrashSnapshot& snapshot) {
  if (!this->crash_snapshot_ || !this->crash_verify_ || !crash_snapshot_is_valid(snapshot) ||
      global_preferences == nullptr || !this->crash_pref_.save(&snapshot) || !global_preferences->sync()) {
    return false;
  }
  if (!this->crash_pref_.load(this->crash_verify_.data()) || !crash_snapshot_is_valid(*this->crash_verify_.data()) ||
      std::memcmp(&snapshot, this->crash_verify_.data(), sizeof(snapshot)) != 0) {
    ESP_LOGE(TAG, "Crash snapshot read-after-write verification failed");
    return false;
  }
  *this->crash_snapshot_.data() = *this->crash_verify_.data();
  return true;
}

bool OpenQuattLogHistory::has_pending_crash() const {
  if (!this->lock_crash_()) {
    return false;
  }
  const bool pending = this->crash_snapshot_ && crash_snapshot_is_pending(*this->crash_snapshot_.data());
  this->unlock_crash_();
  return pending;
}

bool OpenQuattLogHistory::copy_pending_crash(CrashSnapshot* out) const {
  if (out == nullptr || !this->lock_crash_()) {
    return false;
  }
  const bool pending = this->crash_snapshot_ && crash_snapshot_is_pending(*this->crash_snapshot_.data());
  if (pending) {
    *out = *this->crash_snapshot_.data();
  }
  this->unlock_crash_();
  return pending;
}

bool OpenQuattLogHistory::clear_pending_crash_(const std::array<uint8_t, 16U>& crash_id, const char* action) {
  if (!this->lock_crash_()) {
    return false;
  }
  if (!this->crash_snapshot_ || !this->crash_clear_ || !crash_snapshot_is_pending(*this->crash_snapshot_.data()) ||
      !crash_id_matches(this->crash_snapshot_.data()->crash_id, crash_id)) {
    this->unlock_crash_();
    return false;
  }
  CrashSnapshot* const cleared = this->crash_clear_.data();
  *cleared = CrashSnapshot{};
  crash_snapshot_finalize(cleared);
  const bool persisted = this->persist_crash_snapshot_(*cleared);
  this->unlock_crash_();
  if (persisted) {
    ESP_LOGD(TAG, "Crash snapshot %s after durable state update", action == nullptr ? "cleared" : action);
  }
  return persisted;
}

bool OpenQuattLogHistory::acknowledge_pending_crash(const std::array<uint8_t, 16U>& crash_id) {
  return this->clear_pending_crash_(crash_id, "acknowledged");
}

bool OpenQuattLogHistory::discard_pending_crash(const std::array<uint8_t, 16U>& crash_id) {
  return this->clear_pending_crash_(crash_id, "discarded");
}

std::string OpenQuattLogHistory::format_crash_id(const std::array<uint8_t, 16U>& crash_id) {
  char uuid[37];
  std::snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", crash_id[0],
                crash_id[1], crash_id[2], crash_id[3], crash_id[4], crash_id[5], crash_id[6], crash_id[7], crash_id[8],
                crash_id[9], crash_id[10], crash_id[11], crash_id[12], crash_id[13], crash_id[14], crash_id[15]);
  return uuid;
}

#ifdef USE_ESP32_CRASH_HANDLER
void OpenQuattLogHistory::load_crash_time_breadcrumb_() {
  this->pending_crash_breadcrumb_loaded_ = false;
  this->pending_crash_breadcrumb_valid_ = false;
  this->pending_crash_epoch_s_ = 0;
  this->pending_crash_uptime_s_ = 0;
  this->pending_crash_breadcrumb_sequence_ = 0;
  if (this->pending_crash_build_) {
    *this->pending_crash_build_.data() = CrashBuildIdentity{};
  }

  if (crash_time_breadcrumb_is_valid(crash_time_breadcrumb)) {
    this->pending_crash_breadcrumb_loaded_ = true;
    this->pending_crash_breadcrumb_valid_ = (crash_time_breadcrumb.flags & CRASH_TIME_BREADCRUMB_TIMESTAMP_VALID) != 0U;
    this->pending_crash_epoch_s_ = crash_time_breadcrumb.epoch_s;
    this->pending_crash_uptime_s_ = crash_time_breadcrumb.uptime_s;
    this->pending_crash_breadcrumb_sequence_ = crash_time_breadcrumb.sequence;
    if (this->pending_crash_build_) {
      *this->pending_crash_build_.data() = crash_time_breadcrumb.build;
    }
    return;
  }

  if (!crash_time_breadcrumb_v1_is_valid(crash_time_breadcrumb)) {
    return;
  }
  CrashTimeBreadcrumbV1 legacy{};
  std::memcpy(&legacy, &crash_time_breadcrumb, sizeof(legacy));
  this->pending_crash_breadcrumb_loaded_ = true;
  this->pending_crash_breadcrumb_valid_ = true;
  this->pending_crash_epoch_s_ = legacy.epoch_s;
  this->pending_crash_uptime_s_ = legacy.uptime_s;
  this->pending_crash_breadcrumb_sequence_ = legacy.sequence;
}

void OpenQuattLogHistory::initialize_current_crash_time_breadcrumb_() {
  CrashTimeBreadcrumb next{};
  next.magic = CRASH_TIME_BREADCRUMB_MAGIC;
  next.version = CRASH_TIME_BREADCRUMB_VERSION;
  next.uptime_s = millis() / 1000UL;
  next.sequence = this->pending_crash_breadcrumb_loaded_ ? (this->pending_crash_breadcrumb_sequence_ + 1U) : 1U;
  this->load_current_build_identity_(&next.build);
  if (this->time_is_valid_()) {
    next.flags |= CRASH_TIME_BREADCRUMB_TIMESTAMP_VALID;
    next.epoch_s = static_cast<uint32_t>(this->clock_->now().timestamp);
  }
  next.crc = crash_time_breadcrumb_crc(next);
  crash_time_breadcrumb = next;
  this->current_crash_breadcrumb_initialized_ = true;
  this->last_crash_breadcrumb_update_ms_ = millis();
}

void OpenQuattLogHistory::update_crash_time_breadcrumb_() {
  if (!this->current_crash_breadcrumb_initialized_) {
    this->initialize_current_crash_time_breadcrumb_();
    return;
  }

  const uint32_t now_ms = millis();
  if (this->last_crash_breadcrumb_update_ms_ != 0 &&
      (now_ms - this->last_crash_breadcrumb_update_ms_) < CRASH_TIME_BREADCRUMB_UPDATE_INTERVAL_MS) {
    return;
  }

  CrashTimeBreadcrumb next = crash_time_breadcrumb;
  if (!crash_time_breadcrumb_is_valid(next)) {
    this->current_crash_breadcrumb_initialized_ = false;
    this->initialize_current_crash_time_breadcrumb_();
    return;
  }
  next.uptime_s = now_ms / 1000UL;
  ++next.sequence;
  if (this->time_is_valid_()) {
    next.flags |= CRASH_TIME_BREADCRUMB_TIMESTAMP_VALID;
    next.epoch_s = static_cast<uint32_t>(this->clock_->now().timestamp);
  }
  next.crc = 0U;
  next.crc = crash_time_breadcrumb_crc(next);
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

bool OpenQuattLogHistory::complete_crash_candidate_(CrashSnapshot* snapshot) {
  if (snapshot == nullptr) {
    return false;
  }
  snapshot->flags |= CRASH_SNAPSHOT_PENDING;
  if (this->pending_crash_breadcrumb_valid_) {
    snapshot->flags |= CRASH_SNAPSHOT_TIMESTAMP_VALID;
    snapshot->timestamp_s = this->pending_crash_epoch_s_;
  }
  if (this->pending_crash_breadcrumb_loaded_) {
    snapshot->uptime_s = this->pending_crash_uptime_s_;
    snapshot->breadcrumb_sequence = this->pending_crash_breadcrumb_sequence_;
    if (this->pending_crash_build_) {
      snapshot->captured_build = *this->pending_crash_build_.data();
    }
  }
  if (!this->fingerprint_crash_candidate_(snapshot)) {
    return false;
  }
  esp_fill_random(snapshot->crash_id.data(), snapshot->crash_id.size());
  snapshot->crash_id[6] = static_cast<uint8_t>((snapshot->crash_id[6] & 0x0FU) | 0x40U);
  snapshot->crash_id[8] = static_cast<uint8_t>((snapshot->crash_id[8] & 0x3FU) | 0x80U);
  snapshot->reset_reason = static_cast<uint32_t>(esp_reset_reason());
  this->load_current_build_identity_(&snapshot->current_build);

  crash_snapshot_resolve_build_guard(snapshot);
  crash_snapshot_finalize(snapshot);
  return crash_snapshot_is_pending(*snapshot);
}

bool OpenQuattLogHistory::fingerprint_crash_candidate_(CrashSnapshot* snapshot) {
  if (snapshot == nullptr || !this->crash_verify_) {
    return false;
  }
  CrashSnapshot* material = this->crash_verify_.data();
  std::memset(material, 0, sizeof(*material));
  material->flags = snapshot->flags & (CRASH_SNAPSHOT_TIMESTAMP_VALID | CRASH_SNAPSHOT_RAW_CAUSE_VALID |
                                       CRASH_SNAPSHOT_FAULT_ADDR_VALID | CRASH_SNAPSHOT_OTHER_CORE_BACKTRACE_VALID);
  material->timestamp_s = snapshot->timestamp_s;
  material->uptime_s = snapshot->uptime_s;
  material->breadcrumb_sequence = snapshot->breadcrumb_sequence;
  copy_build_identity_canonical_(&material->captured_build, snapshot->captured_build);
  material->exception_type = snapshot->exception_type;
  material->crashed_core = snapshot->crashed_core;
  std::memcpy(material->exception_type_name, snapshot->exception_type_name, sizeof(material->exception_type_name));
  std::memcpy(material->reason, snapshot->reason, sizeof(material->reason));
  material->raw_cause = snapshot->raw_cause;
  material->pc = snapshot->pc;
  material->fault_addr = snapshot->fault_addr;
  material->crashed_core_backtrace.core = snapshot->crashed_core_backtrace.core;
  material->crashed_core_backtrace.count = snapshot->crashed_core_backtrace.count;
  material->other_core_backtrace.core = snapshot->other_core_backtrace.core;
  material->other_core_backtrace.count = snapshot->other_core_backtrace.count;
  for (size_t index = 0U; index < snapshot->crashed_core_backtrace.count; ++index) {
    material->crashed_core_backtrace.addresses[index] = snapshot->crashed_core_backtrace.addresses[index];
  }
  for (size_t index = 0U; index < snapshot->other_core_backtrace.count; ++index) {
    material->other_core_backtrace.addresses[index] = snapshot->other_core_backtrace.addresses[index];
  }
  return mbedtls_sha256(reinterpret_cast<const unsigned char*>(material), sizeof(*material),
                        snapshot->marker_fingerprint.data(), 0) == 0;
}

void OpenQuattLogHistory::consume_crash_log_line_(const char* tag, const char* message) {
  if (!this->crash_replay_active_.load() || !this->crash_log_parser_ || tag == nullptr || message == nullptr ||
      std::strcmp(tag, "esp32.crash") != 0) {
    return;
  }
  const char* message_start = message;
  size_t message_length = std::strlen(message);
  split_log_fields_(message, nullptr, nullptr, &message_start, &message_length);
  char sanitized[RAW_MAX_LEN];
  copy_sanitized_log_line_(message_start, message_length, sanitized, sizeof(sanitized));
  this->crash_log_parser_.data()->consume(sanitized);
}

void OpenQuattLogHistory::capture_pending_crash_report_() {
  if (!this->pending_crash_report_) {
    return;
  }
  if (!this->crash_snapshot_ || !this->crash_candidate_ || !this->crash_verify_ || !this->crash_log_parser_ ||
      this->crash_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Crash snapshot storage is unavailable; preserving ESPHome crash record");
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
  ESP_LOGE(TAG, "ESPHome crash report follows; log timestamps below are replay timestamps after reboot");

  this->crash_log_parser_.data()->reset();
  this->crash_replay_active_.store(true);
  esp32::crash_handler_log();
  this->crash_replay_active_.store(false);

  CrashSnapshot* candidate = this->crash_candidate_.data();
  *candidate = CrashSnapshot{};
  if (!this->crash_log_parser_.data()->finish(candidate) || !this->complete_crash_candidate_(candidate)) {
    ESP_LOGE(TAG, "Could not structure the ESPHome crash replay; preserving its retained crash marker");
    return;
  }

  if (!this->lock_crash_()) {
    ESP_LOGE(TAG, "Could not lock crash snapshot storage; preserving ESPHome crash record");
    return;
  }
  if (crash_snapshot_reuse_durable(*this->crash_snapshot_.data(), candidate)) {
    const std::array<uint8_t, 16U> crash_id = candidate->crash_id;
    this->crash_candidate_ready_ = false;
    this->unlock_crash_();
    ESP_LOGI(TAG, "Reusing durable crash snapshot %s after reboot before marker clear",
             format_crash_id(crash_id).c_str());
    esp32::crash_handler_clear();
    this->pending_crash_report_ = false;
    this->initialize_current_crash_time_breadcrumb_();
    return;
  }
  this->crash_candidate_ready_ = true;
  const bool persisted = this->persist_crash_snapshot_(*candidate);
  this->unlock_crash_();
  if (!persisted) {
    this->next_crash_persist_retry_ms_ = millis() + CRASH_PERSIST_RETRY_INTERVAL_MS;
    ESP_LOGE(TAG, "Could not durably persist crash snapshot; preserving ESPHome crash marker for retry");
    return;
  }

  this->crash_candidate_ready_ = false;
  esp32::crash_handler_clear();
  this->pending_crash_report_ = false;
  this->initialize_current_crash_time_breadcrumb_();
}

void OpenQuattLogHistory::retry_pending_crash_persist_() {
  if (!this->pending_crash_report_ || !this->crash_candidate_ready_ || !this->crash_candidate_ ||
      static_cast<int32_t>(millis() - this->next_crash_persist_retry_ms_) < 0) {
    return;
  }
  if (!this->lock_crash_()) {
    this->next_crash_persist_retry_ms_ = millis() + CRASH_PERSIST_RETRY_INTERVAL_MS;
    return;
  }
  const bool persisted = this->persist_crash_snapshot_(*this->crash_candidate_.data());
  this->unlock_crash_();
  if (!persisted) {
    this->next_crash_persist_retry_ms_ = millis() + CRASH_PERSIST_RETRY_INTERVAL_MS;
    return;
  }
  this->crash_candidate_ready_ = false;
  esp32::crash_handler_clear();
  this->pending_crash_report_ = false;
  this->initialize_current_crash_time_breadcrumb_();
}
#endif

void OpenQuattLogHistory::on_log_(uint8_t level, const char* tag, const char* message, size_t message_len) {
#ifdef USE_ESP32_CRASH_HANDLER
  this->consume_crash_log_line_(tag, message);
#endif
  if (!this->capture_enabled_() || message == nullptr || message_len == 0) {
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
  if (!this->lock_history_()) {
    return;
  }
  entry.seq = static_cast<uint16_t>(this->next_seq_++);
  this->push_entry_locked_(entry);
  this->unlock_history_();
}

void OpenQuattLogHistory::set_enabled(bool enabled) { this->enabled_ = enabled; }

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
  this->crash_mutex_ = xSemaphoreCreateMutexStatic(&this->crash_mutex_storage_);
  if (this->crash_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to initialize crash snapshot mutex");
  } else if (!this->crash_snapshot_.allocate_external(1U)) {
    ESP_LOGE(TAG, "Failed to allocate crash snapshot in PSRAM");
  } else {
    *this->crash_snapshot_.data() = CrashSnapshot{};
    crash_snapshot_finalize(this->crash_snapshot_.data());
    if (global_preferences == nullptr) {
      ESP_LOGE(TAG, "Preferences backend is unavailable; ESPHome crash marker will be preserved");
    } else {
      this->crash_pref_ = global_preferences->make_preference<CrashSnapshot>(CRASH_SNAPSHOT_STORAGE_KEY, true);
      this->load_crash_snapshot_();
    }
  }
  if (!this->crash_candidate_.allocate_external(1U)) {
    ESP_LOGE(TAG, "Failed to allocate crash candidate in PSRAM");
  }
  if (!this->crash_verify_.allocate_external(1U)) {
    ESP_LOGE(TAG, "Failed to allocate crash verification buffer in PSRAM");
  }
  if (!this->crash_clear_.allocate_external(1U)) {
    ESP_LOGE(TAG, "Failed to allocate crash clear buffer in PSRAM");
  }
#ifdef USE_ESP32_CRASH_HANDLER
  if (!this->crash_log_parser_.allocate_external(1U)) {
    ESP_LOGE(TAG, "Failed to allocate crash replay parser in PSRAM");
  }
  if (!this->pending_crash_build_.allocate_external(1U)) {
    ESP_LOGE(TAG, "Failed to allocate retained crash build identity in PSRAM");
  }
#endif

#ifdef USE_ESP32_CRASH_HANDLER
  this->load_crash_time_breadcrumb_();
  this->pending_crash_report_ = esp32::crash_handler_has_data();
#endif

  if (logger::global_logger == nullptr) {
    ESP_LOGE(TAG, "global_logger is unavailable");
#ifdef USE_ESP32_CRASH_HANDLER
    if (!this->pending_crash_report_) this->initialize_current_crash_time_breadcrumb_();
#endif
    return;
  }

  this->history_mutex_ = xSemaphoreCreateMutex();
  if (this->history_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate log history mutex");
  } else {
    if (this->enabled_switch_ != nullptr) {
      this->enabled_ = this->enabled_switch_->state;
    }
    if (!this->entries_.allocate_external(ENTRY_CAPACITY)) {
      ESP_LOGE(TAG, "Failed to allocate log history buffer in PSRAM");
    }
  }

  logger::global_logger->add_log_callback(
      this, [](void* self, uint8_t level, const char* tag, const char* message, size_t message_len) {
        static_cast<OpenQuattLogHistory*>(self)->on_log_(level, tag, message, message_len);
      });

#ifdef USE_ESP32_CRASH_HANDLER
  this->capture_pending_crash_report_();
  if (!this->pending_crash_report_ && !this->current_crash_breadcrumb_initialized_)
    this->initialize_current_crash_time_breadcrumb_();
#endif

  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    return;
  }
  this->rotate_csrf_token_();
  web_server_base::global_web_server_base->add_handler(new OpenQuattLogHistoryRequestHandler(this));
  this->sync_time_state_();
}

void OpenQuattLogHistory::loop() {
#ifdef USE_ESP32_CRASH_HANDLER
  this->retry_pending_crash_persist_();
#endif
  this->sync_time_state_();
}

void OpenQuattLogHistory::dump_config() {
  size_t entry_count = 0;
  if (this->lock_history_()) {
    entry_count = this->count_;
    this->unlock_history_();
  }

  ESP_LOGCONFIG(TAG, "OpenQuatt log history");
  ESP_LOGCONFIG(TAG, "  Enabled switch: %s", this->enabled_switch_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Clock: %s", this->clock_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", YESNO(this->enabled_));
  ESP_LOGCONFIG(TAG, "  Entries: %u / %u", static_cast<unsigned>(entry_count), static_cast<unsigned>(ENTRY_CAPACITY));
  ESP_LOGCONFIG(TAG, "  History buffer: %s",
                !this->entries_ ? "missing" : (this->entries_.is_external() ? "PSRAM" : "internal"));
#ifdef USE_ESP32_CRASH_HANDLER
  ESP_LOGCONFIG(TAG, "  ESPHome crash marker pending: %s", YESNO(this->pending_crash_report_));
#endif
  ESP_LOGCONFIG(TAG, "  Durable crash snapshot pending: %s", YESNO(this->has_pending_crash()));
  ESP_LOGCONFIG(TAG, "  Crash snapshot memory: %s",
                !this->crash_snapshot_ ? "missing" : (this->crash_snapshot_.is_external() ? "PSRAM" : "internal"));
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
  if (!writer.write_literal("{\"enabled\":") || !writer.write_literal(this->enabled_ ? "true" : "false") ||
      !writer.write_literal(",\"csrf_token\":") ||
      !writer.write_json_string(this->csrf_token_.c_str(), this->csrf_token_.size()) ||
      !writer.write_literal(",\"entries\":[")) {
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

}  // namespace openquatt_log_history
}  // namespace esphome
