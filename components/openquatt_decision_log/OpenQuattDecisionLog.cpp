#include "OpenQuattDecisionLog.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_decision_log {
namespace {

static const char* const TAG = "openquatt.decision_log";
static constexpr uint32_t MIN_VALID_EPOCH_S = 1704067200UL;  // 2024-01-01 00:00:00 UTC
static constexpr uint32_t MAX_VALID_EPOCH_S = 2082758400UL;  // 2036-01-01 00:00:00 UTC

static bool epoch_is_sane(uint32_t epoch_s) { return epoch_s >= MIN_VALID_EPOCH_S && epoch_s < MAX_VALID_EPOCH_S; }

static bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) {
    return false;
  }
  const size_t path_len = std::strlen(path);
  return std::strncmp(url, path, path_len) == 0 && (url[path_len] == '\0' || url[path_len] == '?');
}

template <typename T>
T* allocate_external(size_t count) {
  if (count == 0) {
    return nullptr;
  }
  RAMAllocator<T> allocator(RAMAllocator<T>::ALLOC_EXTERNAL);
  T* data = allocator.allocate(count);
  if (data != nullptr) {
    std::memset(data, 0, sizeof(T) * count);
  }
  return data;
}

template <typename T>
T* allocate_internal(size_t count) {
  if (count == 0) {
    return nullptr;
  }
  RAMAllocator<T> allocator(RAMAllocator<T>::ALLOC_INTERNAL);
  T* data = allocator.allocate(count);
  if (data != nullptr) {
    std::memset(data, 0, sizeof(T) * count);
  }
  return data;
}

class ChunkedJsonWriter {
 public:
  explicit ChunkedJsonWriter(httpd_req_t* req) : req_(req) {}

  bool write_char(char c) { return this->write_bytes_(&c, 1); }

  bool write_literal(const char* text) {
    if (text == nullptr) {
      return true;
    }
    return this->write_bytes_(text, std::strlen(text));
  }

  bool write_string(const char* value) {
    if (!this->write_char('"')) {
      return false;
    }
    const char* cursor = value != nullptr ? value : "";
    while (*cursor != '\0') {
      const unsigned char c = static_cast<unsigned char>(*cursor++);
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
            char escaped[7];
            const int len = std::snprintf(escaped, sizeof(escaped), "\\u%04X", c);
            if (len < 0 || !this->write_bytes_(escaped, static_cast<size_t>(len))) return false;
          } else if (!this->write_char(static_cast<char>(c))) {
            return false;
          }
          break;
      }
    }
    return this->write_char('"');
  }

  bool write_uint32(uint32_t value) {
    char buffer[24];
    const int len = std::snprintf(buffer, sizeof(buffer), "%" PRIu32, value);
    return len >= 0 && this->write_bytes_(buffer, static_cast<size_t>(len));
  }

  bool write_uint64(uint64_t value) {
    char buffer[32];
    const int len = std::snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    return len >= 0 && this->write_bytes_(buffer, static_cast<size_t>(len));
  }

  bool write_int32(int32_t value) {
    char buffer[24];
    const int len = std::snprintf(buffer, sizeof(buffer), "%" PRId32, value);
    return len >= 0 && this->write_bytes_(buffer, static_cast<size_t>(len));
  }

  bool write_size(size_t value) {
    char buffer[24];
    const int len = std::snprintf(buffer, sizeof(buffer), "%zu", value);
    return len >= 0 && this->write_bytes_(buffer, static_cast<size_t>(len));
  }

  bool flush() {
    if (this->used_ == 0) {
      return true;
    }
    if (httpd_resp_send_chunk(this->req_, this->buffer_, static_cast<ssize_t>(this->used_)) != ESP_OK) {
      return false;
    }
    this->used_ = 0;
    return true;
  }

 private:
  static constexpr size_t BUFFER_SIZE = 384;

  bool write_bytes_(const char* data, size_t len) {
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
      std::memcpy(this->buffer_ + this->used_, cursor, to_copy);
      this->used_ += to_copy;
      cursor += to_copy;
      remaining -= to_copy;
    }
    return true;
  }

  httpd_req_t* req_;
  char buffer_[BUFFER_SIZE]{};
  size_t used_{0};
};

class OpenQuattDecisionLogRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattDecisionLogRequestHandler(OpenQuattDecisionLog* parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    return url_path_matches(url_buf, "/openquatt/decision-log") && request->method() == HTTP_GET;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const std::string meta_arg = request->arg("meta");
    if (meta_arg == "1" || meta_arg == "true") {
      this->parent_->write_metadata(req);
    } else {
      this->parent_->write_decision_log(req);
    }
  }

 protected:
  OpenQuattDecisionLog* parent_;
};

}  // namespace

OpenQuattDecisionLog::~OpenQuattDecisionLog() { this->release_buffers_(); }

float OpenQuattDecisionLog::get_setup_priority() const { return setup_priority::WIFI; }

void OpenQuattDecisionLog::setup() {
  this->allocate_buffers_();
  this->flash_partition_ =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "openquatt_data");
  if (!this->flash_partition_available_()) {
    ESP_LOGW(TAG, "Decision-log flash region unavailable");
  }
  this->flash_enabled_ = this->flash_switch_ != nullptr && this->flash_switch_->state;
  this->scan_flash_archive_();
  this->restore_flash_events_();
  this->initialize_current_hour_();
  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGW(TAG, "web_server_base is not available; decision log endpoint disabled");
  } else {
    web_server_base::global_web_server_base->add_handler(new OpenQuattDecisionLogRequestHandler(this));
  }
  this->emit(EVENT_BOOT_MARKER, SUBJECT_SYSTEM, REASON_UNKNOWN, SEVERITY_NORMAL, 0, STATE_UNKNOWN, STATE_ACTIVE);
}

void OpenQuattDecisionLog::loop() {
  if (!this->time_is_valid_()) {
    return;
  }

  this->process_urgent_flush_();

  const uint32_t epoch_hour = (this->current_epoch_s_() / 3600UL) * 3600UL;
  if (this->tracked_hour_start_epoch_s_ == 0) {
    this->tracked_hour_start_epoch_s_ = epoch_hour;
    this->initialize_current_hour_();
    return;
  }
  if (epoch_hour == this->tracked_hour_start_epoch_s_) {
    return;
  }

  uint32_t urgent_target_seq = 0U;
  bool urgent_target_active = false;
  portENTER_CRITICAL(&this->mux_);
  if (this->urgent_flush_.pending()) {
    urgent_target_active = true;
    urgent_target_seq = this->urgent_flush_.requested_event_seq();
  }
  portEXIT_CRITICAL(&this->mux_);
  bool urgent_target_persisted = false;
  if (this->flash_switch_enabled_() &&
      this->flush_pending_events_(SIZE_MAX, urgent_target_active, urgent_target_seq, &urgent_target_persisted) &&
      urgent_target_persisted) {
    this->complete_urgent_flush_(static_cast<uint64_t>(esp_timer_get_time()), urgent_target_seq);
  }
  this->tracked_hour_start_epoch_s_ = epoch_hour;
  this->initialize_current_hour_();
}

void OpenQuattDecisionLog::on_shutdown() { this->force_flush(); }

void OpenQuattDecisionLog::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt decision log:");
  ESP_LOGCONFIG(TAG, "  Events: %zu/%zu requested (%s)", this->event_capacity_, this->event_capacity_requested_,
                this->events_external_ ? "psram" : (this->events_ != nullptr ? "internal fallback" : "disabled"));
  ESP_LOGCONFIG(TAG, "  Hour buckets: %zu/%zu requested (%s)", this->bucket_capacity_, this->bucket_capacity_requested_,
                this->buckets_external_ ? "psram" : (this->buckets_ != nullptr ? "internal fallback" : "disabled"));
  ESP_LOGCONFIG(TAG, "  Record sizes: event=%zu bucket=%zu", sizeof(DecisionEvent), sizeof(HourBucket));
  ESP_LOGCONFIG(
      TAG, "  Flash archive: %s (%u/%u events, offset=0x%X, size=%u bytes)",
      this->flash_archive_available_() ? (this->flash_switch_enabled_() ? "enabled" : "disabled") : "unavailable",
      static_cast<unsigned>(this->flash_stored_event_count_), static_cast<unsigned>(FLASH_EVENT_CAPACITY),
      static_cast<unsigned>(FLASH_PARTITION_OFFSET), static_cast<unsigned>(FLASH_TOTAL_BYTES));
}

bool OpenQuattDecisionLog::time_is_valid_() const {
  if (this->clock_ == nullptr) {
    return false;
  }
  const auto now = this->clock_->now();
  return now.is_valid() && epoch_is_sane(static_cast<uint32_t>(now.timestamp));
}

uint32_t OpenQuattDecisionLog::current_epoch_s_() const {
  if (!this->time_is_valid_()) {
    return 0;
  }
  const auto now = this->clock_->now();
  return static_cast<uint32_t>(now.timestamp);
}

uint64_t OpenQuattDecisionLog::monotonic_uptime_s_() const {
  const int64_t uptime_us = esp_timer_get_time();
  return uptime_us > 0 ? static_cast<uint64_t>(uptime_us) / 1000000ULL : 0;
}

uint64_t OpenQuattDecisionLog::boot_epoch_s_() const {
  const uint64_t now_epoch_s = this->current_epoch_s_();
  if (now_epoch_s == 0) {
    return 0;
  }
  const uint64_t uptime_s = this->monotonic_uptime_s_();
  return now_epoch_s > uptime_s ? now_epoch_s - uptime_s : 0;
}

void OpenQuattDecisionLog::release_buffers_() {
  if (this->events_ != nullptr) {
    free(this->events_);  // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    this->events_ = nullptr;
  }
  if (this->buckets_ != nullptr) {
    free(this->buckets_);  // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    this->buckets_ = nullptr;
  }
  if (this->flash_index_ != nullptr) {
    free(this->flash_index_);  // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    this->flash_index_ = nullptr;
  }
  this->event_capacity_ = 0;
  this->bucket_capacity_ = 0;
  this->events_external_ = false;
  this->buckets_external_ = false;
  this->flash_index_count_ = 0;
}

void OpenQuattDecisionLog::allocate_buffers_() {
  this->release_buffers_();

  this->events_ = allocate_external<DecisionEvent>(this->event_capacity_requested_);
  if (this->events_ != nullptr) {
    this->event_capacity_ = this->event_capacity_requested_;
    this->events_external_ = true;
  } else {
    this->events_ = allocate_internal<DecisionEvent>(this->event_capacity_fallback_);
    this->event_capacity_ = this->events_ != nullptr ? this->event_capacity_fallback_ : 0;
    this->events_external_ = false;
  }

  this->buckets_ = allocate_external<HourBucket>(this->bucket_capacity_requested_);
  if (this->buckets_ != nullptr) {
    this->bucket_capacity_ = this->bucket_capacity_requested_;
    this->buckets_external_ = true;
  } else {
    this->buckets_ = allocate_internal<HourBucket>(this->bucket_capacity_fallback_);
    this->bucket_capacity_ = this->buckets_ != nullptr ? this->bucket_capacity_fallback_ : 0;
    this->buckets_external_ = false;
  }

  this->flash_index_ = allocate_external<FlashBlockInfo>(FLASH_SLOT_COUNT);
  if (this->flash_index_ == nullptr) {
    ESP_LOGW(TAG, "Failed to allocate decision-log flash index in PSRAM");
  }
}

void OpenQuattDecisionLog::emit(uint8_t event_type, uint8_t subject, uint8_t reason_code, uint8_t severity,
                                uint8_t control_mode_code, uint8_t from_state, uint8_t to_state, int16_t value_a,
                                int16_t value_b, int16_t threshold_a, uint16_t duration_s, uint8_t flags) {
  DecisionEvent event{};
  event.epoch_s = this->current_epoch_s_();
  event.uptime_s = this->monotonic_uptime_s_();
  event.duration_s = duration_s;
  event.value_a = value_a;
  event.value_b = value_b;
  event.threshold_a = threshold_a;
  event.event_type = event_type;
  event.subject = subject;
  event.reason_code = reason_code;
  event.severity = severity;
  event.control_mode_code = control_mode_code;
  event.from_state = from_state;
  event.to_state = to_state;
  event.flags = flags;

  bool recorded = false;
  portENTER_CRITICAL(&this->mux_);
  event.seq = this->next_seq_++;
  recorded = this->push_event_locked_(event);
  this->update_bucket_locked_(event);
  portEXIT_CRITICAL(&this->mux_);

  if (recorded && urgent_event_(event)) {
    this->request_urgent_flush_(event.seq);
  }
}

bool OpenQuattDecisionLog::urgent_event_(const DecisionEvent& event) {
  switch (event.event_type) {
    case EVENT_INCIDENT_START:
    case EVENT_INCIDENT_CLEAR:
      return event.severity == SEVERITY_FAULT;
    case EVENT_INCIDENT_ACKNOWLEDGED:
      return (event.flags & FLAG_MANUAL_RESET_REQUIRED) != 0U;
    case EVENT_HP_AVAILABILITY_CHANGE:
      return event.to_state == STATE_FAULTED || event.to_state == STATE_OFFLINE;
    case EVENT_CONTROL_MODE_CHANGE:
      return event.control_mode_code == 4 || event.value_a == 4 || event.value_b == 4;
    case EVENT_BOILER_FALLBACK_START:
    case EVENT_BOILER_FALLBACK_STOP:
      return true;
    default:
      return false;
  }
}

void OpenQuattDecisionLog::request_urgent_flush_(uint32_t event_seq) {
  if (!this->flash_switch_enabled_()) {
    return;
  }
  const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
  portENTER_CRITICAL(&this->mux_);
  this->urgent_flush_.request(now_us, event_seq);
  portEXIT_CRITICAL(&this->mux_);
}

void OpenQuattDecisionLog::clear_urgent_flush_(uint64_t now_us) {
  portENTER_CRITICAL(&this->mux_);
  this->urgent_flush_.clear(now_us);
  portEXIT_CRITICAL(&this->mux_);
}

void OpenQuattDecisionLog::complete_urgent_flush_(uint64_t now_us, uint32_t persisted_target_seq) {
  portENTER_CRITICAL(&this->mux_);
  this->urgent_flush_.mark_target_persisted(now_us, persisted_target_seq);
  portEXIT_CRITICAL(&this->mux_);
}

void OpenQuattDecisionLog::process_urgent_flush_() {
  const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
  bool should_attempt = false;
  uint32_t urgent_target_seq = 0U;
  portENTER_CRITICAL(&this->mux_);
  should_attempt = this->urgent_flush_.should_attempt(now_us, URGENT_FLUSH_COALESCE_US, URGENT_FLUSH_MIN_INTERVAL_US);
  if (should_attempt) {
    this->urgent_flush_.mark_attempt(now_us);
    urgent_target_seq = this->urgent_flush_.requested_event_seq();
  }
  portEXIT_CRITICAL(&this->mux_);

  if (!should_attempt) {
    return;
  }
  bool urgent_target_persisted = false;
  if (this->flush_pending_events_(URGENT_FLUSH_MAX_BATCHES, true, urgent_target_seq, &urgent_target_persisted) &&
      urgent_target_persisted) {
    this->complete_urgent_flush_(now_us, urgent_target_seq);
    return;
  }

  portENTER_CRITICAL(&this->mux_);
  this->urgent_flush_.mark_failure(now_us, URGENT_FLUSH_RETRY_US);
  portEXIT_CRITICAL(&this->mux_);
}

bool OpenQuattDecisionLog::push_event_locked_(const DecisionEvent& event) {
  if (this->events_ == nullptr || this->event_capacity_ == 0) {
    this->dropped_count_ = this->dropped_count_ < UINT32_MAX ? this->dropped_count_ + 1 : UINT32_MAX;
    return false;
  }
  if (this->event_count_ == this->event_capacity_ &&
      this->urgent_flush_.protects_unpersisted_sequence(this->events_[this->event_head_].seq,
                                                        this->last_persisted_event_seq_)) {
    // Preserve the complete RAM interval through the newest urgent target.
    // Dropping a later event is safer than silently overwriting a safety edge
    // that the persistence policy still promises to flush.
    this->dropped_count_ = this->dropped_count_ < UINT32_MAX ? this->dropped_count_ + 1U : UINT32_MAX;
    return false;
  }

  const size_t insert_index = (this->event_head_ + this->event_count_) % this->event_capacity_;
  this->events_[insert_index] = event;
  if (this->event_count_ < this->event_capacity_) {
    ++this->event_count_;
  } else {
    this->event_head_ = (this->event_head_ + 1) % this->event_capacity_;
    this->dropped_count_ = this->dropped_count_ < UINT32_MAX ? this->dropped_count_ + 1 : UINT32_MAX;
  }
  return true;
}

HourBucket* OpenQuattDecisionLog::current_bucket_locked_(uint64_t uptime_s, uint32_t epoch_s, bool* created) {
  if (created != nullptr) {
    *created = false;
  }
  if (this->buckets_ == nullptr || this->bucket_capacity_ == 0) {
    return nullptr;
  }
  const bool has_epoch = epoch_is_sane(epoch_s);
  const uint32_t epoch_hour = has_epoch ? (epoch_s / 3600UL) * 3600UL : 0;
  const uint64_t uptime_hour = (uptime_s / 3600ULL) * 3600ULL;
  const size_t index = has_epoch ? static_cast<size_t>((epoch_hour / 3600UL) % this->bucket_capacity_)
                                 : static_cast<size_t>((uptime_s / 3600ULL) % this->bucket_capacity_);
  HourBucket* bucket = &this->buckets_[index];
  if (!bucket->valid ||
      (has_epoch ? bucket->hour_start_epoch_s != epoch_hour : bucket->hour_start_uptime_s != uptime_hour)) {
    std::memset(bucket, 0, sizeof(*bucket));
    if (has_epoch) {
      const uint32_t elapsed_in_hour_s = epoch_s - epoch_hour;
      bucket->hour_start_uptime_s = uptime_s >= elapsed_in_hour_s ? uptime_s - elapsed_in_hour_s : 0;
      bucket->hour_start_epoch_s = epoch_hour;
    } else {
      bucket->hour_start_uptime_s = uptime_hour;
    }
    bucket->valid = true;
    if (created != nullptr) {
      *created = true;
    }
  }
  return bucket;
}

uint16_t OpenQuattDecisionLog::increment_u16_(uint16_t value) {
  return value < UINT16_MAX ? static_cast<uint16_t>(value + 1) : UINT16_MAX;
}

void OpenQuattDecisionLog::update_bucket_locked_(const DecisionEvent& event) {
  HourBucket* bucket = this->current_bucket_locked_(event.uptime_s, event.epoch_s);
  if (bucket == nullptr) {
    return;
  }

  if (event.severity == SEVERITY_ATTENTION || event.event_type == EVENT_ATTENTION_PATTERN) {
    bucket->attention_count = increment_u16_(bucket->attention_count);
  }

  switch (event.event_type) {
    case EVENT_SOURCE_START:
      if (event.subject == SUBJECT_HP1)
        bucket->starts_hp1 = increment_u16_(bucket->starts_hp1);
      else if (event.subject == SUBJECT_HP2)
        bucket->starts_hp2 = increment_u16_(bucket->starts_hp2);
      break;
    case EVENT_SOURCE_STOP:
      if (event.subject == SUBJECT_HP1)
        bucket->stops_hp1 = increment_u16_(bucket->stops_hp1);
      else if (event.subject == SUBJECT_HP2)
        bucket->stops_hp2 = increment_u16_(bucket->stops_hp2);
      break;
    case EVENT_TOPOLOGY_CHANGE:
      if (event.to_state == STATE_SINGLE)
        bucket->topology_single_count = increment_u16_(bucket->topology_single_count);
      else if (event.to_state == STATE_DUO)
        bucket->topology_duo_count = increment_u16_(bucket->topology_duo_count);
      break;
    case EVENT_BOILER_ASSIST_START:
      bucket->cv_assist_start_count = increment_u16_(bucket->cv_assist_start_count);
      break;
    case EVENT_BOILER_ASSIST_STOP:
      bucket->cv_assist_stop_count = increment_u16_(bucket->cv_assist_stop_count);
      break;
    case EVENT_COOLING_LIMITED:
      bucket->cooling_limited_count = increment_u16_(bucket->cooling_limited_count);
      if (event.reason_code == REASON_DEW_STOP)
        bucket->dewpoint_stop_count = increment_u16_(bucket->dewpoint_stop_count);
      break;
    case EVENT_COOLING_RELEASED:
      bucket->cooling_released_count = increment_u16_(bucket->cooling_released_count);
      break;
    case EVENT_STICKY_PUMP_RUN:
      bucket->sticky_run_count = increment_u16_(bucket->sticky_run_count);
      break;
    case EVENT_DEFROST_SEEN_START:
      if (event.subject == SUBJECT_HP1)
        bucket->defrost_seen_count_hp1 = increment_u16_(bucket->defrost_seen_count_hp1);
      else if (event.subject == SUBJECT_HP2)
        bucket->defrost_seen_count_hp2 = increment_u16_(bucket->defrost_seen_count_hp2);
      break;
    default:
      break;
  }

  if (event.event_type == EVENT_DECISION_HOLD && event.reason_code == REASON_DEFROST_HOLD) {
    if (event.subject == SUBJECT_HP1)
      bucket->defrost_hold_count_hp1 = increment_u16_(bucket->defrost_hold_count_hp1);
    else if (event.subject == SUBJECT_HP2)
      bucket->defrost_hold_count_hp2 = increment_u16_(bucket->defrost_hold_count_hp2);
  } else if (event.reason_code == REASON_DEFROST_BOOST) {
    if (event.subject == SUBJECT_HP1)
      bucket->defrost_boost_count_hp1 = increment_u16_(bucket->defrost_boost_count_hp1);
    else if (event.subject == SUBJECT_HP2)
      bucket->defrost_boost_count_hp2 = increment_u16_(bucket->defrost_boost_count_hp2);
  }
}

uint32_t OpenQuattDecisionLog::fnv1a_hash_(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index < len; ++index) {
    hash ^= data[index];
    hash *= 16777619u;
  }
  return hash;
}

OpenQuattDecisionLog::FlashEventRecord OpenQuattDecisionLog::pack_flash_event_(const DecisionEvent& event) {
  FlashEventRecord record{};
  record.seq = event.seq;
  record.epoch_s = event.epoch_s;
  record.value_a = event.value_a;
  record.value_b = event.value_b;
  record.threshold_a = event.threshold_a;
  record.duration_s = event.duration_s;
  record.event_type = event.event_type;
  record.subject = event.subject;
  record.reason_code = event.reason_code;
  record.severity = event.severity;
  record.control_mode_code = event.control_mode_code;
  record.from_state = event.from_state;
  record.to_state = event.to_state;
  record.flags = event.flags;
  return record;
}

DecisionEvent OpenQuattDecisionLog::unpack_flash_event_(const FlashEventRecord& record) {
  DecisionEvent event{};
  event.seq = record.seq;
  event.epoch_s = record.epoch_s;
  event.value_a = record.value_a;
  event.value_b = record.value_b;
  event.threshold_a = record.threshold_a;
  event.duration_s = record.duration_s;
  event.event_type = record.event_type;
  event.subject = record.subject;
  event.reason_code = record.reason_code;
  event.severity = record.severity;
  event.control_mode_code = record.control_mode_code;
  event.from_state = record.from_state;
  event.to_state = record.to_state;
  event.flags = record.flags;
  return event;
}

bool OpenQuattDecisionLog::flash_switch_enabled_() const {
  return this->flash_switch_ != nullptr ? this->flash_switch_->state : this->flash_enabled_;
}

bool OpenQuattDecisionLog::flash_partition_available_() const {
  return this->flash_partition_ != nullptr &&
         this->flash_partition_->size >= FLASH_PARTITION_OFFSET + FLASH_TOTAL_BYTES;
}

bool OpenQuattDecisionLog::flash_archive_available_() const {
  return this->flash_partition_available_() && this->flash_index_ != nullptr;
}

void OpenQuattDecisionLog::reset_flash_metadata_() {
  this->flash_index_count_ = 0;
  this->next_flash_sequence_ = 0;
  this->flash_oldest_epoch_s_ = 0;
  this->flash_newest_epoch_s_ = 0;
  this->flash_last_flush_epoch_s_ = 0;
  this->flash_stored_event_count_ = 0;
}

bool OpenQuattDecisionLog::read_flash_block_(uint32_t slot_index, uint32_t expected_sequence, FlashBlockInfo* info,
                                             FlashEventRecord* events) const {
  if (info == nullptr || events == nullptr || !this->flash_partition_available_() || slot_index >= FLASH_SLOT_COUNT) {
    return false;
  }

  FlashBlockHeader header{};
  const size_t slot_offset = FLASH_PARTITION_OFFSET + static_cast<size_t>(slot_index) * FLASH_SLOT_SIZE;
  if (esp_partition_read(this->flash_partition_, slot_offset, &header, sizeof(header)) != ESP_OK ||
      header.magic != FLASH_TAG_MAGIC || header.version != FLASH_TAG_VERSION || header.event_count == 0 ||
      header.event_count > FLASH_EVENTS_PER_SLOT || !epoch_is_sane(header.first_epoch_s) ||
      !epoch_is_sane(header.last_epoch_s) || header.first_epoch_s > header.last_epoch_s ||
      (expected_sequence != UINT32_MAX && header.block_sequence != expected_sequence)) {
    return false;
  }
  const size_t payload_bytes = static_cast<size_t>(header.event_count) * sizeof(FlashEventRecord);
  if (esp_partition_read(this->flash_partition_, slot_offset + sizeof(header), events, payload_bytes) != ESP_OK ||
      events[0].seq != header.first_event_seq ||
      fnv1a_hash_(reinterpret_cast<const uint8_t*>(events), payload_bytes) != header.crc32) {
    return false;
  }
  uint32_t first_epoch_s = UINT32_MAX;
  uint32_t last_epoch_s = 0;
  for (size_t index = 0; index < header.event_count; ++index) {
    if (!epoch_is_sane(events[index].epoch_s) || (index > 0 && events[index].seq <= events[index - 1U].seq)) {
      return false;
    }
    first_epoch_s = std::min(first_epoch_s, events[index].epoch_s);
    last_epoch_s = std::max(last_epoch_s, events[index].epoch_s);
  }
  if (first_epoch_s != header.first_epoch_s || last_epoch_s != header.last_epoch_s) {
    return false;
  }

  info->block_sequence = header.block_sequence;
  info->write_epoch_s = header.write_epoch_s;
  info->first_event_seq = header.first_event_seq;
  info->first_epoch_s = header.first_epoch_s;
  info->last_epoch_s = header.last_epoch_s;
  info->event_count = header.event_count;
  info->slot_index = slot_index;
  return true;
}

void OpenQuattDecisionLog::rebuild_flash_metadata_() {
  this->flash_oldest_epoch_s_ = 0;
  this->flash_newest_epoch_s_ = 0;
  this->flash_last_flush_epoch_s_ = 0;
  this->flash_stored_event_count_ = 0;
  const uint32_t now_epoch_s = this->current_epoch_s_();
  const uint32_t cutoff_epoch_s = now_epoch_s > RETENTION_SECONDS ? now_epoch_s - RETENTION_SECONDS : 0;
  for (size_t index = 0; index < this->flash_index_count_; ++index) {
    const FlashBlockInfo& info = this->flash_index_[index];
    if (cutoff_epoch_s > 0 && info.last_epoch_s < cutoff_epoch_s) {
      continue;
    }
    std::array<FlashEventRecord, FLASH_EVENTS_PER_SLOT> records{};
    FlashBlockInfo verified{};
    if (!this->read_flash_block_(info.slot_index, info.block_sequence, &verified, records.data())) {
      continue;
    }
    for (size_t event_index = 0; event_index < verified.event_count; ++event_index) {
      const uint32_t epoch_s = records[event_index].epoch_s;
      if (cutoff_epoch_s > 0 && epoch_s < cutoff_epoch_s) {
        continue;
      }
      ++this->flash_stored_event_count_;
      if (this->flash_oldest_epoch_s_ == 0 || epoch_s < this->flash_oldest_epoch_s_) {
        this->flash_oldest_epoch_s_ = epoch_s;
      }
      if (epoch_s >= this->flash_newest_epoch_s_) {
        this->flash_newest_epoch_s_ = epoch_s;
      }
    }
    if (info.write_epoch_s >= this->flash_last_flush_epoch_s_) {
      this->flash_last_flush_epoch_s_ = info.write_epoch_s;
    }
  }
}

bool OpenQuattDecisionLog::scan_flash_archive_() {
  this->flash_archive_scanned_ = false;
  portENTER_CRITICAL(&this->mux_);
  this->flash_index_count_ = 0;
  portEXIT_CRITICAL(&this->mux_);
  if (!this->flash_partition_available_() || this->flash_index_ == nullptr) {
    this->reset_flash_metadata_();
    return false;
  }

  size_t found_count = 0;
  uint32_t highest_sequence = 0;
  uint32_t highest_event_seq = 0;
  bool any_valid = false;
  for (uint32_t slot = 0; slot < FLASH_SLOT_COUNT; ++slot) {
    FlashBlockInfo info{};
    std::array<FlashEventRecord, FLASH_EVENTS_PER_SLOT> events{};
    if (!this->read_flash_block_(slot, UINT32_MAX, &info, events.data())) {
      continue;
    }
    this->flash_index_[found_count++] = info;
    if (!any_valid || info.block_sequence > highest_sequence) {
      highest_sequence = info.block_sequence;
    }
    highest_event_seq = std::max(highest_event_seq, events[info.event_count - 1U].seq);
    any_valid = true;
  }

  std::sort(this->flash_index_, this->flash_index_ + found_count,
            [](const FlashBlockInfo& left, const FlashBlockInfo& right) {
              return left.block_sequence < right.block_sequence;
            });
  portENTER_CRITICAL(&this->mux_);
  this->flash_index_count_ = found_count;
  portEXIT_CRITICAL(&this->mux_);
  this->next_flash_sequence_ = any_valid ? highest_sequence + 1U : 0U;
  this->last_persisted_event_seq_ = highest_event_seq;
  if (any_valid && highest_event_seq >= this->next_seq_) {
    this->next_seq_ = highest_event_seq + 1U;
  }
  this->rebuild_flash_metadata_();
  this->flash_archive_scanned_ = true;
  return any_valid;
}

bool OpenQuattDecisionLog::write_flash_events_(const DecisionEvent* events, size_t event_count) {
  if (!this->flash_switch_enabled_() || !this->flash_archive_available_() || events == nullptr || event_count == 0 ||
      event_count > FLASH_EVENTS_PER_SLOT) {
    return false;
  }

  const uint32_t sequence = this->next_flash_sequence_;
  const uint32_t slot_index = sequence % FLASH_SLOT_COUNT;
  const size_t slot_offset = FLASH_PARTITION_OFFSET + static_cast<size_t>(slot_index) * FLASH_SLOT_SIZE;
  if ((slot_index % FLASH_SLOTS_PER_SECTOR) == 0) {
    const esp_err_t erase_result = esp_partition_erase_range(this->flash_partition_, slot_offset, FLASH_SECTOR_SIZE);
    if (erase_result != ESP_OK) {
      ESP_LOGW(TAG, "Could not erase decision-log flash sector: %s", esp_err_to_name(erase_result));
      return false;
    }
  }

  std::array<FlashEventRecord, FLASH_EVENTS_PER_SLOT> records{};
  for (size_t index = 0; index < event_count; ++index) {
    if (!epoch_is_sane(events[index].epoch_s)) {
      return false;
    }
    records[index] = pack_flash_event_(events[index]);
  }
  FlashBlockHeader header{};
  header.magic = FLASH_TAG_MAGIC;
  header.version = FLASH_TAG_VERSION;
  header.event_count = static_cast<uint16_t>(event_count);
  header.block_sequence = sequence;
  header.write_epoch_s = this->current_epoch_s_();
  header.first_event_seq = records[0].seq;
  header.first_epoch_s = records[0].epoch_s;
  header.last_epoch_s = records[0].epoch_s;
  for (size_t index = 1; index < event_count; ++index) {
    header.first_epoch_s = std::min(header.first_epoch_s, records[index].epoch_s);
    header.last_epoch_s = std::max(header.last_epoch_s, records[index].epoch_s);
  }
  header.crc32 = fnv1a_hash_(reinterpret_cast<const uint8_t*>(records.data()), event_count * sizeof(FlashEventRecord));

  std::array<uint8_t, FLASH_SLOT_SIZE> slot_buffer{};
  slot_buffer.fill(0xFF);
  std::memcpy(slot_buffer.data(), &header, sizeof(header));
  std::memcpy(slot_buffer.data() + sizeof(header), records.data(), event_count * sizeof(FlashEventRecord));
  const esp_err_t write_result =
      esp_partition_write(this->flash_partition_, slot_offset, slot_buffer.data(), slot_buffer.size());
  if (write_result != ESP_OK) {
    ESP_LOGW(TAG, "Could not write decision-log flash slot %u: %s", static_cast<unsigned>(slot_index),
             esp_err_to_name(write_result));
    return false;
  }

  FlashBlockInfo info{};
  info.block_sequence = header.block_sequence;
  info.write_epoch_s = header.write_epoch_s;
  info.first_event_seq = header.first_event_seq;
  info.first_epoch_s = header.first_epoch_s;
  info.last_epoch_s = header.last_epoch_s;
  info.event_count = header.event_count;
  info.slot_index = slot_index;
  this->record_flash_block_(info);
  this->next_flash_sequence_ = sequence + 1U;
  this->last_persisted_event_seq_ = events[event_count - 1U].seq;
  return true;
}

void OpenQuattDecisionLog::record_flash_block_(const FlashBlockInfo& info) {
  if (this->flash_index_ == nullptr) return;
  const uint32_t now_epoch_s = this->current_epoch_s_();
  const uint32_t cutoff_epoch_s = now_epoch_s > RETENTION_SECONDS ? now_epoch_s - RETENTION_SECONDS : 0U;
  const bool sector_was_erased = (info.slot_index % FLASH_SLOTS_PER_SECTOR) == 0U;
  const uint32_t sector_first_slot = (info.slot_index / FLASH_SLOTS_PER_SECTOR) * FLASH_SLOTS_PER_SECTOR;
  portENTER_CRITICAL(&this->mux_);
  size_t count = this->flash_index_count_;
  for (size_t index = 0U; index < count;) {
    const uint32_t indexed_slot = this->flash_index_[index].slot_index;
    const bool invalidated =
        indexed_slot == info.slot_index || (sector_was_erased && indexed_slot >= sector_first_slot &&
                                            indexed_slot < sector_first_slot + FLASH_SLOTS_PER_SECTOR);
    if (!invalidated) {
      ++index;
      continue;
    }
    for (size_t move = index + 1U; move < count; ++move) {
      this->flash_index_[move - 1U] = this->flash_index_[move];
    }
    --count;
  }
  if (count == FLASH_SLOT_COUNT) {
    for (size_t move = 1U; move < count; ++move) {
      this->flash_index_[move - 1U] = this->flash_index_[move];
    }
    --count;
  }
  size_t insert_index = 0U;
  while (insert_index < count && this->flash_index_[insert_index].block_sequence < info.block_sequence) {
    ++insert_index;
  }
  for (size_t move = count; move > insert_index; --move) {
    this->flash_index_[move] = this->flash_index_[move - 1U];
  }
  this->flash_index_[insert_index] = info;
  this->flash_index_count_ = count + 1U;
  this->flash_archive_scanned_ = true;

  this->flash_oldest_epoch_s_ = 0U;
  this->flash_newest_epoch_s_ = 0U;
  this->flash_last_flush_epoch_s_ = 0U;
  this->flash_stored_event_count_ = 0U;
  for (size_t index = 0U; index < this->flash_index_count_; ++index) {
    const FlashBlockInfo& entry = this->flash_index_[index];
    if (cutoff_epoch_s > 0U && entry.last_epoch_s < cutoff_epoch_s) {
      continue;
    }
    this->flash_stored_event_count_ += entry.event_count;
    if (this->flash_oldest_epoch_s_ == 0U || entry.first_epoch_s < this->flash_oldest_epoch_s_) {
      this->flash_oldest_epoch_s_ = entry.first_epoch_s;
    }
    this->flash_newest_epoch_s_ = std::max(this->flash_newest_epoch_s_, entry.last_epoch_s);
    this->flash_last_flush_epoch_s_ = std::max(this->flash_last_flush_epoch_s_, entry.write_epoch_s);
  }
  portEXIT_CRITICAL(&this->mux_);
}

bool OpenQuattDecisionLog::flush_pending_events_(size_t max_batches, bool urgent_target_active,
                                                 uint32_t urgent_target_seq, bool* urgent_target_persisted) {
  if (urgent_target_persisted != nullptr) {
    *urgent_target_persisted = false;
  }
  if (!this->flash_switch_enabled_() || !this->flash_archive_available_() || !this->time_is_valid_()) {
    return false;
  }
  std::array<DecisionEvent, FLASH_EVENTS_PER_SLOT> pending{};
  size_t pending_count = 0;
  size_t batches_written = 0U;
  bool pending_contains_urgent_target = false;
  bool wrote = false;
  const uint64_t boot_epoch_s = this->boot_epoch_s_();
  size_t event_count = 0U;
  portENTER_CRITICAL(&this->mux_);
  event_count = this->event_count_;
  portEXIT_CRITICAL(&this->mux_);
  for (size_t index = 0; index < event_count && batches_written < max_batches; ++index) {
    DecisionEvent event{};
    if (!this->copy_event_(index, &event) || event.seq <= this->last_persisted_event_seq_) {
      continue;
    }
    if (!epoch_is_sane(event.epoch_s)) {
      const uint64_t derived_epoch_s = boot_epoch_s + event.uptime_s;
      if (boot_epoch_s == 0 || derived_epoch_s > UINT32_MAX || !epoch_is_sane(static_cast<uint32_t>(derived_epoch_s))) {
        return false;
      }
      event.epoch_s = static_cast<uint32_t>(derived_epoch_s);
    }
    pending_contains_urgent_target =
        pending_contains_urgent_target || (urgent_target_active && event.seq == urgent_target_seq);
    pending[pending_count++] = event;
    if (pending_count == pending.size()) {
      if (!this->write_flash_events_(pending.data(), pending_count)) {
        return false;
      }
      if (pending_contains_urgent_target && urgent_target_persisted != nullptr) {
        *urgent_target_persisted = true;
      }
      wrote = true;
      ++batches_written;
      pending_count = 0;
      pending_contains_urgent_target = false;
    }
  }
  if (pending_count > 0 && batches_written < max_batches) {
    if (!this->write_flash_events_(pending.data(), pending_count)) {
      return false;
    }
    if (pending_contains_urgent_target && urgent_target_persisted != nullptr) {
      *urgent_target_persisted = true;
    }
    wrote = true;
  }
  return wrote;
}

void OpenQuattDecisionLog::restore_flash_events_() {
  if (!this->flash_archive_scanned_ || this->events_ == nullptr) {
    return;
  }
  const uint32_t now_epoch_s = this->current_epoch_s_();
  const uint32_t cutoff_epoch_s = now_epoch_s > RETENTION_SECONDS ? now_epoch_s - RETENTION_SECONDS : 0;
  uint32_t highest_event_seq = 0;
  for (size_t index = 0; index < this->flash_index_count_; ++index) {
    FlashBlockInfo info{};
    if (!this->copy_flash_info_(index, &info) || (cutoff_epoch_s > 0 && info.last_epoch_s < cutoff_epoch_s)) {
      continue;
    }
    std::array<FlashEventRecord, FLASH_EVENTS_PER_SLOT> records{};
    FlashBlockInfo verified{};
    if (!this->read_flash_block_(info.slot_index, info.block_sequence, &verified, records.data())) {
      continue;
    }
    for (size_t event_index = 0; event_index < verified.event_count; ++event_index) {
      if (cutoff_epoch_s > 0 && records[event_index].epoch_s < cutoff_epoch_s) {
        continue;
      }
      DecisionEvent event = unpack_flash_event_(records[event_index]);
      portENTER_CRITICAL(&this->mux_);
      this->push_event_locked_(event);
      this->update_bucket_locked_(event);
      portEXIT_CRITICAL(&this->mux_);
      highest_event_seq = std::max(highest_event_seq, event.seq);
    }
  }
  this->next_seq_ = std::max(this->next_seq_, highest_event_seq + 1U);
  this->last_persisted_event_seq_ = std::max(this->last_persisted_event_seq_, highest_event_seq);
}

void OpenQuattDecisionLog::initialize_current_hour_() {
  if (!this->time_is_valid_()) {
    return;
  }
  const uint64_t uptime_s = this->monotonic_uptime_s_();
  const uint32_t epoch_s = this->current_epoch_s_();
  const uint32_t epoch_hour = (epoch_s / 3600UL) * 3600UL;
  if (this->tracked_hour_start_epoch_s_ == 0) {
    this->tracked_hour_start_epoch_s_ = epoch_hour;
  }
  portENTER_CRITICAL(&this->mux_);
  this->current_bucket_locked_(uptime_s, epoch_s);
  portEXIT_CRITICAL(&this->mux_);
}

void OpenQuattDecisionLog::set_flash_enabled(bool enabled) {
  this->flash_enabled_ = enabled;
  if (enabled) {
    const uint32_t current_watermark = this->next_seq_ > 0 ? this->next_seq_ - 1U : this->last_persisted_event_seq_;
    this->scan_flash_archive_();
    this->last_persisted_event_seq_ = std::max(this->last_persisted_event_seq_, current_watermark);
  } else if (this->next_seq_ > 0) {
    this->last_persisted_event_seq_ = this->next_seq_ - 1U;
  }
  if (!enabled) {
    this->clear_urgent_flush_(static_cast<uint64_t>(esp_timer_get_time()));
  }
}

bool OpenQuattDecisionLog::force_flush() {
  if (!this->flash_switch_enabled_() || !this->time_is_valid_()) {
    return false;
  }
  bool urgent_target_active = false;
  uint32_t urgent_target_seq = 0U;
  portENTER_CRITICAL(&this->mux_);
  if (this->urgent_flush_.pending()) {
    urgent_target_active = true;
    urgent_target_seq = this->urgent_flush_.requested_event_seq();
  }
  portEXIT_CRITICAL(&this->mux_);
  bool urgent_target_persisted = false;
  const bool flushed =
      this->flush_pending_events_(SIZE_MAX, urgent_target_active, urgent_target_seq, &urgent_target_persisted);
  if (flushed && urgent_target_persisted) {
    this->complete_urgent_flush_(static_cast<uint64_t>(esp_timer_get_time()), urgent_target_seq);
  }
  return flushed;
}

bool OpenQuattDecisionLog::clear_flash_history() {
  if (!this->flash_partition_available_()) {
    this->reset_flash_metadata_();
    return false;
  }
  const esp_err_t result = esp_partition_erase_range(this->flash_partition_, FLASH_PARTITION_OFFSET, FLASH_TOTAL_BYTES);
  if (result != ESP_OK) {
    ESP_LOGW(TAG, "Could not erase decision-log flash archive: %s", esp_err_to_name(result));
    return false;
  }
  this->reset_flash_metadata_();
  this->last_persisted_event_seq_ = this->next_seq_ > 0 ? this->next_seq_ - 1U : 0;
  this->flash_archive_scanned_ = true;
  this->clear_urgent_flush_(static_cast<uint64_t>(esp_timer_get_time()));
  return true;
}

bool OpenQuattDecisionLog::copy_event_(size_t index, DecisionEvent* out) const {
  if (out == nullptr) return false;
  portENTER_CRITICAL(&this->mux_);
  if (this->events_ == nullptr || index >= this->event_count_ || this->event_capacity_ == 0) {
    portEXIT_CRITICAL(&this->mux_);
    return false;
  }
  const size_t event_index = (this->event_head_ + index) % this->event_capacity_;
  *out = this->events_[event_index];
  portEXIT_CRITICAL(&this->mux_);
  return true;
}

bool OpenQuattDecisionLog::copy_bucket_(size_t index, HourBucket* out) const {
  if (out == nullptr) return false;
  portENTER_CRITICAL(&this->mux_);
  if (this->buckets_ == nullptr || index >= this->bucket_capacity_) {
    portEXIT_CRITICAL(&this->mux_);
    return false;
  }
  *out = this->buckets_[index];
  portEXIT_CRITICAL(&this->mux_);
  return true;
}

bool OpenQuattDecisionLog::copy_flash_info_(size_t index, FlashBlockInfo* out) const {
  if (out == nullptr || this->flash_index_ == nullptr) {
    return false;
  }
  portENTER_CRITICAL(&this->mux_);
  if (index >= this->flash_index_count_) {
    portEXIT_CRITICAL(&this->mux_);
    return false;
  }
  *out = this->flash_index_[index];
  portEXIT_CRITICAL(&this->mux_);
  return true;
}

const char* OpenQuattDecisionLog::event_type_to_string_(uint8_t value) {
  switch (value) {
    case EVENT_BOOT_MARKER:
      return "boot_marker";
    case EVENT_SOURCE_START:
      return "source_start";
    case EVENT_SOURCE_STOP:
      return "source_stop";
    case EVENT_TOPOLOGY_CHANGE:
      return "topology_change";
    case EVENT_DECISION_HOLD:
      return "decision_hold";
    case EVENT_DECISION_BLOCKED:
      return "decision_blocked";
    case EVENT_DEFROST_SEEN_START:
      return "defrost_seen_start";
    case EVENT_DEFROST_SEEN_CLEAR:
      return "defrost_seen_clear";
    case EVENT_COOLING_LIMITED:
      return "cooling_limited";
    case EVENT_COOLING_RELEASED:
      return "cooling_released";
    case EVENT_STICKY_PUMP_RUN:
      return "sticky_pump_run";
    case EVENT_BOILER_ASSIST_START:
      return "boiler_assist_start";
    case EVENT_BOILER_ASSIST_STOP:
      return "boiler_assist_stop";
    case EVENT_ATTENTION_PATTERN:
      return "attention_pattern";
    case EVENT_FROST_PROTECTION_START:
      return "frost_protection_start";
    case EVENT_FROST_PROTECTION_CLEAR:
      return "frost_protection_clear";
    case EVENT_CANDIDATE_BLOCKED:
      return "candidate_blocked";
    case EVENT_FLOW_HOLD_START:
      return "flow_hold_start";
    case EVENT_FLOW_HOLD_CLEAR:
      return "flow_hold_clear";
    case EVENT_STARTUP_INHIBIT_START:
      return "startup_inhibit_start";
    case EVENT_STARTUP_INHIBIT_CLEAR:
      return "startup_inhibit_clear";
    case EVENT_STARTUP_INHIBIT_REFRESH:
      return "startup_inhibit_refresh";
    case EVENT_INCIDENT_START:
      return "incident_start";
    case EVENT_INCIDENT_CLEAR:
      return "incident_clear";
    case EVENT_INCIDENT_ACKNOWLEDGED:
      return "incident_acknowledged";
    case EVENT_HP_AVAILABILITY_CHANGE:
      return "hp_availability_change";
    case EVENT_CONTROL_MODE_CHANGE:
      return "control_mode_change";
    case EVENT_BOILER_FALLBACK_START:
      return "boiler_fallback_start";
    case EVENT_BOILER_FALLBACK_STOP:
      return "boiler_fallback_stop";
    case EVENT_HP_START_CONFIRMED:
      return "hp_start_confirmed";
    case EVENT_HP_STOP_CONFIRMED:
      return "hp_stop_confirmed";
    default:
      return "unknown";
  }
}

const char* OpenQuattDecisionLog::subject_to_string_(uint8_t value) {
  switch (value) {
    case SUBJECT_SYSTEM:
      return "SYSTEM";
    case SUBJECT_HP1:
      return "HP1";
    case SUBJECT_HP2:
      return "HP2";
    case SUBJECT_BOTH:
      return "BOTH";
    case SUBJECT_CV:
      return "CV";
    case SUBJECT_COOLING:
      return "COOLING";
    case SUBJECT_PUMP:
      return "PUMP";
    case SUBJECT_CONTROLLER:
      return "CONTROLLER";
    default:
      return "UNKNOWN";
  }
}

const char* OpenQuattDecisionLog::reason_to_string_(uint8_t value) {
  switch (value) {
    case REASON_KEEP_CURRENT:
      return "keep_current";
    case REASON_HOLD_ACTIVE:
      return "hold_active";
    case REASON_DEFROST_HOLD:
      return "defrost_hold";
    case REASON_BETTER_HEAT:
      return "better_heat";
    case REASON_SOFT_GUARD:
      return "soft_guard";
    case REASON_LESS_POWER:
      return "less_power";
    case REASON_NO_CANDIDATE:
      return "no_candidate";
    case REASON_DEFROST_BOOST:
      return "defrost_boost";
    case REASON_RUNTIME_LEAD:
      return "runtime_lead";
    case REASON_SINGLE_TOPOLOGY:
      return "single_topology";
    case REASON_OIL_RETURN_HOLD:
      return "oil_return_hold";
    case REASON_MIN_REST_ACTIVE:
      return "min_rest_active";
    case REASON_START_STOP_RATE_HIGH:
      return "start_stop_rate_high";
    case REASON_STICKY_PROTECTION:
      return "sticky_protection";
    case REASON_BOILER_ASSIST:
      return "boiler_assist";
    case REASON_DEW_STOP:
      return "dew_stop";
    case REASON_COOLING_LIMITER:
      return "cooling_limiter";
    case REASON_SENSOR_FALLBACK:
      return "sensor_fallback";
    case REASON_FROST_PROTECTION:
      return "frost_protection";
    case REASON_FLOW_PREFLOW:
      return "flow_preflow";
    case REASON_FLOW_POSTFLOW:
      return "flow_postflow";
    case REASON_FLOW_TOO_LOW:
      return "flow_too_low";
    case REASON_CANDIDATE_IN_REST:
      return "candidate_in_rest";
    case REASON_CANDIDATE_IN_DEFROST:
      return "candidate_in_defrost";
    case REASON_CANDIDATE_UNAVAILABLE:
      return "candidate_unavailable";
    case REASON_COOLING_REQUEST_CLEARED:
      return "cooling_request_cleared";
    case REASON_HEATING_REQUEST_CLEARED:
      return "heating_request_cleared";
    case REASON_PROJECTED_FLOOR:
      return "projected_floor";
    case REASON_SIMMER:
      return "simmer";
    case REASON_FALLING_GAP:
      return "falling_gap";
    case REASON_BUFFER_STOP:
      return "buffer_stop";
    case REASON_RESTART_WAIT:
      return "restart_wait";
    case REASON_ROOM_CAP:
      return "room_cap";
    case REASON_LEVEL1_HOLD:
      return "level1_hold";
    case REASON_OIL_RETURN_RECOVERY:
      return "oil_return_recovery";
    case REASON_CAPACITY_CAP:
      return "capacity_cap";
    case REASON_STARTUP_INHIBIT:
      return "startup_inhibit";
    case REASON_HP_FAULT:
      return "hp_fault";
    case REASON_HP_PROTECTION:
      return "hp_protection";
    case REASON_HP_PREHEAT:
      return "hp_preheat";
    case REASON_HP_LINK_LOSS:
      return "hp_link_loss";
    case REASON_HP_START_FAILED:
      return "hp_start_failed";
    case REASON_HP_STOP_UNCONFIRMED:
      return "hp_stop_unconfirmed";
    case REASON_HP_RECOVERY_WAIT:
      return "hp_recovery_wait";
    case REASON_BOILER_FALLBACK:
      return "boiler_fallback";
    case REASON_FALLBACK_BLOCKED:
      return "fallback_blocked";
    case REASON_HEATING_REQUEST:
      return "heating_request";
    case REASON_COOLING_REQUEST:
      return "cooling_request";
    case REASON_COMMISSIONING:
      return "commissioning";
    case REASON_SUPERVISORY_OVERRIDE:
      return "supervisory_override";
    case REASON_HP_PERSISTENCE_FAILURE:
      return "hp_persistence_failure";
    case REASON_HP_RECOVERED:
      return "hp_recovered";
    case REASON_ROOM_DEMAND:
      return "room_demand";
    case REASON_SETPOINT_RAISE:
      return "setpoint_raise";
    default:
      return "unknown";
  }
}

const char* OpenQuattDecisionLog::severity_to_string_(uint8_t value) {
  switch (value) {
    case SEVERITY_NORMAL:
      return "normal";
    case SEVERITY_LIMITED:
      return "limited";
    case SEVERITY_ATTENTION:
      return "attention";
    case SEVERITY_FAULT:
      return "fault";
    default:
      return "unknown";
  }
}

const char* OpenQuattDecisionLog::state_to_string_(uint8_t value) {
  switch (value) {
    case STATE_IDLE:
      return "idle";
    case STATE_STANDBY:
      return "standby";
    case STATE_ACTIVE:
      return "active";
    case STATE_SINGLE:
      return "single";
    case STATE_DUO:
      return "duo";
    case STATE_BLOCKED:
      return "blocked";
    case STATE_LIMITED:
      return "limited";
    case STATE_AVAILABLE:
      return "available";
    case STATE_SUSPECT:
      return "suspect";
    case STATE_OFFLINE:
      return "offline";
    case STATE_FAULTED:
      return "faulted";
    case STATE_RECOVERING:
      return "recovering";
    case STATE_STARTING:
      return "starting";
    case STATE_PREHEAT:
      return "preheat";
    case STATE_FALLBACK:
      return "fallback";
    default:
      return "unknown";
  }
}

void OpenQuattDecisionLog::write_decision_log(httpd_req_t* req) const {
  ChunkedJsonWriter writer(req);
  const uint64_t boot_epoch_s = this->boot_epoch_s_();
  const uint64_t uptime_s = this->monotonic_uptime_s_();
  const uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

  size_t event_count = 0;
  uint32_t dropped_count = 0;
  portENTER_CRITICAL(&this->mux_);
  event_count = this->event_count_;
  dropped_count = this->dropped_count_;
  portEXIT_CRITICAL(&this->mux_);
  const uint32_t now_epoch_s = this->current_epoch_s_();
  const uint32_t cutoff_epoch_s = now_epoch_s > RETENTION_SECONDS ? now_epoch_s - RETENTION_SECONDS : 0;
  size_t visible_event_count = 0;
  for (size_t index = 0; index < event_count; ++index) {
    DecisionEvent event{};
    if (!this->copy_event_(index, &event)) {
      continue;
    }
    const uint64_t epoch_s = event.epoch_s > 0 ? event.epoch_s : (boot_epoch_s > 0 ? boot_epoch_s + event.uptime_s : 0);
    if (cutoff_epoch_s == 0 || epoch_s >= cutoff_epoch_s) {
      ++visible_event_count;
    }
  }

  bool ok =
      writer.write_literal(R"({"ok":true,"storage":{)") && writer.write_literal(R"("events":)") &&
      writer.write_string(this->events_external_ ? "psram"
                                                 : (this->events_ != nullptr ? "internal_fallback" : "disabled")) &&
      writer.write_literal(R"(,"buckets":)") &&
      writer.write_string(this->buckets_external_ ? "psram"
                                                  : (this->buckets_ != nullptr ? "internal_fallback" : "disabled")) &&
      writer.write_literal(R"(,"event_capacity":)") && writer.write_size(this->event_capacity_) &&
      writer.write_literal(R"(,"event_requested":)") && writer.write_size(this->event_capacity_requested_) &&
      writer.write_literal(R"(,"bucket_capacity":)") && writer.write_size(this->bucket_capacity_) &&
      writer.write_literal(R"(,"bucket_requested":)") && writer.write_size(this->bucket_capacity_requested_) &&
      writer.write_literal(R"(,"event_archive":)") &&
      writer.write_string(this->flash_archive_available_() ? "flash" : "unavailable") &&
      writer.write_literal(R"(,"flash_enabled":)") &&
      writer.write_literal(this->flash_switch_enabled_() ? "true" : "false") && writer.write_literal(R"(},"meta":{)") &&
      writer.write_literal(R"("api_schema_version":2)") && writer.write_literal(R"(,"incident_catalog_version":1)") &&
      writer.write_literal(R"(,"event_record_size":)") && writer.write_size(sizeof(DecisionEvent)) &&
      writer.write_literal(R"(,"bucket_record_size":)") && writer.write_size(sizeof(HourBucket)) &&
      writer.write_literal(R"(,"event_count":)") && writer.write_size(visible_event_count) &&
      writer.write_literal(R"(,"dropped_count":)") && writer.write_uint32(dropped_count) &&
      writer.write_literal(R"(,"boot_epoch_s":)") && writer.write_uint64(boot_epoch_s) &&
      writer.write_literal(R"(,"uptime_s":)") && writer.write_uint64(uptime_s) &&
      writer.write_literal(R"(,"internal_heap_free":)") && writer.write_uint32(internal_free) &&
      writer.write_literal(R"(,"internal_heap_min":)") && writer.write_uint32(internal_min) &&
      writer.write_literal(R"(,"psram_free":)") && writer.write_uint32(psram_free) &&
      writer.write_literal(R"(,"flash_stored_events":)") && writer.write_uint32(this->flash_stored_event_count_) &&
      writer.write_literal(R"(,"flash_oldest_epoch_s":)") && writer.write_uint32(this->flash_oldest_epoch_s_) &&
      writer.write_literal(R"(,"flash_newest_epoch_s":)") && writer.write_uint32(this->flash_newest_epoch_s_) &&
      writer.write_literal(R"(,"flash_last_flush_epoch_s":)") && writer.write_uint32(this->flash_last_flush_epoch_s_) &&
      writer.write_literal(R"(,"flash_storage_bytes":)") && writer.write_size(FLASH_TOTAL_BYTES) &&
      writer.write_literal(R"(,"flash_write_count":)") && writer.write_uint32(this->next_flash_sequence_) &&
      writer.write_literal(R"(},"events":[)");

  bool first_event = true;
  for (size_t index = 0; ok && index < event_count; ++index) {
    DecisionEvent event{};
    if (!this->copy_event_(index, &event)) {
      continue;
    }
    const uint64_t epoch_s = event.epoch_s > 0 ? static_cast<uint64_t>(event.epoch_s)
                                               : (boot_epoch_s > 0 ? boot_epoch_s + event.uptime_s : 0);
    if (cutoff_epoch_s > 0 && epoch_s < cutoff_epoch_s) {
      continue;
    }
    ok = (first_event || writer.write_char(',')) && writer.write_literal(R"({"seq":)") &&
         writer.write_uint32(event.seq) && writer.write_literal(R"(,"uptime_s":)") &&
         writer.write_uint64(event.uptime_s) && writer.write_literal(R"(,"epoch_s":)") &&
         writer.write_uint64(epoch_s) && writer.write_literal(R"(,"event_type":)") &&
         writer.write_string(event_type_to_string_(event.event_type)) && writer.write_literal(R"(,"subject":)") &&
         writer.write_string(subject_to_string_(event.subject)) && writer.write_literal(R"(,"reason":)") &&
         writer.write_string(reason_to_string_(event.reason_code)) && writer.write_literal(R"(,"severity":)") &&
         writer.write_string(severity_to_string_(event.severity)) && writer.write_literal(R"(,"cm":)") &&
         writer.write_uint32(event.control_mode_code) && writer.write_literal(R"(,"from":)") &&
         writer.write_string(state_to_string_(event.from_state)) && writer.write_literal(R"(,"to":)") &&
         writer.write_string(state_to_string_(event.to_state)) && writer.write_literal(R"(,"value_a":)") &&
         writer.write_int32(event.value_a) && writer.write_literal(R"(,"value_b":)") &&
         writer.write_int32(event.value_b) && writer.write_literal(R"(,"threshold_a":)") &&
         writer.write_int32(event.threshold_a) && writer.write_literal(R"(,"duration_s":)") &&
         writer.write_uint32(event.duration_s) && writer.write_literal(R"(,"flags":)") &&
         writer.write_uint32(event.flags) && writer.write_char('}');
    if (ok) {
      first_event = false;
    }
  }

  ok = ok && writer.write_literal(R"(],"buckets":[)");
  bool first_bucket = true;
  auto write_bucket = [&](const HourBucket& bucket, const char* source) -> bool {
    const bool prefix_ok = first_bucket || writer.write_char(',');
    const bool bucket_ok =
        prefix_ok && writer.write_literal(R"({"hour_start_uptime_s":)") &&
        writer.write_uint64(bucket.hour_start_uptime_s) && writer.write_literal(R"(,"hour_start_epoch_s":)") &&
        writer.write_uint32(bucket.hour_start_epoch_s) && writer.write_literal(R"(,"source":)") &&
        writer.write_string(source) && writer.write_literal(R"(,"starts_hp1":)") &&
        writer.write_uint32(bucket.starts_hp1) && writer.write_literal(R"(,"starts_hp2":)") &&
        writer.write_uint32(bucket.starts_hp2) && writer.write_literal(R"(,"stops_hp1":)") &&
        writer.write_uint32(bucket.stops_hp1) && writer.write_literal(R"(,"stops_hp2":)") &&
        writer.write_uint32(bucket.stops_hp2) && writer.write_literal(R"(,"topology_single_count":)") &&
        writer.write_uint32(bucket.topology_single_count) && writer.write_literal(R"(,"topology_duo_count":)") &&
        writer.write_uint32(bucket.topology_duo_count) && writer.write_literal(R"(,"cv_assist_start_count":)") &&
        writer.write_uint32(bucket.cv_assist_start_count) && writer.write_literal(R"(,"cv_assist_stop_count":)") &&
        writer.write_uint32(bucket.cv_assist_stop_count) && writer.write_literal(R"(,"cooling_limited_count":)") &&
        writer.write_uint32(bucket.cooling_limited_count) && writer.write_literal(R"(,"cooling_released_count":)") &&
        writer.write_uint32(bucket.cooling_released_count) && writer.write_literal(R"(,"dewpoint_stop_count":)") &&
        writer.write_uint32(bucket.dewpoint_stop_count) && writer.write_literal(R"(,"sticky_run_count":)") &&
        writer.write_uint32(bucket.sticky_run_count) && writer.write_literal(R"(,"defrost_seen_count_hp1":)") &&
        writer.write_uint32(bucket.defrost_seen_count_hp1) && writer.write_literal(R"(,"defrost_seen_count_hp2":)") &&
        writer.write_uint32(bucket.defrost_seen_count_hp2) && writer.write_literal(R"(,"defrost_hold_count_hp1":)") &&
        writer.write_uint32(bucket.defrost_hold_count_hp1) && writer.write_literal(R"(,"defrost_hold_count_hp2":)") &&
        writer.write_uint32(bucket.defrost_hold_count_hp2) && writer.write_literal(R"(,"defrost_boost_count_hp1":)") &&
        writer.write_uint32(bucket.defrost_boost_count_hp1) && writer.write_literal(R"(,"defrost_boost_count_hp2":)") &&
        writer.write_uint32(bucket.defrost_boost_count_hp2) && writer.write_literal(R"(,"attention_count":)") &&
        writer.write_uint32(bucket.attention_count) && writer.write_char('}');
    if (bucket_ok) {
      first_bucket = false;
    }
    return bucket_ok;
  };
  for (size_t index = 0; ok && index < this->bucket_capacity_; ++index) {
    HourBucket bucket{};
    if (!this->copy_bucket_(index, &bucket) || !bucket.valid) {
      continue;
    }
    ok = write_bucket(bucket, "ram");
  }

  ok = ok && writer.write_literal(R"(]})") && writer.flush();
  if (!ok) {
    ESP_LOGW(TAG, "Failed to write decision-log response");
  }
  httpd_resp_send_chunk(req, nullptr, 0);
}

void OpenQuattDecisionLog::write_metadata(httpd_req_t* req) const {
  ChunkedJsonWriter writer(req);
  bool ok = writer.write_literal(R"({"ok":true,"enabled":)") &&
            writer.write_literal(this->flash_switch_enabled_() ? "true" : "false") &&
            writer.write_literal(R"(,"available":)") &&
            writer.write_literal(this->flash_archive_available_() ? "true" : "false") &&
            writer.write_literal(R"(,"stored_events":)") && writer.write_uint32(this->flash_stored_event_count_) &&
            writer.write_literal(R"(,"capacity_events":)") && writer.write_size(FLASH_EVENT_CAPACITY) &&
            writer.write_literal(R"(,"retention_days":7)") && writer.write_literal(R"(,"oldest_epoch_s":)") &&
            writer.write_uint32(this->flash_oldest_epoch_s_) && writer.write_literal(R"(,"newest_epoch_s":)") &&
            writer.write_uint32(this->flash_newest_epoch_s_) && writer.write_literal(R"(,"last_flush_epoch_s":)") &&
            writer.write_uint32(this->flash_last_flush_epoch_s_) && writer.write_literal(R"(,"storage_bytes":)") &&
            writer.write_size(FLASH_TOTAL_BYTES) && writer.write_literal(R"(,"write_count":)") &&
            writer.write_uint32(this->next_flash_sequence_) && writer.write_literal(R"(})") && writer.flush();
  if (!ok) {
    ESP_LOGW(TAG, "Failed to write decision-log metadata response");
  }
  httpd_resp_send_chunk(req, nullptr, 0);
}

}  // namespace openquatt_decision_log
}  // namespace esphome
