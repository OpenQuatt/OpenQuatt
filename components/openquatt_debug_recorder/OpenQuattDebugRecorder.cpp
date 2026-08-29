#include "OpenQuattDebugRecorder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <esp_heap_caps.h>
#include <esp_random.h>

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_debug_recorder {

static const char* const TAG = "openquatt.debug_recorder";

namespace {

bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) {
    return false;
  }
  const size_t path_len = std::strlen(path);
  return std::strncmp(url, path, path_len) == 0 && (url[path_len] == '\0' || url[path_len] == '?');
}

uint32_t parse_uint_arg(AsyncWebServerRequest* request, const char* name, uint32_t fallback) {
  if (request == nullptr || name == nullptr) {
    return fallback;
  }
  const std::string raw = request->arg(name);
  if (raw.empty()) {
    return fallback;
  }
  for (const char character : raw) {
    if (character < '0' || character > '9') {
      return fallback;
    }
  }
  char* end = nullptr;
  const unsigned long value = std::strtoul(raw.c_str(), &end, 10);
  if (end == raw.c_str() || *end != '\0' || value > std::numeric_limits<uint32_t>::max()) {
    return fallback;
  }
  return static_cast<uint32_t>(value);
}

bool copy_text(char* destination, size_t capacity, const char* source, size_t length) {
  if (destination == nullptr || capacity == 0 || source == nullptr || length >= capacity) {
    return false;
  }
  std::memcpy(destination, source, length);
  destination[length] = '\0';
  return true;
}

bool ascii_equals_ignore_case(const char* value, size_t length, const char* expected) {
  const size_t expected_length = std::strlen(expected);
  if (value == nullptr || length != expected_length) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    char left = value[index];
    char right = expected[index];
    if (left >= 'A' && left <= 'Z') {
      left = static_cast<char>(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = static_cast<char>(right - 'A' + 'a');
    }
    if (left != right) {
      return false;
    }
  }
  return true;
}

bool string_is_missing(const char* value, size_t length, bool preserve_unknown) {
  return length == 0 || (!preserve_unknown && ascii_equals_ignore_case(value, length, "unknown")) ||
         ascii_equals_ignore_case(value, length, "unavailable") || ascii_equals_ignore_case(value, length, "nan") ||
         ascii_equals_ignore_case(value, length, "invalid");
}

uint32_t hash_string(const char* value, size_t length) {
  uint32_t hash = 2166136261U;
  for (size_t index = 0; index < length; ++index) {
    hash ^= static_cast<uint8_t>(value[index]);
    hash *= 16777619U;
  }
  return hash;
}

template <typename EntitiesT>
void* find_entity_in(const EntitiesT& entities, const char* name) {
  for (auto* entity : entities) {
    if (entity != nullptr && entity->get_name() == name) {
      return entity;
    }
  }
  return nullptr;
}

bool header_matches_host(const std::string& header_value, const std::string& host) {
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

std::string base64_encode(const uint8_t* data, size_t length) {
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

class ChunkedJsonWriter {
 public:
  explicit ChunkedJsonWriter(httpd_req_t* req) : req_(req) { this->buffer_.allocate_external(BUFFER_SIZE); }

  bool ready() const { return static_cast<bool>(this->buffer_); }
  bool write_char(char c) { return this->write_bytes_(&c, 1); }

  bool write_literal(const char* text) { return text == nullptr || this->write_bytes_(text, std::strlen(text)); }

  bool write_json_string(const char* value, size_t length) {
    if (!this->write_char('"')) {
      return false;
    }
    for (size_t index = 0; index < length; ++index) {
      const unsigned char c = static_cast<unsigned char>(value[index]);
      switch (c) {
        case '\\':
          if (!this->write_literal("\\\\")) return false;
          break;
        case '"':
          if (!this->write_literal("\\\"")) return false;
          break;
        case '\b':
          if (!this->write_literal("\\b")) return false;
          break;
        case '\f':
          if (!this->write_literal("\\f")) return false;
          break;
        case '\n':
          if (!this->write_literal("\\n")) return false;
          break;
        case '\r':
          if (!this->write_literal("\\r")) return false;
          break;
        case '\t':
          if (!this->write_literal("\\t")) return false;
          break;
        default:
          if (c < 0x20) {
            char buffer[7];
            const int written = std::snprintf(buffer, sizeof(buffer), "\\u%04X", c);
            if (written < 0 || !this->write_bytes_(buffer, static_cast<size_t>(written))) return false;
          } else if (!this->write_char(static_cast<char>(c))) {
            return false;
          }
          break;
      }
    }
    return this->write_char('"');
  }

  bool write_uint32(uint32_t value) {
    char buffer[16];
    const int written = std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(value));
    return written > 0 && this->write_bytes_(buffer, static_cast<size_t>(written));
  }

  bool write_uint64(uint64_t value) {
    char buffer[24];
    const int written = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return written > 0 && this->write_bytes_(buffer, static_cast<size_t>(written));
  }

  bool write_float(float value) {
    if (!std::isfinite(value)) {
      return this->write_literal("null");
    }
    char buffer[24];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.7g", static_cast<double>(value));
    return written > 0 && this->write_bytes_(buffer, static_cast<size_t>(written));
  }

  bool write_bool(bool value) { return this->write_literal(value ? "true" : "false"); }

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

  bool write_bytes_(const char* data, size_t length) {
    if (!this->buffer_) {
      return false;
    }
    size_t remaining = length;
    const char* cursor = data;
    while (remaining > 0) {
      if (this->used_ == BUFFER_SIZE && !this->flush()) {
        return false;
      }
      const size_t to_copy = std::min(BUFFER_SIZE - this->used_, remaining);
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

class OpenQuattDebugRecorderRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattDebugRecorderRequestHandler(OpenQuattDebugRecorder* parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    if (url_path_matches(url_buf, "/openquatt/debug-recording/status")) {
      return request->method() == HTTP_GET;
    }
    if (url_path_matches(url_buf, "/openquatt/debug-recording/configure") ||
        url_path_matches(url_buf, "/openquatt/debug-recording/start") ||
        url_path_matches(url_buf, "/openquatt/debug-recording/freeze") ||
        url_path_matches(url_buf, "/openquatt/debug-recording/stop")) {
      return request->method() == HTTP_POST;
    }
    return url_path_matches(url_buf, "/openquatt/debug-recording/download") && request->method() == HTTP_GET;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    const bool mutation = request->method() == HTTP_POST;
    if (mutation && (!this->passes_same_origin_(request) || !this->passes_csrf_(request))) {
      request->send(403, "application/json", R"({"ok":false,"error":"forbidden"})");
      return;
    }

    if (url_path_matches(url_buf, "/openquatt/debug-recording/configure")) {
      const std::string entities = request->arg("entities");
      if (entities.empty() || !this->parent_->configure(entities, parse_uint_arg(request, "reset", 0) != 0)) {
        request->send(409, "application/json", R"({"ok":false,"error":"configuration_failed"})");
        return;
      }
      this->parent_->write_status(*request);
      return;
    }

    if (url_path_matches(url_buf, "/openquatt/debug-recording/start")) {
      const bool rolling = parse_uint_arg(request, "rolling", 0) != 0;
      uint32_t duration_s = parse_uint_arg(request, "duration_s", 0);
      if (duration_s == 0) {
        const uint32_t minutes = parse_uint_arg(request, "minutes", 15);
        duration_s = minutes > std::numeric_limits<uint32_t>::max() / 60U ? 0 : minutes * 60U;
      }
      const bool started = rolling ? this->parent_->start_rolling() : this->parent_->start(duration_s);
      if (!started) {
        request->send(409, "application/json", R"({"ok":false,"error":"recorder_not_configured"})");
        return;
      }
      this->parent_->write_status(*request);
      return;
    }

    if (url_path_matches(url_buf, "/openquatt/debug-recording/freeze")) {
      this->parent_->freeze();
      this->parent_->write_status(*request);
      return;
    }

    if (url_path_matches(url_buf, "/openquatt/debug-recording/stop")) {
      this->parent_->stop();
      this->parent_->write_status(*request);
      return;
    }

    if (url_path_matches(url_buf, "/openquatt/debug-recording/download")) {
      this->parent_->write_recording(*request);
    } else {
      this->parent_->write_status(*request);
    }
  }

 protected:
  bool passes_same_origin_(AsyncWebServerRequest* request) const {
    const auto host = request->get_header("Host");
    if (!host.has_value() || host->empty()) {
      return false;
    }
    const auto origin = request->get_header("Origin");
    if (origin.has_value() && !header_matches_host(origin.value(), host.value())) {
      return false;
    }
    const auto referer = request->get_header("Referer");
    return !referer.has_value() || header_matches_host(referer.value(), host.value());
  }

  bool passes_csrf_(AsyncWebServerRequest* request) const {
    const std::string csrf_token = request->arg("csrf_token");
    return !csrf_token.empty() && csrf_token == this->parent_->get_csrf_token();
  }

  OpenQuattDebugRecorder* parent_;
};

}  // namespace

bool OpenQuattDebugRecorder::RecordingSnapshot::allocate() {
  return this->samples.allocate_external(BUFFER_BYTES) && this->fields.allocate_external(FIELD_CAPACITY) &&
         this->string_entries.allocate_external(STRING_ENTRY_CAPACITY) &&
         this->string_data.allocate_external(STRING_DATA_BYTES);
}

const uint8_t* OpenQuattDebugRecorder::RecordingSnapshot::sample_at(size_t index) const {
  if (!this->samples || index >= this->count || this->sample_stride == 0) {
    return nullptr;
  }
  return this->samples.data() + index * this->sample_stride;
}

const OpenQuattDebugRecorder::StringEntry* OpenQuattDebugRecorder::RecordingSnapshot::string_at(uint32_t index) const {
  if (!this->string_entries || index >= STRING_ENTRY_CAPACITY) {
    return nullptr;
  }
  const StringEntry& entry = this->string_entries[index];
  return entry.length > 0 && entry.offset + entry.length <= this->string_data_used ? &entry : nullptr;
}

float OpenQuattDebugRecorder::get_setup_priority() const { return setup_priority::WIFI; }

bool OpenQuattDebugRecorder::lock_state_(TickType_t wait_ticks) const {
  return this->state_mutex_ != nullptr && xSemaphoreTake(this->state_mutex_, wait_ticks) == pdTRUE;
}

void OpenQuattDebugRecorder::unlock_state_() const { xSemaphoreGive(this->state_mutex_); }

bool OpenQuattDebugRecorder::begin_export_() const {
  if (!this->lock_state_()) {
    return false;
  }
  const bool can_start = this->available_() && !this->export_in_progress_;
  if (can_start) {
    this->export_in_progress_ = true;
  }
  this->unlock_state_();
  return can_start;
}

void OpenQuattDebugRecorder::end_export_() const {
  if (!this->lock_state_()) {
    return;
  }
  this->export_in_progress_ = false;
  this->unlock_state_();
}

bool OpenQuattDebugRecorder::time_is_valid_() const {
  return this->clock_ != nullptr && this->clock_->now().is_valid();
}

uint64_t OpenQuattDebugRecorder::current_time_ms_() const {
  if (this->time_is_valid_()) {
    return static_cast<uint64_t>(this->clock_->now().timestamp) * 1000ULL;
  }
  return static_cast<uint64_t>(millis());
}

uint64_t OpenQuattDebugRecorder::started_time_ms_() const {
  if (this->started_ms_ == 0) {
    return 0;
  }
  const uint32_t now_ms = millis();
  const uint64_t now_time_ms = this->current_time_ms_();
  const uint32_t elapsed_ms = now_ms - this->started_ms_;
  return now_time_ms >= elapsed_ms ? now_time_ms - elapsed_ms : 0;
}

uint64_t OpenQuattDebugRecorder::ended_time_ms_() const {
  if (this->started_ms_ == 0) {
    return 0;
  }
  if (this->active_) {
    return this->current_time_ms_();
  }
  return this->started_time_ms_() + static_cast<uint64_t>(this->stopped_ms_ - this->started_ms_);
}

uint32_t OpenQuattDebugRecorder::elapsed_s_() const {
  if (this->started_ms_ == 0) {
    return 0;
  }
  const uint32_t end_ms = this->active_ ? millis() : this->stopped_ms_;
  return (end_ms - this->started_ms_) / 1000U;
}

uint32_t OpenQuattDebugRecorder::remaining_s_() const {
  if (!this->active_ || this->rolling_) {
    return 0;
  }
  const uint32_t elapsed = this->elapsed_s_();
  return elapsed >= this->duration_s_ ? 0 : this->duration_s_ - elapsed;
}

uint32_t OpenQuattDebugRecorder::retained_duration_s_() const {
  if (this->count_ == 0) {
    return 0;
  }
  const uint8_t* first = this->sample_at_(0);
  const uint8_t* last = this->sample_at_(this->count_ - 1);
  const uint32_t first_offset = sample_offset_(first);
  const uint32_t last_offset = sample_offset_(last);
  return last_offset >= first_offset ? last_offset - first_offset : 0;
}

uint32_t OpenQuattDebugRecorder::retention_capacity_s_() const {
  return this->sample_capacity_ > 0 ? static_cast<uint32_t>(this->sample_capacity_ - 1) * (SAMPLE_INTERVAL_MS / 1000U)
                                    : 0;
}

uint32_t OpenQuattDebugRecorder::estimated_size_() const {
  uint64_t estimate = 2048U + static_cast<uint64_t>(this->field_count_) * 24U +
                      static_cast<uint64_t>(this->count_) * 16U +
                      static_cast<uint64_t>(this->total_change_count_) * 14U +
                      static_cast<uint64_t>(this->field_count_) * 12U + this->string_data_used_;
  return static_cast<uint32_t>(std::min<uint64_t>(estimate, std::numeric_limits<uint32_t>::max()));
}

uint32_t OpenQuattDebugRecorder::sanitize_duration_s_(uint32_t duration_s) const {
  if (duration_s == 0) {
    duration_s = DEFAULT_DURATION_S;
  }
  return std::max(MIN_DURATION_S, std::min(MAX_DURATION_S, duration_s));
}

uint8_t OpenQuattDebugRecorder::value_size_for_type_(FieldType type) {
  switch (type) {
    case FieldType::BINARY_SENSOR:
    case FieldType::SWITCH:
      return 1;
    case FieldType::TEXT_SENSOR:
    case FieldType::SELECT:
      return 2;
    default:
      return 4;
  }
}

bool OpenQuattDebugRecorder::event_type_(FieldType type) {
  return type == FieldType::BINARY_SENSOR || type == FieldType::SWITCH || type == FieldType::TEXT_SENSOR ||
         type == FieldType::SELECT;
}

uint32_t OpenQuattDebugRecorder::read_value_(const uint8_t* sample, const DebugField& field) {
  if (sample == nullptr) {
    return MISSING_VALUE;
  }
  const uint8_t* source = sample + field.value_offset;
  if (field.value_size == 1) {
    return *source == UINT8_MAX ? MISSING_VALUE : *source;
  }
  if (field.value_size == 2) {
    uint16_t value = 0;
    std::memcpy(&value, source, sizeof(value));
    return value == UINT16_MAX ? MISSING_VALUE : value;
  }
  uint32_t value = MISSING_VALUE;
  std::memcpy(&value, source, sizeof(value));
  return value;
}

void OpenQuattDebugRecorder::write_value_(uint8_t* sample, const DebugField& field, uint32_t value) {
  uint8_t* destination = sample + field.value_offset;
  if (field.value_size == 1) {
    *destination = value == MISSING_VALUE ? UINT8_MAX : static_cast<uint8_t>(value);
    return;
  }
  if (field.value_size == 2) {
    const uint16_t packed = value == MISSING_VALUE ? UINT16_MAX : static_cast<uint16_t>(value);
    std::memcpy(destination, &packed, sizeof(packed));
    return;
  }
  std::memcpy(destination, &value, sizeof(value));
}

uint32_t OpenQuattDebugRecorder::sample_offset_(const uint8_t* sample) {
  uint32_t value = 0;
  if (sample != nullptr) {
    std::memcpy(&value, sample, sizeof(value));
  }
  return value;
}

uint16_t OpenQuattDebugRecorder::sample_change_count_(const uint8_t* sample) {
  uint16_t value = 0;
  if (sample != nullptr) {
    std::memcpy(&value, sample + sizeof(uint32_t), sizeof(value));
  }
  return value;
}

uint16_t OpenQuattDebugRecorder::sample_event_count_(const uint8_t* sample) {
  uint16_t value = 0;
  if (sample != nullptr) {
    std::memcpy(&value, sample + sizeof(uint32_t) + sizeof(uint16_t), sizeof(value));
  }
  return value;
}

void OpenQuattDebugRecorder::write_sample_header_(uint8_t* sample, uint32_t offset_s, uint16_t change_count,
                                                  uint16_t event_count) {
  std::memcpy(sample, &offset_s, sizeof(offset_s));
  std::memcpy(sample + sizeof(offset_s), &change_count, sizeof(change_count));
  std::memcpy(sample + sizeof(offset_s) + sizeof(change_count), &event_count, sizeof(event_count));
}

uint8_t* OpenQuattDebugRecorder::writable_sample_at_(size_t physical_index) {
  if (!this->samples_ || this->sample_stride_ == 0 || physical_index >= this->sample_capacity_) {
    return nullptr;
  }
  return this->samples_.data() + physical_index * this->sample_stride_;
}

const uint8_t* OpenQuattDebugRecorder::sample_at_(size_t index) const {
  if (!this->samples_ || this->sample_stride_ == 0 || index >= this->count_) {
    return nullptr;
  }
  const size_t physical_index =
      this->count_ < this->sample_capacity_ ? index : (this->write_index_ + index) % this->sample_capacity_;
  return this->samples_.data() + physical_index * this->sample_stride_;
}

void OpenQuattDebugRecorder::clear_strings_() {
  for (size_t index = 0; index < STRING_ENTRY_CAPACITY; ++index) {
    this->string_entries_[index] = StringEntry{};
  }
  std::fill(this->string_buckets_.data(), this->string_buckets_.data() + STRING_BUCKET_CAPACITY, INVALID_STRING_INDEX);
  this->string_count_ = 0;
  this->string_data_used_ = 0;
  this->string_overflow_ = false;
}

void OpenQuattDebugRecorder::clear_() {
  this->count_ = 0;
  this->write_index_ = 0;
  this->last_sample_ms_ = 0;
  this->total_change_count_ = 0;
  this->total_event_count_ = 0;
  this->clear_strings_();
}

void OpenQuattDebugRecorder::abort_pending_configuration_() {
  this->configuration_pending_ = false;
  this->pending_field_count_ = 0;
  this->pending_requested_field_count_ = 0;
  this->pending_missing_field_count_ = 0;
}

bool OpenQuattDebugRecorder::compact_strings_() {
  if (!this->string_entries_ || !this->string_buckets_ || !this->string_compaction_order_ || !this->string_data_) {
    return false;
  }
  size_t live_count = 0;
  for (size_t index = 0; index < STRING_ENTRY_CAPACITY; ++index) {
    const StringEntry& entry = this->string_entries_[index];
    if (entry.length == 0 || entry.ref_count == 0) {
      continue;
    }
    if (entry.offset + entry.length > this->string_data_used_) {
      this->string_overflow_ = true;
      return false;
    }
    this->string_compaction_order_[live_count++] = static_cast<uint16_t>(index);
  }
  std::sort(this->string_compaction_order_.data(), this->string_compaction_order_.data() + live_count,
            [&](uint16_t left, uint16_t right) {
              return this->string_entries_[left].offset < this->string_entries_[right].offset;
            });

  size_t new_data_used = 0;
  for (size_t order_index = 0; order_index < live_count; ++order_index) {
    StringEntry& entry = this->string_entries_[this->string_compaction_order_[order_index]];
    if (entry.offset != new_data_used) {
      std::memmove(this->string_data_.data() + new_data_used, this->string_data_.data() + entry.offset, entry.length);
    }
    entry.offset = static_cast<uint32_t>(new_data_used);
    new_data_used += entry.length;
  }

  std::fill(this->string_buckets_.data(), this->string_buckets_.data() + STRING_BUCKET_CAPACITY, INVALID_STRING_INDEX);
  for (size_t index = 0; index < STRING_ENTRY_CAPACITY; ++index) {
    StringEntry& entry = this->string_entries_[index];
    if (entry.length == 0 || entry.ref_count == 0) {
      entry = StringEntry{};
      continue;
    }
    const size_t bucket = entry.hash % STRING_BUCKET_CAPACITY;
    entry.next_index = this->string_buckets_[bucket];
    this->string_buckets_[bucket] = static_cast<uint16_t>(index);
  }
  this->string_count_ = live_count;
  this->string_data_used_ = new_data_used;
  return true;
}

uint32_t OpenQuattDebugRecorder::intern_string_(const char* value, size_t length, bool preserve_unknown) {
  if (string_is_missing(value, length, preserve_unknown)) {
    return MISSING_VALUE;
  }
  if (length > UINT16_MAX) {
    this->string_overflow_ = true;
    return MISSING_VALUE;
  }
  const uint32_t hash = hash_string(value, length);
  const size_t bucket = hash % STRING_BUCKET_CAPACITY;
  auto find_existing = [&]() -> uint32_t {
    uint16_t index = this->string_buckets_[bucket];
    size_t visited = 0;
    while (index != INVALID_STRING_INDEX && visited++ < STRING_ENTRY_CAPACITY) {
      const StringEntry& entry = this->string_entries_[index];
      if (entry.hash == hash && entry.length == length && entry.offset + entry.length <= this->string_data_used_ &&
          std::memcmp(this->string_data_.data() + entry.offset, value, length) == 0) {
        return index;
      }
      index = entry.next_index;
    }
    return MISSING_VALUE;
  };

  uint32_t existing = find_existing();
  if (existing != MISSING_VALUE) {
    return existing;
  }

  if (this->string_count_ >= STRING_ENTRY_CAPACITY || this->string_data_used_ + length > STRING_DATA_BYTES) {
    if (!this->compact_strings_()) {
      this->string_overflow_ = true;
      return MISSING_VALUE;
    }
    existing = find_existing();
    if (existing != MISSING_VALUE) {
      return existing;
    }
  }

  size_t free_index = STRING_ENTRY_CAPACITY;
  for (size_t index = 0; index < STRING_ENTRY_CAPACITY; ++index) {
    if (this->string_entries_[index].length == 0) {
      free_index = index;
      break;
    }
  }
  if (free_index >= STRING_ENTRY_CAPACITY || this->string_data_used_ + length > STRING_DATA_BYTES) {
    this->string_overflow_ = true;
    return MISSING_VALUE;
  }

  StringEntry& entry = this->string_entries_[free_index];
  entry.hash = hash;
  entry.offset = static_cast<uint32_t>(this->string_data_used_);
  entry.ref_count = 0;
  entry.length = static_cast<uint16_t>(length);
  entry.next_index = this->string_buckets_[bucket];
  std::memcpy(this->string_data_.data() + this->string_data_used_, value, length);
  this->string_data_used_ += length;
  this->string_buckets_[bucket] = static_cast<uint16_t>(free_index);
  this->string_count_++;
  return static_cast<uint32_t>(free_index);
}

void OpenQuattDebugRecorder::retain_string_(uint32_t index) {
  if (index < STRING_ENTRY_CAPACITY && this->string_entries_[index].length > 0) {
    this->string_entries_[index].ref_count++;
  }
}

void OpenQuattDebugRecorder::release_sample_strings_(const uint8_t* sample) {
  if (sample == nullptr) {
    return;
  }
  for (size_t field_index = 0; field_index < this->field_count_; ++field_index) {
    const DebugField& field = this->fields_[field_index];
    if (field.type != FieldType::TEXT_SENSOR && field.type != FieldType::SELECT) {
      continue;
    }
    const uint32_t value = read_value_(sample, field);
    if (value < STRING_ENTRY_CAPACITY && this->string_entries_[value].ref_count > 0) {
      this->string_entries_[value].ref_count--;
    }
  }
}

uint32_t OpenQuattDebugRecorder::capture_value_(const DebugField& field) {
  switch (field.type) {
    case FieldType::SYSTEM_UPTIME_MS:
      return millis();
    case FieldType::SYSTEM_FREE_HEAP:
      return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    case FieldType::SYSTEM_FREE_PSRAM:
      return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    case FieldType::SYSTEM_MIN_FREE_HEAP:
      return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    case FieldType::SYSTEM_LARGEST_FREE_HEAP_BLOCK:
      return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
#ifdef USE_SENSOR
    case FieldType::SENSOR: {
      auto* entity = static_cast<sensor::Sensor*>(field.source);
      if (entity == nullptr || !entity->has_state() || !std::isfinite(entity->state)) return MISSING_VALUE;
      uint32_t value;
      std::memcpy(&value, &entity->state, sizeof(value));
      return value;
    }
#endif
#ifdef USE_NUMBER
    case FieldType::NUMBER: {
      auto* entity = static_cast<number::Number*>(field.source);
      if (entity == nullptr || !entity->has_state() || !std::isfinite(entity->state)) return MISSING_VALUE;
      uint32_t value;
      std::memcpy(&value, &entity->state, sizeof(value));
      return value;
    }
#endif
#ifdef USE_BINARY_SENSOR
    case FieldType::BINARY_SENSOR: {
      auto* entity = static_cast<binary_sensor::BinarySensor*>(field.source);
      return entity != nullptr && entity->has_state() ? static_cast<uint32_t>(entity->state) : MISSING_VALUE;
    }
#endif
#ifdef USE_SWITCH
    case FieldType::SWITCH: {
      auto* entity = static_cast<switch_::Switch*>(field.source);
      return entity != nullptr ? static_cast<uint32_t>(entity->state) : MISSING_VALUE;
    }
#endif
#ifdef USE_TEXT_SENSOR
    case FieldType::TEXT_SENSOR: {
      auto* entity = static_cast<text_sensor::TextSensor*>(field.source);
      if (entity == nullptr || !entity->has_state()) return MISSING_VALUE;
      const bool preserve_unknown =
          std::strcmp(field.key, "hp1Generation") == 0 || std::strcmp(field.key, "hp2Generation") == 0;
      return this->intern_string_(entity->state.data(), entity->state.size(), preserve_unknown);
    }
#endif
#ifdef USE_SELECT
    case FieldType::SELECT: {
      auto* entity = static_cast<select::Select*>(field.source);
      if (entity == nullptr || !entity->has_state()) return MISSING_VALUE;
      const StringRef value = entity->current_option();
      return this->intern_string_(value.c_str(), value.size());
    }
#endif
    default:
      return MISSING_VALUE;
  }
}

bool OpenQuattDebugRecorder::configure(const std::string& entities, bool reset) {
  if (!this->lock_state_()) {
    return false;
  }
  if (!this->available_() || this->active_) {
    this->unlock_state_();
    return false;
  }

  auto fail = [&]() {
    this->abort_pending_configuration_();
    this->unlock_state_();
    return false;
  };

  if (reset) {
    this->abort_pending_configuration_();
    this->configuration_pending_ = true;
    auto add_system_field = [&](const char* key, const char* unit, FieldType type) {
      DebugField& field = this->pending_fields_[this->pending_field_count_++];
      field = DebugField{};
      return copy_text(field.key, sizeof(field.key), key, std::strlen(key)) &&
             copy_text(field.name, sizeof(field.name), key, std::strlen(key)) &&
             copy_text(field.unit, sizeof(field.unit), unit, std::strlen(unit)) && (field.type = type, true);
    };
    if (!add_system_field("uptimeMs", "ms", FieldType::SYSTEM_UPTIME_MS) ||
        !add_system_field("freeHeap", "B", FieldType::SYSTEM_FREE_HEAP) ||
        !add_system_field("freePsram", "B", FieldType::SYSTEM_FREE_PSRAM) ||
        !add_system_field("minFreeHeap", "B", FieldType::SYSTEM_MIN_FREE_HEAP) ||
        !add_system_field("largestFreeHeapBlock", "B", FieldType::SYSTEM_LARGEST_FREE_HEAP_BLOCK)) {
      return fail();
    }
  } else if (!this->configuration_pending_) {
    return fail();
  }

  size_t start = 0;
  while (start < entities.size()) {
    const size_t end = entities.find('\n', start);
    const size_t line_end = end == std::string::npos ? entities.size() : end;
    const size_t first_tab = entities.find('\t', start);
    const size_t second_tab = first_tab == std::string::npos ? std::string::npos : entities.find('\t', first_tab + 1);
    if (first_tab == std::string::npos || second_tab == std::string::npos || second_tab >= line_end ||
        this->pending_requested_field_count_ >= FIELD_CAPACITY - SYSTEM_FIELD_COUNT) {
      return fail();
    }

    const size_t key_length = first_tab - start;
    const size_t domain_length = second_tab - first_tab - 1;
    const size_t name_length = line_end - second_tab - 1;
    if (key_length == 0 || domain_length == 0 || name_length == 0 || key_length >= FIELD_KEY_BYTES ||
        name_length >= FIELD_NAME_BYTES) {
      return fail();
    }

    const std::string domain = entities.substr(first_tab + 1, domain_length);
    DebugField field{};
    if (!copy_text(field.key, sizeof(field.key), entities.data() + start, key_length) ||
        !copy_text(field.name, sizeof(field.name), entities.data() + second_tab + 1, name_length)) {
      return fail();
    }
    for (size_t index = 0; index < this->pending_field_count_; ++index) {
      if (std::strcmp(this->pending_fields_[index].key, field.key) == 0) {
        return fail();
      }
    }

    if (domain == "sensor") {
      field.type = FieldType::SENSOR;
#ifdef USE_SENSOR
      field.source = find_entity_in(App.get_sensors(), field.name);
#endif
    } else if (domain == "number") {
      field.type = FieldType::NUMBER;
#ifdef USE_NUMBER
      field.source = find_entity_in(App.get_numbers(), field.name);
#endif
    } else if (domain == "binary_sensor") {
      field.type = FieldType::BINARY_SENSOR;
#ifdef USE_BINARY_SENSOR
      field.source = find_entity_in(App.get_binary_sensors(), field.name);
#endif
    } else if (domain == "switch") {
      field.type = FieldType::SWITCH;
#ifdef USE_SWITCH
      field.source = find_entity_in(App.get_switches(), field.name);
#endif
    } else if (domain == "text_sensor") {
      field.type = FieldType::TEXT_SENSOR;
#ifdef USE_TEXT_SENSOR
      field.source = find_entity_in(App.get_text_sensors(), field.name);
#endif
    } else if (domain == "select") {
      field.type = FieldType::SELECT;
#ifdef USE_SELECT
      field.source = find_entity_in(App.get_selects(), field.name);
#endif
    } else {
      return fail();
    }

    this->pending_requested_field_count_++;
    if (field.source == nullptr) {
      this->pending_missing_field_count_++;
    } else {
      if (field.type == FieldType::SENSOR) {
#ifdef USE_SENSOR
        const StringRef unit = static_cast<sensor::Sensor*>(field.source)->get_unit_of_measurement_ref();
        copy_text(field.unit, sizeof(field.unit), unit.c_str(), std::min(unit.size(), sizeof(field.unit) - 1));
#endif
      } else if (field.type == FieldType::NUMBER) {
#ifdef USE_NUMBER
        const StringRef unit = static_cast<number::Number*>(field.source)->get_unit_of_measurement_ref();
        copy_text(field.unit, sizeof(field.unit), unit.c_str(), std::min(unit.size(), sizeof(field.unit) - 1));
#endif
      }
      if (this->pending_field_count_ >= FIELD_CAPACITY) {
        return fail();
      }
      this->pending_fields_[this->pending_field_count_++] = field;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }

  this->unlock_state_();
  return true;
}

bool OpenQuattDebugRecorder::activate_pending_configuration_() {
  if (!this->configuration_pending_ || this->pending_field_count_ <= SYSTEM_FIELD_COUNT) {
    return false;
  }
  size_t value_offset = SAMPLE_HEADER_BYTES;
  for (size_t index = 0; index < this->pending_field_count_; ++index) {
    DebugField& field = this->pending_fields_[index];
    field.value_size = value_size_for_type_(field.type);
    if (value_offset + field.value_size > std::numeric_limits<uint16_t>::max()) {
      return false;
    }
    field.value_offset = static_cast<uint16_t>(value_offset);
    value_offset += field.value_size;
  }
  if (value_offset == 0 || value_offset > BUFFER_BYTES) {
    return false;
  }
  const size_t sample_capacity = BUFFER_BYTES / value_offset;
  if (sample_capacity < 2) {
    return false;
  }

  this->fields_.swap(this->pending_fields_);
  this->field_count_ = this->pending_field_count_;
  this->missing_field_count_ = this->pending_missing_field_count_;
  this->sample_stride_ = value_offset;
  this->sample_capacity_ = sample_capacity;
  this->abort_pending_configuration_();
  return true;
}

void OpenQuattDebugRecorder::capture_sample_() {
  if (!this->active_ || !this->samples_ || this->sample_capacity_ == 0) {
    return;
  }
  const uint32_t now_ms = millis();
  const uint8_t* previous = this->count_ > 0 ? this->sample_at_(this->count_ - 1) : nullptr;
  uint8_t* sample = this->writable_sample_at_(this->write_index_);
  if (sample == nullptr) {
    return;
  }

  if (this->count_ == this->sample_capacity_) {
    const uint32_t old_changes = sample_change_count_(sample);
    const uint32_t old_events = sample_event_count_(sample);
    this->total_change_count_ = old_changes > this->total_change_count_ ? 0 : this->total_change_count_ - old_changes;
    this->total_event_count_ = old_events > this->total_event_count_ ? 0 : this->total_event_count_ - old_events;
    uint8_t* next_oldest = this->writable_sample_at_((this->write_index_ + 1U) % this->sample_capacity_);
    if (next_oldest != nullptr) {
      const uint32_t next_changes = sample_change_count_(next_oldest);
      const uint32_t next_events = sample_event_count_(next_oldest);
      this->total_change_count_ =
          next_changes > this->total_change_count_ ? 0 : this->total_change_count_ - next_changes;
      this->total_event_count_ = next_events > this->total_event_count_ ? 0 : this->total_event_count_ - next_events;
      write_sample_header_(next_oldest, sample_offset_(next_oldest), 0, 0);
    }
    this->release_sample_strings_(sample);
  }

  uint16_t change_count = 0;
  uint16_t event_count = 0;
  for (size_t index = 0; index < this->field_count_; ++index) {
    const DebugField& field = this->fields_[index];
    const uint32_t value = this->capture_value_(field);
    write_value_(sample, field, value);
    if ((field.type == FieldType::TEXT_SENSOR || field.type == FieldType::SELECT) && value != MISSING_VALUE) {
      this->retain_string_(value);
    }
    if (previous != nullptr && read_value_(previous, field) != value) {
      change_count++;
      if (event_type_(field.type)) {
        event_count++;
      }
    }
  }
  write_sample_header_(sample, (now_ms - this->started_ms_) / 1000U, change_count, event_count);
  this->total_change_count_ += change_count;
  this->total_event_count_ += event_count;
  this->write_index_ = (this->write_index_ + 1) % this->sample_capacity_;
  if (this->count_ < this->sample_capacity_) {
    this->count_++;
  }
  this->last_sample_ms_ = now_ms;
}

bool OpenQuattDebugRecorder::start(uint32_t duration_s) {
  if (!this->lock_state_()) {
    return false;
  }
  if (!this->available_() || this->active_ || !this->activate_pending_configuration_()) {
    this->unlock_state_();
    ESP_LOGW(TAG, "Debug recording unavailable or configuration not committed");
    return false;
  }
  this->active_ = true;
  this->rolling_ = false;
  this->frozen_ = false;
  const uint64_t next_recording_id = this->current_time_ms_();
  this->recording_id_ = std::max(this->recording_id_ + 1U, next_recording_id);
  this->duration_s_ = this->sanitize_duration_s_(duration_s);
  this->started_ms_ = millis();
  this->stopped_ms_ = 0;
  this->clear_();
  this->capture_sample_();
  this->unlock_state_();
  return true;
}

bool OpenQuattDebugRecorder::start_rolling() {
  if (!this->lock_state_()) {
    return false;
  }
  if (!this->available_() || this->active_ || !this->activate_pending_configuration_()) {
    this->unlock_state_();
    ESP_LOGW(TAG, "Rolling debug recording unavailable or configuration not committed");
    return false;
  }
  this->active_ = true;
  this->rolling_ = true;
  this->frozen_ = false;
  const uint64_t next_recording_id = this->current_time_ms_();
  this->recording_id_ = std::max(this->recording_id_ + 1U, next_recording_id);
  this->duration_s_ = 0;
  this->started_ms_ = millis();
  this->stopped_ms_ = 0;
  this->clear_();
  this->capture_sample_();
  this->unlock_state_();
  return true;
}

void OpenQuattDebugRecorder::freeze() {
  if (!this->lock_state_()) {
    return;
  }
  if (this->active_) {
    this->active_ = false;
    this->frozen_ = this->rolling_;
    this->stopped_ms_ = millis();
  }
  this->unlock_state_();
}

void OpenQuattDebugRecorder::stop() {
  if (!this->lock_state_()) {
    return;
  }
  if (this->active_) {
    this->active_ = false;
    this->frozen_ = this->rolling_;
    this->stopped_ms_ = millis();
  }
  this->unlock_state_();
}

void OpenQuattDebugRecorder::rotate_csrf_token_() {
  std::array<uint8_t, 32> bytes{};
  for (size_t index = 0; index < bytes.size(); index += sizeof(uint32_t)) {
    const uint32_t random = esp_random();
    for (size_t byte_index = 0; byte_index < sizeof(uint32_t) && index + byte_index < bytes.size(); ++byte_index) {
      bytes[index + byte_index] = static_cast<uint8_t>(random >> (byte_index * 8U));
    }
  }
  this->csrf_token_ = base64_encode(bytes.data(), bytes.size());
}

void OpenQuattDebugRecorder::setup() {
  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    this->mark_failed();
    return;
  }
  this->state_mutex_ = xSemaphoreCreateMutexStatic(&this->state_mutex_storage_);
  if (this->state_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to initialize debug recorder synchronization");
    this->mark_failed();
    return;
  }

  const bool allocated = this->samples_.allocate_external(BUFFER_BYTES) &&
                         this->fields_.allocate_external(FIELD_CAPACITY) &&
                         this->pending_fields_.allocate_external(FIELD_CAPACITY) &&
                         this->string_entries_.allocate_external(STRING_ENTRY_CAPACITY) &&
                         this->string_buckets_.allocate_external(STRING_BUCKET_CAPACITY) &&
                         this->string_compaction_order_.allocate_external(STRING_ENTRY_CAPACITY) &&
                         this->string_data_.allocate_external(STRING_DATA_BYTES);
  if (!allocated || !this->available_()) {
    this->samples_.release();
    this->fields_.release();
    this->pending_fields_.release();
    this->string_entries_.release();
    this->string_buckets_.release();
    this->string_compaction_order_.release();
    this->string_data_.release();
    ESP_LOGE(TAG, "Failed to allocate debug recording storage in PSRAM");
  } else {
    this->clear_strings_();
  }
  this->rotate_csrf_token_();
  web_server_base::global_web_server_base->add_handler(new OpenQuattDebugRecorderRequestHandler(this));
}

void OpenQuattDebugRecorder::loop() {
  if (!this->lock_state_(0)) {
    return;
  }
  if (!this->active_) {
    this->unlock_state_();
    return;
  }
  const uint32_t now_ms = millis();
  if (!this->rolling_ && now_ms - this->started_ms_ >= this->duration_s_ * 1000U) {
    if (this->last_sample_ms_ != now_ms) {
      this->capture_sample_();
    }
    this->active_ = false;
    this->frozen_ = false;
    this->stopped_ms_ = now_ms;
  } else if (this->last_sample_ms_ == 0 || now_ms - this->last_sample_ms_ >= SAMPLE_INTERVAL_MS) {
    this->capture_sample_();
  }
  this->unlock_state_();
}

void OpenQuattDebugRecorder::dump_config() {
  if (!this->lock_state_()) {
    ESP_LOGCONFIG(TAG, "OpenQuatt debug recorder: synchronization unavailable");
    return;
  }
  ESP_LOGCONFIG(TAG, "OpenQuatt debug recorder");
  ESP_LOGCONFIG(TAG, "  Clock: %s", this->clock_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Fields: %u / %u", static_cast<unsigned>(this->field_count_),
                static_cast<unsigned>(FIELD_CAPACITY));
  ESP_LOGCONFIG(TAG, "  Samples: %u / %u", static_cast<unsigned>(this->count_),
                static_cast<unsigned>(this->sample_capacity_));
  ESP_LOGCONFIG(TAG, "  Packed row: %u bytes", static_cast<unsigned>(this->sample_stride_));
  ESP_LOGCONFIG(TAG, "  Sample arena: %s (%u bytes)", this->available_() ? "PSRAM" : "unavailable",
                static_cast<unsigned>(BUFFER_BYTES));
  this->unlock_state_();
}

bool OpenQuattDebugRecorder::capture_snapshot_(RecordingSnapshot* snapshot) const {
  if (snapshot == nullptr || !snapshot->allocate() || !this->lock_state_()) {
    return false;
  }
  if (!this->available_() || this->sample_stride_ == 0 ||
      this->count_ * this->sample_stride_ > snapshot->samples.size()) {
    this->unlock_state_();
    return false;
  }

  snapshot->available = true;
  snapshot->active = this->active_;
  snapshot->rolling = this->rolling_;
  snapshot->frozen = this->frozen_;
  snapshot->string_overflow = this->string_overflow_;
  snapshot->recording_id = this->recording_id_;
  snapshot->exported_at_ms = this->current_time_ms_();
  snapshot->started_at_ms = this->started_time_ms_();
  snapshot->ended_at_ms = this->ended_time_ms_();
  snapshot->duration_s = this->elapsed_s_();
  snapshot->retained_duration_s = this->retained_duration_s_();
  snapshot->retention_capacity_s = this->retention_capacity_s_();
  snapshot->event_count = this->total_event_count_;
  snapshot->count = this->count_;
  snapshot->sample_capacity = this->sample_capacity_;
  snapshot->sample_stride = this->sample_stride_;
  snapshot->field_count = this->field_count_;
  snapshot->missing_field_count = this->missing_field_count_;
  snapshot->string_data_used = this->string_data_used_;

  std::memcpy(snapshot->fields.data(), this->fields_.data(), this->field_count_ * sizeof(DebugField));
  std::memcpy(snapshot->string_entries.data(), this->string_entries_.data(),
              STRING_ENTRY_CAPACITY * sizeof(StringEntry));
  std::memcpy(snapshot->string_data.data(), this->string_data_.data(), this->string_data_used_);
  for (size_t index = 0; index < this->count_; ++index) {
    std::memcpy(snapshot->samples.data() + index * this->sample_stride_, this->sample_at_(index), this->sample_stride_);
  }
  this->unlock_state_();
  return true;
}

void OpenQuattDebugRecorder::write_status(httpd_req_t* req) const {
  struct Status {
    bool available{false};
    bool active{false};
    bool rolling{false};
    bool frozen{false};
    bool configuration_pending{false};
    bool string_overflow{false};
    uint64_t recording_id{0};
    uint32_t duration_s{0};
    uint32_t elapsed_s{0};
    uint32_t remaining_s{0};
    uint32_t retained_duration_s{0};
    uint32_t retention_capacity_s{0};
    uint32_t event_count{0};
    uint32_t estimated_size{0};
    size_t count{0};
    size_t sample_capacity{0};
    size_t sample_stride{0};
    size_t field_count{0};
    size_t missing_field_count{0};
    size_t pending_field_count{0};
    size_t pending_requested_field_count{0};
    size_t pending_missing_field_count{0};
    size_t string_count{0};
  } status;

  if (!this->lock_state_()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, R"({"ok":false,"available":false,"error":"synchronization_unavailable"})");
    return;
  }
  status.available = this->available_();
  status.active = this->active_;
  status.rolling = this->rolling_;
  status.frozen = this->frozen_;
  status.configuration_pending = this->configuration_pending_;
  status.string_overflow = this->string_overflow_;
  status.recording_id = this->recording_id_;
  status.duration_s = this->duration_s_;
  status.elapsed_s = this->elapsed_s_();
  status.remaining_s = this->remaining_s_();
  status.retained_duration_s = this->retained_duration_s_();
  status.retention_capacity_s = this->retention_capacity_s_();
  status.event_count = this->total_event_count_;
  status.estimated_size = this->estimated_size_();
  status.count = this->count_;
  status.sample_capacity = this->sample_capacity_;
  status.sample_stride = this->sample_stride_;
  status.field_count = this->field_count_;
  status.missing_field_count = this->missing_field_count_;
  status.pending_field_count = this->pending_field_count_;
  status.pending_requested_field_count = this->pending_requested_field_count_;
  status.pending_missing_field_count = this->pending_missing_field_count_;
  status.string_count = this->string_count_;
  this->unlock_state_();

  ChunkedJsonWriter writer(req);
  if (!writer.ready()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, R"({"ok":false,"available":false,"error":"response_buffer_unavailable"})");
    return;
  }
  httpd_resp_set_status(req, HTTPD_200);
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  constexpr size_t persistent_storage_bytes =
      BUFFER_BYTES + 2U * FIELD_CAPACITY * sizeof(DebugField) + STRING_ENTRY_CAPACITY * sizeof(StringEntry) +
      STRING_BUCKET_CAPACITY * sizeof(uint16_t) + STRING_ENTRY_CAPACITY * sizeof(uint16_t) + STRING_DATA_BYTES;
  const bool ok =
      writer.write_literal(R"({"ok":true,"available":)") && writer.write_bool(status.available) &&
      writer.write_literal(R"(,"active":)") && writer.write_bool(status.active) &&
      writer.write_literal(R"(,"mode":")") && writer.write_literal(status.rolling ? "rolling" : "manual") &&
      writer.write_literal(R"(","rolling":)") && writer.write_bool(status.rolling) &&
      writer.write_literal(R"(,"frozen":)") && writer.write_bool(status.frozen) &&
      writer.write_literal(R"(,"recording_id":)") && writer.write_uint64(status.recording_id) &&
      writer.write_literal(R"(,"storage":")") && writer.write_literal(status.available ? "psram" : "unavailable") &&
      writer.write_literal(R"(","interval_s":)") && writer.write_uint32(SAMPLE_INTERVAL_MS / 1000U) &&
      writer.write_literal(R"(,"duration_s":)") && writer.write_uint32(status.duration_s) &&
      writer.write_literal(R"(,"elapsed_s":)") && writer.write_uint32(status.elapsed_s) &&
      writer.write_literal(R"(,"remaining_s":)") && writer.write_uint32(status.remaining_s) &&
      writer.write_literal(R"(,"retained_duration_s":)") && writer.write_uint32(status.retained_duration_s) &&
      writer.write_literal(R"(,"retention_capacity_s":)") && writer.write_uint32(status.retention_capacity_s) &&
      writer.write_literal(R"(,"sample_count":)") && writer.write_uint32(static_cast<uint32_t>(status.count)) &&
      writer.write_literal(R"(,"sample_capacity":)") &&
      writer.write_uint32(static_cast<uint32_t>(status.sample_capacity)) &&
      writer.write_literal(R"(,"sample_row_bytes":)") &&
      writer.write_uint32(static_cast<uint32_t>(status.sample_stride)) && writer.write_literal(R"(,"field_count":)") &&
      writer.write_uint32(static_cast<uint32_t>(status.field_count)) &&
      writer.write_literal(R"(,"entity_field_count":)") &&
      writer.write_uint32(static_cast<uint32_t>(
          status.field_count > SYSTEM_FIELD_COUNT ? status.field_count - SYSTEM_FIELD_COUNT : 0)) &&
      writer.write_literal(R"(,"missing_field_count":)") &&
      writer.write_uint32(static_cast<uint32_t>(status.missing_field_count)) &&
      writer.write_literal(R"(,"configuration_pending":)") && writer.write_bool(status.configuration_pending) &&
      writer.write_literal(R"(,"pending_entity_field_count":)") &&
      writer.write_uint32(static_cast<uint32_t>(
          status.pending_field_count > SYSTEM_FIELD_COUNT ? status.pending_field_count - SYSTEM_FIELD_COUNT : 0)) &&
      writer.write_literal(R"(,"pending_requested_field_count":)") &&
      writer.write_uint32(static_cast<uint32_t>(status.pending_requested_field_count)) &&
      writer.write_literal(R"(,"pending_missing_field_count":)") &&
      writer.write_uint32(static_cast<uint32_t>(status.pending_missing_field_count)) &&
      writer.write_literal(R"(,"string_count":)") && writer.write_uint32(static_cast<uint32_t>(status.string_count)) &&
      writer.write_literal(R"(,"string_overflow":)") && writer.write_bool(status.string_overflow) &&
      writer.write_literal(R"(,"event_count":)") && writer.write_uint32(status.event_count) &&
      writer.write_literal(R"(,"buffer_size":)") && writer.write_uint32(static_cast<uint32_t>(BUFFER_BYTES)) &&
      writer.write_literal(R"(,"storage_size":)") &&
      writer.write_uint32(static_cast<uint32_t>(persistent_storage_bytes)) &&
      writer.write_literal(R"(,"estimated_size":)") && writer.write_uint32(status.estimated_size) &&
      writer.write_literal(R"(,"buffer":")") && writer.write_literal(status.available ? "psram" : "unavailable") &&
      writer.write_literal(R"(","csrf_token":)") &&
      writer.write_json_string(this->csrf_token_.c_str(), this->csrf_token_.size()) && writer.write_literal("}") &&
      writer.flush();
  if (!ok) {
    ESP_LOGW(TAG, "Failed to write debug recording status response");
  }
  httpd_resp_send_chunk(req, nullptr, 0);
}

void OpenQuattDebugRecorder::write_recording(httpd_req_t* req) const {
  if (!this->begin_export_()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, R"({"ok":false,"error":"snapshot_unavailable"})");
    return;
  }
  this->write_recording_export_(req);
  this->end_export_();
}

void OpenQuattDebugRecorder::write_recording_export_(httpd_req_t* req) const {
  RecordingSnapshot snapshot;
  if (!this->capture_snapshot_(&snapshot)) {
    ESP_LOGW(TAG, "Failed to allocate or capture immutable debug recording snapshot");
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, R"({"ok":false,"error":"snapshot_unavailable"})");
    return;
  }
  ChunkedJsonWriter writer(req);
  if (!writer.ready()) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, R"({"ok":false,"error":"response_buffer_unavailable"})");
    return;
  }
  httpd_resp_set_status(req, HTTPD_200);
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  const uint8_t* initial = snapshot.sample_at(0);
  auto write_field_value = [&](const DebugField& field, uint32_t value) -> bool {
    if (value == MISSING_VALUE) {
      return writer.write_literal("null");
    }
    switch (field.type) {
      case FieldType::SENSOR:
      case FieldType::NUMBER: {
        float numeric;
        std::memcpy(&numeric, &value, sizeof(numeric));
        return writer.write_float(numeric);
      }
      case FieldType::BINARY_SENSOR:
      case FieldType::SWITCH:
        return writer.write_bool(value != 0);
      case FieldType::TEXT_SENSOR:
      case FieldType::SELECT: {
        const StringEntry* entry = snapshot.string_at(value);
        return entry != nullptr && writer.write_json_string(snapshot.string_data.data() + entry->offset, entry->length);
      }
      default:
        return writer.write_uint32(value);
    }
  };

  bool ok = writer.write_literal(R"({"format":"openquatt-debug-device-v1","schema_version":1)") &&
            writer.write_literal(R"(,"kind":"openquatt_debug_recording","encoding":"device-psram-delta-json-v1")") &&
            writer.write_literal(R"(,"exported_at_ms":)") && writer.write_uint64(snapshot.exported_at_ms) &&
            writer.write_literal(R"(,"source":{"device":"OpenQuatt","storage":"psram"})") &&
            writer.write_literal(R"(,"recording":{"started_at_ms":)") && writer.write_uint64(snapshot.started_at_ms) &&
            writer.write_literal(R"(,"recording_id":)") && writer.write_uint64(snapshot.recording_id) &&
            writer.write_literal(R"(,"ended_at_ms":)") && writer.write_uint64(snapshot.ended_at_ms) &&
            writer.write_literal(R"(,"active":)") && writer.write_bool(snapshot.active) &&
            writer.write_literal(R"(,"mode":")") && writer.write_literal(snapshot.rolling ? "rolling" : "manual") &&
            writer.write_literal(R"(","rolling":)") && writer.write_bool(snapshot.rolling) &&
            writer.write_literal(R"(,"frozen":)") && writer.write_bool(snapshot.frozen) &&
            writer.write_literal(R"(,"duration_s":)") && writer.write_uint32(snapshot.duration_s) &&
            writer.write_literal(R"(,"retained_duration_s":)") && writer.write_uint32(snapshot.retained_duration_s) &&
            writer.write_literal(R"(,"retention_capacity_s":)") && writer.write_uint32(snapshot.retention_capacity_s) &&
            writer.write_literal(R"(,"interval_s":)") && writer.write_uint32(SAMPLE_INTERVAL_MS / 1000U) &&
            writer.write_literal(R"(,"sample_count":)") && writer.write_uint32(static_cast<uint32_t>(snapshot.count)) &&
            writer.write_literal(R"(,"sample_capacity":)") &&
            writer.write_uint32(static_cast<uint32_t>(snapshot.sample_capacity)) &&
            writer.write_literal(R"(,"sample_row_bytes":)") &&
            writer.write_uint32(static_cast<uint32_t>(snapshot.sample_stride)) &&
            writer.write_literal(R"(,"buffer_size":)") && writer.write_uint32(static_cast<uint32_t>(BUFFER_BYTES)) &&
            writer.write_literal(R"(,"column_count":)") &&
            writer.write_uint32(static_cast<uint32_t>(snapshot.field_count)) &&
            writer.write_literal(R"(,"missing_field_count":)") &&
            writer.write_uint32(static_cast<uint32_t>(snapshot.missing_field_count)) &&
            writer.write_literal(R"(,"event_count":)") && writer.write_uint32(snapshot.event_count) &&
            writer.write_literal(R"(,"string_overflow":)") && writer.write_bool(snapshot.string_overflow) &&
            writer.write_literal(R"(,"storage":"psram"},"columns":[)");
  if (!ok) {
    ESP_LOGW(TAG, "Failed to start debug recording response");
    httpd_resp_send_chunk(req, nullptr, 0);
    return;
  }

  for (size_t index = 0; index < snapshot.field_count; ++index) {
    if ((index > 0 && !writer.write_char(',')) ||
        !writer.write_json_string(snapshot.fields[index].key, std::strlen(snapshot.fields[index].key))) {
      ok = false;
      break;
    }
  }
  ok = ok && writer.write_literal(R"(],"units":[)");
  bool first_unit = true;
  for (size_t index = 0; ok && index < snapshot.field_count; ++index) {
    const DebugField& field = snapshot.fields[index];
    if (field.unit[0] == '\0') continue;
    ok = (first_unit || writer.write_char(',')) && writer.write_char('[') &&
         writer.write_uint32(static_cast<uint32_t>(index)) && writer.write_char(',') &&
         writer.write_json_string(field.unit, std::strlen(field.unit)) && writer.write_char(']');
    first_unit = false;
  }

  ok = ok && writer.write_literal(R"(],"initial":[)");
  bool first_initial = true;
  if (initial != nullptr) {
    for (size_t index = 0; ok && index < snapshot.field_count; ++index) {
      const uint32_t value = read_value_(initial, snapshot.fields[index]);
      if (value == MISSING_VALUE) continue;
      ok = (first_initial || writer.write_char(',')) && writer.write_char('[') &&
           writer.write_uint32(static_cast<uint32_t>(index)) && writer.write_char(',') &&
           write_field_value(snapshot.fields[index], value) && writer.write_char(']');
      first_initial = false;
    }
  }

  ok = ok && writer.write_literal(R"(],"samples":[)");
  for (size_t sample_index = 0; ok && sample_index < snapshot.count; ++sample_index) {
    const uint8_t* sample = snapshot.sample_at(sample_index);
    const uint8_t* previous = sample_index > 0 ? snapshot.sample_at(sample_index - 1) : initial;
    ok = (sample_index == 0 || writer.write_char(',')) && writer.write_char('[') &&
         writer.write_uint32(sample_offset_(sample)) && writer.write_literal(",[");
    bool first_delta = true;
    for (size_t field_index = 0; ok && previous != nullptr && field_index < snapshot.field_count; ++field_index) {
      const DebugField& field = snapshot.fields[field_index];
      const uint32_t value = read_value_(sample, field);
      if (value == read_value_(previous, field)) continue;
      ok = (first_delta || writer.write_char(',')) && writer.write_char('[') &&
           writer.write_uint32(static_cast<uint32_t>(field_index)) && writer.write_char(',') &&
           write_field_value(field, value) && writer.write_char(']');
      first_delta = false;
    }
    ok = ok && writer.write_literal("]]");
  }
  ok = ok && writer.write_literal(R"(],"events":[]})") && writer.flush();
  if (!ok) {
    ESP_LOGW(TAG, "Debug recording response ended before all snapshot data was written");
  }
  httpd_resp_send_chunk(req, nullptr, 0);
}

}  // namespace openquatt_debug_recorder
}  // namespace esphome
