#include "OpenQuattDebugRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include <esp_heap_caps.h>

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
  char* end = nullptr;
  const unsigned long value = std::strtoul(raw.c_str(), &end, 10);
  if (end == raw.c_str()) {
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
    if (url_path_matches(url_buf, "/openquatt/debug-recording/download")) {
      return request->method() == HTTP_GET;
    }
    return false;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);

    if (url_path_matches(url_buf, "/openquatt/debug-recording/configure")) {
      const std::string entities = request->arg("entities");
      if (entities.empty() || !this->parent_->configure(entities, parse_uint_arg(request, "reset", 0) != 0)) {
        request->send(409, "application/json", R"({"ok":false,"error":"configuration_failed"})");
        return;
      }
      this->send_status_(request);
      return;
    }

    if (url_path_matches(url_buf, "/openquatt/debug-recording/start")) {
      const bool rolling = parse_uint_arg(request, "rolling", 0) != 0;
      uint32_t duration_s = 0;
      const bool started = rolling ? this->parent_->start_rolling() : [&]() {
        duration_s = parse_uint_arg(request, "duration_s", 0);
        if (duration_s == 0) {
          duration_s = parse_uint_arg(request, "minutes", 15) * 60U;
        }
        return this->parent_->start(duration_s);
      }();
      if (!started) {
        request->send(409, "application/json", R"({"ok":false,"error":"recorder_not_configured"})");
        return;
      }
      this->send_status_(request);
      return;
    }

    if (url_path_matches(url_buf, "/openquatt/debug-recording/freeze")) {
      this->parent_->freeze();
      this->send_status_(request);
      return;
    }

    if (url_path_matches(url_buf, "/openquatt/debug-recording/stop")) {
      this->parent_->stop();
      this->send_status_(request);
      return;
    }

    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (url_path_matches(url_buf, "/openquatt/debug-recording/download")) {
      this->parent_->write_recording(req);
    } else {
      this->parent_->write_status(req);
    }
  }

 protected:
  void send_status_(AsyncWebServerRequest* request) const {
    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    this->parent_->write_status(req);
  }

  OpenQuattDebugRecorder* parent_;
};

}  // namespace

float OpenQuattDebugRecorder::get_setup_priority() const { return setup_priority::WIFI; }

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
  return now_time_ms >= static_cast<uint64_t>(now_ms - this->started_ms_)
             ? now_time_ms - static_cast<uint64_t>(now_ms - this->started_ms_)
             : 0;
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
  const DebugSample* first = this->sample_at_(0);
  const DebugSample* last = this->sample_at_(this->count_ - 1);
  return first != nullptr && last != nullptr && last->offset_s >= first->offset_s ? last->offset_s - first->offset_s
                                                                                  : 0;
}

uint32_t OpenQuattDebugRecorder::retention_capacity_s_() const {
  return SAMPLE_CAPACITY > 0 ? static_cast<uint32_t>(SAMPLE_CAPACITY - 1) * (SAMPLE_INTERVAL_MS / 1000U) : 0;
}

uint32_t OpenQuattDebugRecorder::sanitize_duration_s_(uint32_t duration_s) const {
  if (duration_s == 0) {
    duration_s = DEFAULT_DURATION_S;
  }
  return std::max(MIN_DURATION_S, std::min(MAX_DURATION_S, duration_s));
}

void OpenQuattDebugRecorder::clear_strings_() {
  this->string_count_ = 0;
  this->string_data_used_ = 0;
  this->string_overflow_ = false;
}

void OpenQuattDebugRecorder::clear_() {
  this->count_ = 0;
  this->write_index_ = 0;
  this->last_sample_ms_ = 0;
  this->capture_in_progress_ = false;
  this->capture_index_ = 0;
  this->clear_strings_();
}

bool OpenQuattDebugRecorder::compact_strings_() {
  if (!this->string_entries_ || !this->string_data_) {
    return false;
  }

  constexpr uint16_t MISSING_STRING_INDEX = UINT16_MAX;
  uint16_t remap[STRING_ENTRY_CAPACITY];
  std::fill(std::begin(remap), std::end(remap), MISSING_STRING_INDEX);

  // Rolling captures outlive stale text values; keep only strings still referenced by retained samples.
  auto uses_string = [](FieldType type) { return type == FieldType::TEXT_SENSOR || type == FieldType::SELECT; };
  auto mark_sample = [&](DebugSample& sample) {
    for (size_t field_index = 0; field_index < this->field_count_; ++field_index) {
      if (!uses_string(this->fields_[field_index].type)) continue;
      const uint32_t value = sample.values[field_index];
      if (value < this->string_count_) {
        remap[value] = 0;
      }
    }
  };

  for (size_t sample_index = 0; sample_index < this->count_; ++sample_index) {
    mark_sample(this->samples_[sample_index]);
  }
  if (this->capture_in_progress_ && this->capture_index_ < SAMPLE_CAPACITY && this->capture_index_ >= this->count_) {
    mark_sample(this->samples_[this->capture_index_]);
  }

  size_t new_count = 0;
  size_t new_data_used = 0;
  for (size_t old_index = 0; old_index < this->string_count_; ++old_index) {
    if (remap[old_index] == MISSING_STRING_INDEX) continue;
    const StringEntry old_entry = this->string_entries_[old_index];
    if (old_entry.length == 0 || old_entry.offset + old_entry.length > this->string_data_used_) {
      remap[old_index] = MISSING_STRING_INDEX;
      continue;
    }
    if (new_count >= STRING_ENTRY_CAPACITY || new_data_used + old_entry.length > STRING_DATA_BYTES) {
      return false;
    }
    if (old_entry.offset != new_data_used) {
      std::memmove(this->string_data_.data() + new_data_used, this->string_data_.data() + old_entry.offset,
                   old_entry.length);
    }
    StringEntry& new_entry = this->string_entries_[new_count];
    new_entry.hash = old_entry.hash;
    new_entry.offset = static_cast<uint32_t>(new_data_used);
    new_entry.length = old_entry.length;
    remap[old_index] = static_cast<uint16_t>(new_count);
    new_data_used += old_entry.length;
    new_count++;
  }

  auto remap_sample = [&](DebugSample& sample) {
    for (size_t field_index = 0; field_index < this->field_count_; ++field_index) {
      if (!uses_string(this->fields_[field_index].type)) continue;
      const uint32_t value = sample.values[field_index];
      if (value == MISSING_VALUE) continue;
      sample.values[field_index] =
          value < this->string_count_ && remap[value] != MISSING_STRING_INDEX ? remap[value] : MISSING_VALUE;
    }
  };

  for (size_t sample_index = 0; sample_index < this->count_; ++sample_index) {
    remap_sample(this->samples_[sample_index]);
  }
  if (this->capture_in_progress_ && this->capture_index_ < SAMPLE_CAPACITY && this->capture_index_ >= this->count_) {
    remap_sample(this->samples_[this->capture_index_]);
  }

  this->string_count_ = new_count;
  this->string_data_used_ = new_data_used;
  this->string_overflow_ = false;
  return true;
}

const OpenQuattDebugRecorder::DebugSample* OpenQuattDebugRecorder::sample_at_(size_t index) const {
  if (!this->samples_ || index >= this->count_) {
    return nullptr;
  }
  if (this->count_ < SAMPLE_CAPACITY) {
    return &this->samples_[index];
  }
  return &this->samples_[(this->write_index_ + index) % SAMPLE_CAPACITY];
}

const OpenQuattDebugRecorder::StringEntry* OpenQuattDebugRecorder::string_at_(uint32_t index) const {
  return this->string_entries_ && index < this->string_count_ ? &this->string_entries_[index] : nullptr;
}

uint32_t OpenQuattDebugRecorder::intern_string_(const char* value, size_t length, bool preserve_unknown) {
  if (string_is_missing(value, length, preserve_unknown)) {
    return MISSING_VALUE;
  }
  const uint32_t hash = hash_string(value, length);
  auto find_existing = [&]() -> uint32_t {
    for (size_t index = 0; index < this->string_count_; ++index) {
      const StringEntry& entry = this->string_entries_[index];
      if (entry.hash == hash && entry.length == length &&
          std::memcmp(this->string_data_.data() + entry.offset, value, length) == 0) {
        return static_cast<uint32_t>(index);
      }
    }
    return MISSING_VALUE;
  };

  uint32_t existing = find_existing();
  if (existing != MISSING_VALUE) {
    return existing;
  }
  if (this->string_count_ >= STRING_ENTRY_CAPACITY || length > UINT16_MAX ||
      this->string_data_used_ + length > STRING_DATA_BYTES) {
    if (this->rolling_ && this->compact_strings_()) {
      existing = find_existing();
      if (existing != MISSING_VALUE) {
        return existing;
      }
    }
  }
  if (this->string_count_ >= STRING_ENTRY_CAPACITY || length > UINT16_MAX ||
      this->string_data_used_ + length > STRING_DATA_BYTES) {
    this->string_overflow_ = true;
    return MISSING_VALUE;
  }
  StringEntry& entry = this->string_entries_[this->string_count_];
  entry.hash = hash;
  entry.offset = static_cast<uint32_t>(this->string_data_used_);
  entry.length = static_cast<uint16_t>(length);
  std::memcpy(this->string_data_.data() + this->string_data_used_, value, length);
  this->string_data_used_ += length;
  return static_cast<uint32_t>(this->string_count_++);
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
  if (!this->available_() || this->active_) {
    return false;
  }

  if (reset) {
    this->field_count_ = 0;
    this->missing_field_count_ = 0;
    auto add_system_field = [&](const char* key, const char* unit, FieldType type) {
      DebugField& field = this->fields_[this->field_count_++];
      field = DebugField{};
      copy_text(field.key, sizeof(field.key), key, std::strlen(key));
      copy_text(field.name, sizeof(field.name), key, std::strlen(key));
      copy_text(field.unit, sizeof(field.unit), unit, std::strlen(unit));
      field.type = type;
    };
    add_system_field("uptimeMs", "ms", FieldType::SYSTEM_UPTIME_MS);
    add_system_field("freeHeap", "B", FieldType::SYSTEM_FREE_HEAP);
    add_system_field("freePsram", "B", FieldType::SYSTEM_FREE_PSRAM);
    add_system_field("minFreeHeap", "B", FieldType::SYSTEM_MIN_FREE_HEAP);
  }

  size_t start = 0;
  while (start < entities.size()) {
    const size_t end = entities.find('\n', start);
    const size_t line_end = end == std::string::npos ? entities.size() : end;
    const size_t first_tab = entities.find('\t', start);
    const size_t second_tab = first_tab == std::string::npos ? std::string::npos : entities.find('\t', first_tab + 1);
    if (first_tab == std::string::npos || second_tab == std::string::npos || second_tab >= line_end ||
        this->field_count_ >= FIELD_CAPACITY) {
      return false;
    }

    const size_t key_length = first_tab - start;
    const size_t domain_length = second_tab - first_tab - 1;
    const size_t name_length = line_end - second_tab - 1;
    if (key_length == 0 || domain_length == 0 || name_length == 0 || key_length >= FIELD_KEY_BYTES ||
        name_length >= FIELD_NAME_BYTES) {
      return false;
    }

    const std::string domain = entities.substr(first_tab + 1, domain_length);
    DebugField field{};
    copy_text(field.key, sizeof(field.key), entities.data() + start, key_length);
    copy_text(field.name, sizeof(field.name), entities.data() + second_tab + 1, name_length);

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
      return false;
    }

    if (field.source == nullptr) {
      this->missing_field_count_++;
    } else if (field.type == FieldType::SENSOR) {
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
    this->fields_[this->field_count_++] = field;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}

void OpenQuattDebugRecorder::capture_sample_() {
  if (!this->active_ || !this->samples_) {
    return;
  }
  const uint32_t now_ms = millis();
  if (!this->rolling_ && this->elapsed_s_() >= this->duration_s_) {
    this->stop();
    return;
  }

  DebugSample& sample = this->samples_[this->write_index_];
  this->capture_in_progress_ = true;
  this->capture_index_ = this->write_index_;
  sample.offset_s = (now_ms - this->started_ms_) / 1000U;
  std::fill(std::begin(sample.values), std::end(sample.values), MISSING_VALUE);
  for (size_t index = 0; index < this->field_count_; ++index) {
    sample.values[index] = this->capture_value_(this->fields_[index]);
  }
  this->capture_in_progress_ = false;

  this->write_index_ = (this->write_index_ + 1) % SAMPLE_CAPACITY;
  if (this->count_ < SAMPLE_CAPACITY) {
    this->count_++;
  }
  this->last_sample_ms_ = now_ms;
}

bool OpenQuattDebugRecorder::start(uint32_t duration_s) {
  if (!this->available_() || this->field_count_ <= 4) {
    ESP_LOGW(TAG, "Debug recording unavailable or not configured");
    return false;
  }
  this->active_ = true;
  this->rolling_ = false;
  this->frozen_ = false;
  this->recording_id_ = this->current_time_ms_();
  this->duration_s_ = this->sanitize_duration_s_(duration_s);
  this->started_ms_ = millis();
  this->stopped_ms_ = 0;
  this->clear_();
  this->capture_sample_();
  return true;
}

bool OpenQuattDebugRecorder::start_rolling() {
  if (!this->available_() || this->field_count_ <= 4) {
    ESP_LOGW(TAG, "Rolling debug recording unavailable or not configured");
    return false;
  }
  this->active_ = true;
  this->rolling_ = true;
  this->frozen_ = false;
  this->recording_id_ = this->current_time_ms_();
  this->duration_s_ = 0;
  this->started_ms_ = millis();
  this->stopped_ms_ = 0;
  this->clear_();
  this->capture_sample_();
  return true;
}

void OpenQuattDebugRecorder::freeze() {
  if (!this->active_) {
    return;
  }
  this->active_ = false;
  this->frozen_ = this->rolling_;
  this->stopped_ms_ = millis();
}

void OpenQuattDebugRecorder::stop() {
  if (!this->active_) {
    return;
  }
  this->active_ = false;
  this->frozen_ = this->rolling_;
  this->stopped_ms_ = millis();
}

void OpenQuattDebugRecorder::setup() {
  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    return;
  }

  const bool allocated = this->samples_.allocate_external(SAMPLE_CAPACITY) &&
                         this->fields_.allocate_external(FIELD_CAPACITY) &&
                         this->string_entries_.allocate_external(STRING_ENTRY_CAPACITY) &&
                         this->string_data_.allocate_external(STRING_DATA_BYTES);
  if (!allocated || !this->available_()) {
    this->samples_.release();
    this->fields_.release();
    this->string_entries_.release();
    this->string_data_.release();
    ESP_LOGE(TAG, "Failed to allocate debug recording storage in PSRAM");
  }

  web_server_base::global_web_server_base->add_handler(new OpenQuattDebugRecorderRequestHandler(this));
}

void OpenQuattDebugRecorder::loop() {
  if (!this->active_) {
    return;
  }
  const uint32_t now_ms = millis();
  if (!this->rolling_ && now_ms - this->started_ms_ >= this->duration_s_ * 1000U) {
    this->stop();
    return;
  }
  if (this->last_sample_ms_ == 0 || now_ms - this->last_sample_ms_ >= SAMPLE_INTERVAL_MS) {
    this->capture_sample_();
  }
}

void OpenQuattDebugRecorder::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt debug recorder");
  ESP_LOGCONFIG(TAG, "  Clock: %s", this->clock_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Fields: %u / %u", static_cast<unsigned>(this->field_count_),
                static_cast<unsigned>(FIELD_CAPACITY));
  ESP_LOGCONFIG(TAG, "  Samples: %u / %u", static_cast<unsigned>(this->count_), static_cast<unsigned>(SAMPLE_CAPACITY));
  ESP_LOGCONFIG(TAG, "  Ring buffer: %s (%u bytes)", this->available_() ? "PSRAM" : "unavailable",
                static_cast<unsigned>(BUFFER_BYTES));
}

void OpenQuattDebugRecorder::write_status(httpd_req_t* req) const {
  ChunkedJsonWriter writer(req);
  const uint32_t estimated_size =
      2048U + static_cast<uint32_t>(this->count_) * (16U + static_cast<uint32_t>(this->field_count_) * 3U);
  if (!writer.write_literal(R"({"ok":true,"available":)") || !writer.write_bool(this->available_()) ||
      !writer.write_literal(R"(,"active":)") || !writer.write_bool(this->active_) ||
      !writer.write_literal(R"(,"mode":")") || !writer.write_literal(this->rolling_ ? "rolling" : "manual") ||
      !writer.write_literal(R"(","rolling":)") || !writer.write_bool(this->rolling_) ||
      !writer.write_literal(R"(,"frozen":)") || !writer.write_bool(this->frozen_) ||
      !writer.write_literal(R"(,"recording_id":)") || !writer.write_uint64(this->recording_id_) ||
      !writer.write_literal(R"(,"storage":")") || !writer.write_literal(this->available_() ? "psram" : "unavailable") ||
      !writer.write_literal(R"(","interval_s":)") || !writer.write_uint32(SAMPLE_INTERVAL_MS / 1000U) ||
      !writer.write_literal(R"(,"duration_s":)") || !writer.write_uint32(this->duration_s_) ||
      !writer.write_literal(R"(,"elapsed_s":)") || !writer.write_uint32(this->elapsed_s_()) ||
      !writer.write_literal(R"(,"remaining_s":)") || !writer.write_uint32(this->remaining_s_()) ||
      !writer.write_literal(R"(,"retained_duration_s":)") || !writer.write_uint32(this->retained_duration_s_()) ||
      !writer.write_literal(R"(,"retention_capacity_s":)") || !writer.write_uint32(this->retention_capacity_s_()) ||
      !writer.write_literal(R"(,"sample_count":)") || !writer.write_uint32(static_cast<uint32_t>(this->count_)) ||
      !writer.write_literal(R"(,"sample_capacity":)") || !writer.write_uint32(static_cast<uint32_t>(SAMPLE_CAPACITY)) ||
      !writer.write_literal(R"(,"field_count":)") || !writer.write_uint32(static_cast<uint32_t>(this->field_count_)) ||
      !writer.write_literal(R"(,"entity_field_count":)") ||
      !writer.write_uint32(static_cast<uint32_t>(this->field_count_ > 4 ? this->field_count_ - 4 : 0)) ||
      !writer.write_literal(R"(,"missing_field_count":)") ||
      !writer.write_uint32(static_cast<uint32_t>(this->missing_field_count_)) ||
      !writer.write_literal(R"(,"string_count":)") ||
      !writer.write_uint32(static_cast<uint32_t>(this->string_count_)) ||
      !writer.write_literal(R"(,"string_overflow":)") || !writer.write_bool(this->string_overflow_) ||
      !writer.write_literal(R"(,"buffer_size":)") || !writer.write_uint32(static_cast<uint32_t>(BUFFER_BYTES)) ||
      !writer.write_literal(R"(,"estimated_size":)") || !writer.write_uint32(estimated_size) ||
      !writer.write_literal(R"(,"buffer":")") || !writer.write_literal(this->available_() ? "psram" : "unavailable") ||
      !writer.write_literal(R"("})") || !writer.flush()) {
    httpd_resp_send_chunk(req, nullptr, 0);
    return;
  }
  httpd_resp_send_chunk(req, nullptr, 0);
}

void OpenQuattDebugRecorder::write_recording(httpd_req_t* req) const {
  ChunkedJsonWriter writer(req);
  const DebugSample* initial = this->sample_at_(0);

  auto write_value = [&](const DebugField& field, uint32_t value) -> bool {
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
        const StringEntry* entry = this->string_at_(value);
        return entry != nullptr && writer.write_json_string(this->string_data_.data() + entry->offset, entry->length);
      }
      default:
        return writer.write_uint32(value);
    }
  };

  if (!writer.write_literal(R"({"format":"openquatt-debug-device-v1","schema_version":1)") ||
      !writer.write_literal(R"(,"kind":"openquatt_debug_recording","encoding":"device-psram-delta-json-v1")") ||
      !writer.write_literal(R"(,"exported_at_ms":)") || !writer.write_uint64(this->current_time_ms_()) ||
      !writer.write_literal(R"(,"source":{"device":"OpenQuatt","storage":")") ||
      !writer.write_literal(this->available_() ? "psram" : "unavailable") || !writer.write_literal(R"("})") ||
      !writer.write_literal(R"(,"recording":{"started_at_ms":)") || !writer.write_uint64(this->started_time_ms_()) ||
      !writer.write_literal(R"(,"recording_id":)") || !writer.write_uint64(this->recording_id_) ||
      !writer.write_literal(R"(,"ended_at_ms":)") || !writer.write_uint64(this->ended_time_ms_()) ||
      !writer.write_literal(R"(,"active":)") || !writer.write_bool(this->active_) ||
      !writer.write_literal(R"(,"mode":")") || !writer.write_literal(this->rolling_ ? "rolling" : "manual") ||
      !writer.write_literal(R"(","rolling":)") || !writer.write_bool(this->rolling_) ||
      !writer.write_literal(R"(,"frozen":)") || !writer.write_bool(this->frozen_) ||
      !writer.write_literal(R"(,"duration_s":)") || !writer.write_uint32(this->elapsed_s_()) ||
      !writer.write_literal(R"(,"retained_duration_s":)") || !writer.write_uint32(this->retained_duration_s_()) ||
      !writer.write_literal(R"(,"retention_capacity_s":)") || !writer.write_uint32(this->retention_capacity_s_()) ||
      !writer.write_literal(R"(,"interval_s":)") || !writer.write_uint32(SAMPLE_INTERVAL_MS / 1000U) ||
      !writer.write_literal(R"(,"sample_count":)") || !writer.write_uint32(static_cast<uint32_t>(this->count_)) ||
      !writer.write_literal(R"(,"sample_capacity":)") || !writer.write_uint32(static_cast<uint32_t>(SAMPLE_CAPACITY)) ||
      !writer.write_literal(R"(,"buffer_size":)") || !writer.write_uint32(static_cast<uint32_t>(BUFFER_BYTES)) ||
      !writer.write_literal(R"(,"column_count":)") || !writer.write_uint32(static_cast<uint32_t>(this->field_count_)) ||
      !writer.write_literal(R"(,"storage":"psram"},"columns":[)")) {
    httpd_resp_send_chunk(req, nullptr, 0);
    return;
  }

  for (size_t index = 0; index < this->field_count_; ++index) {
    if ((index > 0 && !writer.write_char(',')) ||
        !writer.write_json_string(this->fields_[index].key, std::strlen(this->fields_[index].key))) {
      httpd_resp_send_chunk(req, nullptr, 0);
      return;
    }
  }
  if (!writer.write_literal(R"(],"units":[)")) {
    httpd_resp_send_chunk(req, nullptr, 0);
    return;
  }
  bool first_unit = true;
  for (size_t index = 0; index < this->field_count_; ++index) {
    const DebugField& field = this->fields_[index];
    if (field.unit[0] == '\0') continue;
    if ((!first_unit && !writer.write_char(',')) || !writer.write_char('[') ||
        !writer.write_uint32(static_cast<uint32_t>(index)) || !writer.write_char(',') ||
        !writer.write_json_string(field.unit, std::strlen(field.unit)) || !writer.write_char(']')) {
      httpd_resp_send_chunk(req, nullptr, 0);
      return;
    }
    first_unit = false;
  }

  if (!writer.write_literal(R"(],"initial":[)")) {
    httpd_resp_send_chunk(req, nullptr, 0);
    return;
  }
  bool first_initial = true;
  if (initial != nullptr) {
    for (size_t index = 0; index < this->field_count_; ++index) {
      if (initial->values[index] == MISSING_VALUE) continue;
      if ((!first_initial && !writer.write_char(',')) || !writer.write_char('[') ||
          !writer.write_uint32(static_cast<uint32_t>(index)) || !writer.write_char(',') ||
          !write_value(this->fields_[index], initial->values[index]) || !writer.write_char(']')) {
        httpd_resp_send_chunk(req, nullptr, 0);
        return;
      }
      first_initial = false;
    }
  }

  if (!writer.write_literal(R"(],"samples":[)")) {
    httpd_resp_send_chunk(req, nullptr, 0);
    return;
  }
  for (size_t sample_index = 0; sample_index < this->count_; ++sample_index) {
    const DebugSample* sample = this->sample_at_(sample_index);
    const DebugSample* previous = sample_index > 0 ? this->sample_at_(sample_index - 1) : initial;
    if (sample == nullptr || (sample_index > 0 && !writer.write_char(',')) || !writer.write_char('[') ||
        !writer.write_uint32(sample->offset_s) || !writer.write_literal(",[")) {
      httpd_resp_send_chunk(req, nullptr, 0);
      return;
    }
    bool first_delta = true;
    if (previous != nullptr) {
      for (size_t field_index = 0; field_index < this->field_count_; ++field_index) {
        if (sample->values[field_index] == previous->values[field_index]) continue;
        if ((!first_delta && !writer.write_char(',')) || !writer.write_char('[') ||
            !writer.write_uint32(static_cast<uint32_t>(field_index)) || !writer.write_char(',') ||
            !write_value(this->fields_[field_index], sample->values[field_index]) || !writer.write_char(']')) {
          httpd_resp_send_chunk(req, nullptr, 0);
          return;
        }
        first_delta = false;
      }
    }
    if (!writer.write_literal("]]")) {
      httpd_resp_send_chunk(req, nullptr, 0);
      return;
    }
  }

  if (!writer.write_literal(R"(],"events":[]})") || !writer.flush()) {
    httpd_resp_send_chunk(req, nullptr, 0);
    return;
  }
  httpd_resp_send_chunk(req, nullptr, 0);
}

}  // namespace openquatt_debug_recorder
}  // namespace esphome
