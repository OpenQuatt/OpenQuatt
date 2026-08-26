#include "OpenQuattTrends.h"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_timer.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_trends {

static const char* const TAG = "openquatt.trends";

namespace {

constexpr uint32_t kDefaultWindowHours = 24;

uint32_t elapsed_operation_ms(int64_t started_us) {
  const int64_t elapsed_us = esp_timer_get_time() - started_us;
  if (elapsed_us <= 0) {
    return 0U;
  }
  const uint64_t rounded_ms = (static_cast<uint64_t>(elapsed_us) + 999ULL) / 1000ULL;
  return static_cast<uint32_t>(std::min<uint64_t>(rounded_ms, UINT32_MAX));
}

static bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) {
    return false;
  }
  const size_t path_len = std::strlen(path);
  return std::strncmp(url, path, path_len) == 0 && (url[path_len] == '\0' || url[path_len] == '?');
}

uint32_t parse_window_hours_value(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return kDefaultWindowHours;
  }
  const unsigned long parsed = std::strtoul(value, nullptr, 10);
  if (parsed > 0) {
    return static_cast<uint32_t>(parsed);
  }
  return kDefaultWindowHours;
}

uint32_t parse_window_hours_from_url(const char* url) {
  if (url == nullptr) {
    return kDefaultWindowHours;
  }

  const char* query = std::strchr(url, '?');
  if (query == nullptr || *(query + 1) == '\0') {
    return kDefaultWindowHours;
  }
  query++;

  while (*query != '\0') {
    const char* separator = std::strchr(query, '&');
    const size_t length = separator == nullptr ? std::strlen(query) : static_cast<size_t>(separator - query);
    if (length > 6 && std::strncmp(query, "hours=", 6) == 0) {
      char buffer[12];
      const size_t value_length = std::min<size_t>(length - 6, sizeof(buffer) - 1);
      std::memcpy(buffer, query + 6, value_length);
      buffer[value_length] = '\0';
      return parse_window_hours_value(buffer);
    }

    if (separator == nullptr) {
      break;
    }
    query = separator + 1;
  }

  return kDefaultWindowHours;
}

void format_temperature_x10(int16_t value, char* buffer, size_t buffer_size) {
  if (value == INT16_MIN) {
    std::snprintf(buffer, buffer_size, "nan");
    return;
  }

  const int32_t hundredths = static_cast<int32_t>(value) * 10;
  const bool negative = hundredths < 0;
  const uint32_t magnitude = static_cast<uint32_t>(negative ? -hundredths : hundredths);
  std::snprintf(buffer, buffer_size, "%s%lu.%02lu", negative ? "-" : "", static_cast<unsigned long>(magnitude / 100),
                static_cast<unsigned long>(magnitude % 100));
}

void format_unsigned_metric(uint16_t value, char* buffer, size_t buffer_size) {
  if (value == UINT16_MAX) {
    std::snprintf(buffer, buffer_size, "nan");
    return;
  }
  std::snprintf(buffer, buffer_size, "%u", static_cast<unsigned>(value));
}

}  // namespace

class ChunkedTextWriter {
 public:
  explicit ChunkedTextWriter(httpd_req_t* req) : req_(req) {
    this->buffer_.allocate(BUFFER_SIZE);
    this->scratch_.allocate(SCRATCH_SIZE);
  }

  bool printf(const char* format, ...) {
    if (!this->buffer_ || !this->scratch_) {
      return false;
    }
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(this->scratch_.data(), this->scratch_.size(), format, args);
    va_end(args);
    if (written < 0) {
      return false;
    }
    if (static_cast<size_t>(written) < this->scratch_.size()) {
      return this->write_bytes_(this->scratch_.data(), static_cast<size_t>(written));
    }

    // Trend lines are intentionally compact. If this ever grows, truncate the
    // single line instead of allocating a dynamic buffer on the heap.
    return this->write_bytes_(this->scratch_.data(), this->scratch_.size() - 1);
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
  static constexpr size_t SCRATCH_SIZE = 160;

  bool write_bytes_(const char* data, size_t len) {
    if (!this->buffer_) {
      return false;
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
  PsramBuffer<char> scratch_{};
  size_t used_{0};
};

class OpenQuattTrendsRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattTrendsRequestHandler(OpenQuattTrends* parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    return url_path_matches(url_buf, "/trends/history") && request->method() == HTTP_GET;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    const std::string hours_arg = request->arg("hours");
    const uint32_t window_hours =
        hours_arg.empty() ? parse_window_hours_from_url(url_buf) : parse_window_hours_value(hours_arg.c_str());
    const std::string meta_arg = request->arg("meta");
    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (meta_arg == "1" || meta_arg == "true") {
      this->parent_->write_metadata(req);
      return;
    }
    this->parent_->write_history(req, window_hours);
  }

 protected:
  OpenQuattTrends* parent_;
};

void OpenQuattTrends::setup() {
  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    return;
  }

  if (!this->ram_history_.allocate_external(RAM_CAPACITY)) {
    ESP_LOGE(TAG, "Failed to allocate trend history buffer in PSRAM");
  }
  if (!this->flash_index_.allocate_external(FLASH_SLOT_COUNT)) {
    ESP_LOGW(TAG, "Failed to allocate trend flash index");
  }

  this->flash_partition_ =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "openquatt_data");
  if (this->flash_partition_ == nullptr) {
    ESP_LOGW(TAG, "Trend flash partition 'openquatt_data' not found");
  }

  if (this->flash_switch_ != nullptr) {
    this->flash_enabled_ = this->flash_switch_->state;
  }

  if (this->capture_switch_ != nullptr && this->capture_switch_->state) {
    this->last_capture_ms_ = (uint32_t)millis();
  }

  this->scan_flash_archive_();
  this->sync_time_state_();

  web_server_base::global_web_server_base->add_handler(new OpenQuattTrendsRequestHandler(this));
}

void OpenQuattTrends::loop() {
  this->sync_time_state_();
  if (!this->flash_switch_enabled_() || !this->flash_archive_available_()) {
    return;
  }

  if (this->flash_builder_.active && this->flash_builder_.sample_count > 0) {
    const uint32_t now_ms = (uint32_t)millis();
    if ((uint32_t)(now_ms - this->last_flash_flush_ms_) >= SAMPLE_INTERVAL_MS * 12UL) {
      this->flush_flash_builder_(true);
    }
  }
}

void OpenQuattTrends::on_shutdown() { this->force_flush(); }

void OpenQuattTrends::dump_config() {
  const FlashIOMetrics flash_io_metrics = this->snapshot_flash_io_metrics_();
  ESP_LOGCONFIG(TAG, "OpenQuatt Trends");
  ESP_LOGCONFIG(TAG, "  Capture switch: %s", this->capture_switch_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Flash switch: %s", this->flash_switch_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Clock: %s", this->clock_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Flash partition: %s", this->flash_partition_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  RAM samples: %u / %u", static_cast<unsigned>(this->ram_count_),
                static_cast<unsigned>(RAM_CAPACITY));
  ESP_LOGCONFIG(TAG, "  RAM history buffer: %s",
                !this->ram_history_ ? "missing" : (this->ram_history_.is_external() ? "PSRAM" : "internal"));
  ESP_LOGCONFIG(TAG, "  Flash index: %s",
                !this->flash_index_ ? "missing" : (this->flash_index_.is_external() ? "PSRAM" : "internal"));
  ESP_LOGCONFIG(TAG, "  Flash enabled: %s", YESNO(this->flash_switch_enabled_()));
  ESP_LOGCONFIG(TAG, "  Flash archive available: %s", YESNO(this->flash_archive_available_()));
  ESP_LOGCONFIG(TAG, "  Flash archive scanned: %s", YESNO(this->flash_archive_scanned_));
  ESP_LOGCONFIG(TAG, "  Flash I/O: erases=%u erase_failures=%u write_failures=%u max_erase=%ums max_write=%ums",
                static_cast<unsigned>(flash_io_metrics.erase_count),
                static_cast<unsigned>(flash_io_metrics.erase_failure_count),
                static_cast<unsigned>(flash_io_metrics.write_failure_count),
                static_cast<unsigned>(flash_io_metrics.max_erase_duration_ms),
                static_cast<unsigned>(flash_io_metrics.max_write_duration_ms));
}

float OpenQuattTrends::get_setup_priority() const { return setup_priority::WIFI; }

bool OpenQuattTrends::capture_enabled_() const {
  return this->storage_capabilities_().ram_history_available && this->capture_switch_ != nullptr &&
         this->capture_switch_->state;
}

bool OpenQuattTrends::flash_switch_enabled_() const {
  if (this->flash_switch_ != nullptr) {
    return this->flash_switch_->state;
  }
  return this->flash_enabled_;
}

TrendStorageCapabilities OpenQuattTrends::storage_capabilities_() const {
  return trend_storage_capabilities(static_cast<bool>(this->ram_history_), static_cast<bool>(this->flash_index_),
                                    this->flash_partition_ != nullptr);
}

bool OpenQuattTrends::flash_archive_available_() const { return this->storage_capabilities_().flash_archive_available; }

bool OpenQuattTrends::time_is_valid_() const { return this->clock_ != nullptr && this->clock_->now().is_valid(); }

uint64_t OpenQuattTrends::current_time_ms_() const {
  if (this->time_is_valid_()) {
    const auto now = this->clock_->now();
    return static_cast<uint64_t>(now.timestamp) * 1000ULL;
  }
  return static_cast<uint64_t>(millis());
}

uint64_t OpenQuattTrends::current_epoch_offset_ms_() const {
  if (!this->time_is_valid_()) {
    return 0;
  }
  const uint64_t now_ms = this->current_time_ms_();
  const uint64_t mono_ms = static_cast<uint64_t>(millis());
  return now_ms >= mono_ms ? (now_ms - mono_ms) : 0;
}

int16_t OpenQuattTrends::encode_temp_(float value) {
  if (!std::isfinite(value)) {
    return INT16_MIN;
  }
  const float scaled = std::roundf(value * 10.0f);
  if (scaled < static_cast<float>(INT16_MIN + 1) || scaled > static_cast<float>(INT16_MAX)) {
    return INT16_MIN;
  }
  return static_cast<int16_t>(scaled);
}

uint16_t OpenQuattTrends::encode_unsigned_(float value) {
  if (!std::isfinite(value) || value < 0.0f) {
    return UINT16_MAX;
  }
  const float rounded = std::roundf(value);
  if (rounded > static_cast<float>(UINT16_MAX - 1)) {
    return UINT16_MAX;
  }
  return static_cast<uint16_t>(rounded);
}

float OpenQuattTrends::decode_temp_(int16_t value) {
  return value == INT16_MIN ? NAN : static_cast<float>(value) / 10.0f;
}

float OpenQuattTrends::decode_unsigned_(uint16_t value) {
  return value == UINT16_MAX ? NAN : static_cast<float>(value);
}

uint32_t OpenQuattTrends::fnv1a_hash_(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

bool OpenQuattTrends::valid_unsigned_metric_(float value) { return std::isfinite(value) && value >= 0.0f; }

void OpenQuattTrends::reset_interval_metric_samples_(IntervalMetricState& state) {
  state.sum = 0.0f;
  state.count = 0;
  state.min = 0.0f;
  state.max = 0.0f;
}

void OpenQuattTrends::update_interval_metric_(IntervalMetricState& state, float value) {
  if (!valid_unsigned_metric_(value)) {
    return;
  }
  if (state.count == 0) {
    state.min = value;
    state.max = value;
  } else {
    state.min = std::min(state.min, value);
    state.max = std::max(state.max, value);
  }
  state.sum += value;
  state.count++;
}

float OpenQuattTrends::select_interval_metric_value_(const IntervalMetricState& state, float fallback) {
  if (state.count == 0) {
    return fallback;
  }

  const float average = state.sum / static_cast<float>(state.count);
  if (state.last_saved == 0.0f && state.max > 0.0f) {
    return state.max;
  }

  const float min_deviation = std::abs(state.min - average);
  const float max_deviation = std::abs(state.max - average);
  const bool max_is_furthest = max_deviation > min_deviation;
  const float furthest_value = max_is_furthest ? state.max : state.min;
  const float furthest_deviation = max_is_furthest ? max_deviation : min_deviation;

  return furthest_deviation > (INTERVAL_DEVIATION_RATIO * average) ? furthest_value : average;
}

void OpenQuattTrends::update_last_saved_metric_(IntervalMetricState& state, float value) {
  if (valid_unsigned_metric_(value)) {
    state.last_saved = value;
  }
}

OpenQuattTrends::TrendValues OpenQuattTrends::pack_values_(float outside_c, float supply_c, float room_c,
                                                           float room_setpoint_c, float flow_lph, float input_w,
                                                           float output_w) const {
  return TrendValues{
      encode_temp_(outside_c),    encode_temp_(supply_c),    encode_temp_(room_c),       encode_temp_(room_setpoint_c),
      encode_unsigned_(flow_lph), encode_unsigned_(input_w), encode_unsigned_(output_w),
  };
}

OpenQuattTrends::TrendSample OpenQuattTrends::make_sample_(uint64_t timestamp_ms, const TrendValues& values) const {
  return TrendSample{timestamp_ms, values};
}

void OpenQuattTrends::rebase_ram_history_(uint64_t offset_ms) {
  if (offset_ms == 0) {
    return;
  }
  if (!this->ram_history_) {
    return;
  }

  for (size_t i = 0; i < this->ram_count_; ++i) {
    const size_t index = (this->ram_head_ + i) % RAM_CAPACITY;
    this->ram_history_[index].timestamp_ms += offset_ms;
  }

  if (this->flash_builder_.active) {
    this->flash_builder_.start_timestamp_ms += offset_ms;
    for (size_t i = 0; i < this->flash_builder_.sample_count; ++i) {
      this->flash_builder_.samples[i].timestamp_ms += offset_ms;
    }
  }
}

void OpenQuattTrends::sync_time_state_() {
  const bool valid = this->time_is_valid_();
  if (valid && !this->time_rebased_) {
    const uint64_t offset_ms = this->current_epoch_offset_ms_();
    if (offset_ms > 0) {
      this->rebase_ram_history_(offset_ms);
    }
    this->time_rebased_ = true;
  }

  if (valid) {
    this->load_archive_if_needed_();
  }
}

void OpenQuattTrends::load_archive_if_needed_() {
  if (!this->ram_history_ || !this->flash_switch_enabled_() || !this->flash_archive_available_() ||
      !this->time_is_valid_()) {
    return;
  }

  if (!this->flash_archive_scanned_) {
    this->scan_flash_archive_();
  }

  const TrendArchiveLoadAction load_action =
      trend_archive_load_action(this->flash_archive_scanned_, this->flash_index_count_, this->flash_archive_seeded_);
  if (load_action == TrendArchiveLoadAction::WAIT) {
    return;
  }

  if (load_action == TrendArchiveLoadAction::MARK_EMPTY_AS_SEEDED) {
    this->flash_archive_seeded_ = true;
    return;
  }

  if (this->merge_flash_history_into_ram_()) {
    this->flash_archive_seeded_ = true;
  }
}

void OpenQuattTrends::push_ram_sample_(const TrendSample& sample) {
  if (!this->ram_history_) {
    return;
  }
  if (RAM_CAPACITY == 0) {
    return;
  }

  const size_t insert_index = (this->ram_head_ + this->ram_count_) % RAM_CAPACITY;
  this->ram_history_[insert_index] = sample;
  if (this->ram_count_ < RAM_CAPACITY) {
    ++this->ram_count_;
  } else {
    this->ram_head_ = (this->ram_head_ + 1) % RAM_CAPACITY;
  }
}

bool OpenQuattTrends::get_ram_sample_at_(size_t ordered_index, TrendSample* sample) const {
  if (sample == nullptr || !this->ram_history_ || ordered_index >= this->ram_count_) {
    return false;
  }
  const size_t index = (this->ram_head_ + ordered_index) % RAM_CAPACITY;
  *sample = this->ram_history_[index];
  return true;
}

uint64_t OpenQuattTrends::get_ram_oldest_timestamp_ms_() const {
  TrendSample sample{};
  if (this->ram_count_ == 0 || !this->get_ram_sample_at_(0, &sample)) {
    return 0;
  }
  return sample.timestamp_ms;
}

bool OpenQuattTrends::scan_flash_archive_() {
  this->reset_flash_metadata_();
  this->next_flash_sequence_ = 0;
  this->flash_index_count_ = 0;

  if (!this->flash_archive_available_()) {
    return false;
  }

  bool any_valid = false;
  uint32_t highest_sequence = 0;

  for (size_t slot = 0; slot < FLASH_SLOT_COUNT; ++slot) {
    FlashBlockInfo info{};
    std::array<TrendSample, FLASH_SAMPLES_PER_BLOCK> samples{};
    if (!this->read_flash_block_(static_cast<uint32_t>(slot), UINT32_MAX, &info, &samples)) {
      continue;
    }

    any_valid = true;
    ++this->flash_valid_block_count_;
    if (this->flash_index_ && this->flash_index_count_ < this->flash_index_.size()) {
      this->flash_index_[this->flash_index_count_++] = info;
    }
    if (this->flash_valid_block_count_ == 1 || info.start_timestamp_ms < this->flash_oldest_timestamp_ms_) {
      this->flash_oldest_timestamp_ms_ = info.start_timestamp_ms;
    }
    if (this->flash_valid_block_count_ == 1 || info.end_timestamp_ms >= this->flash_latest_timestamp_ms_) {
      this->flash_latest_timestamp_ms_ = info.end_timestamp_ms;
      this->flash_last_flush_timestamp_ms_ =
          info.flush_timestamp_ms > 0 ? info.flush_timestamp_ms : info.end_timestamp_ms;
    }
    if (this->flash_valid_block_count_ == 1 || info.sequence >= highest_sequence) {
      highest_sequence = info.sequence;
    }
  }

  if (this->flash_index_ && this->flash_index_count_ > 1) {
    std::sort(this->flash_index_.data(), this->flash_index_.data() + this->flash_index_count_,
              [](const FlashBlockInfo& a, const FlashBlockInfo& b) {
                if (a.start_timestamp_ms == b.start_timestamp_ms) {
                  return a.sequence < b.sequence;
                }
                return a.start_timestamp_ms < b.start_timestamp_ms;
              });
  }

  this->next_flash_sequence_ = any_valid ? (highest_sequence + 1U) : 0U;
  this->flash_archive_scanned_ = true;
  return any_valid;
}

bool OpenQuattTrends::merge_flash_history_into_ram_() {
  if (!this->ram_history_ || !this->flash_archive_available_()) {
    return false;
  }
  if (!this->flash_archive_scanned_) {
    if (!this->scan_flash_archive_()) {
      return false;
    }
  }

  const uint64_t cutoff_ms =
      this->flash_latest_timestamp_ms_ > RAM_WINDOW_MS ? this->flash_latest_timestamp_ms_ - RAM_WINDOW_MS : 0;

  this->ram_head_ = 0;
  this->ram_count_ = 0;

  for (size_t block_index = 0; block_index < this->flash_index_count_; ++block_index) {
    const FlashBlockInfo& indexed = this->flash_index_[block_index];
    std::array<TrendSample, FLASH_SAMPLES_PER_BLOCK> block_samples{};
    FlashBlockInfo info{};
    if (!this->read_flash_block_(indexed.slot_index, indexed.sequence, &info, &block_samples)) {
      continue;
    }
    for (size_t index = 0; index < info.sample_count; ++index) {
      const TrendSample& sample = block_samples[index];
      if (sample.timestamp_ms >= cutoff_ms) {
        this->push_ram_sample_(sample);
      }
    }
  }

  return this->ram_count_ > 0;
}

bool OpenQuattTrends::clear_flash_archive_() {
  if (!this->flash_archive_available_()) {
    this->reset_flash_metadata_();
    this->next_flash_sequence_ = 0;
    this->flash_archive_scanned_ = false;
    return false;
  }

  const esp_err_t erase_result = esp_partition_erase_range(this->flash_partition_, 0, FLASH_TOTAL_BYTES);
  if (erase_result != ESP_OK) {
    ESP_LOGW(TAG, "Could not erase trend flash archive: %s", esp_err_to_name(erase_result));
    return false;
  }

  this->reset_flash_metadata_();
  this->next_flash_sequence_ = 0;
  this->flash_archive_scanned_ = true;
  this->flash_archive_seeded_ = false;
  this->reset_flash_builder_();
  return true;
}

void OpenQuattTrends::reset_flash_builder_() { this->flash_builder_ = {}; }

bool OpenQuattTrends::append_sample_to_flash_(const TrendSample& sample) {
  if (!this->flash_switch_enabled_() || !this->flash_archive_available_() || !this->time_is_valid_()) {
    return false;
  }

  if (!this->flash_builder_.active) {
    this->flash_builder_.active = true;
    this->flash_builder_.start_timestamp_ms = sample.timestamp_ms;
    this->flash_builder_.sample_count = 0;
    this->flash_builder_.sequence = this->next_flash_sequence_;
  }

  if (this->flash_builder_.sample_count >= FLASH_SAMPLES_PER_BLOCK) {
    if (!this->flush_flash_builder_(true)) {
      return false;
    }
    this->flash_builder_.active = true;
    this->flash_builder_.start_timestamp_ms = sample.timestamp_ms;
    this->flash_builder_.sample_count = 0;
    this->flash_builder_.sequence = this->next_flash_sequence_;
  }

  this->flash_builder_.samples[this->flash_builder_.sample_count++] = sample;
  this->flash_dirty_ = true;

  if (this->flash_builder_.sample_count >= FLASH_SAMPLES_PER_BLOCK) {
    return this->flush_flash_builder_(true);
  }
  return true;
}

bool OpenQuattTrends::write_flash_block_(const FlashBlockBuilder& builder) {
  if (!this->flash_archive_available_() || builder.sample_count == 0) {
    return false;
  }

  const uint32_t slot_index = builder.sequence % FLASH_SLOT_COUNT;
  const uint32_t slot_offset = slot_index * FLASH_SLOT_SIZE;
  const bool erased_sector = (slot_offset % FLASH_SECTOR_SIZE) == 0;
  if (erased_sector) {
    const int64_t erase_started_us = esp_timer_get_time();
    const esp_err_t erase_result = esp_partition_erase_range(this->flash_partition_, slot_offset, FLASH_SECTOR_SIZE);
    this->record_flash_erase_(elapsed_operation_ms(erase_started_us), erase_result == ESP_OK);
    if (erase_result != ESP_OK) {
      ESP_LOGW(TAG, "Could not erase trend flash sector %u: %s",
               static_cast<unsigned>(slot_index / FLASH_SLOTS_PER_SECTOR), esp_err_to_name(erase_result));
      return false;
    }
  }

  std::array<uint8_t, FLASH_SLOT_SIZE> slot_buffer{};
  TrendBlockHeader header{};
  header.magic = TAG_MAGIC;
  header.version = TAG_VERSION;
  header.sample_count = builder.sample_count;
  header.sequence = builder.sequence;
  header.start_timestamp_ms = builder.start_timestamp_ms;
  header.payload_bytes = static_cast<uint32_t>(builder.sample_count * sizeof(TrendSample));
  header.crc32 = fnv1a_hash_(reinterpret_cast<const uint8_t*>(builder.samples.data()), header.payload_bytes);
  header.reserved = static_cast<uint32_t>(this->current_time_ms_() / 1000ULL);
  std::memcpy(slot_buffer.data(), &header, sizeof(header));
  std::memcpy(slot_buffer.data() + sizeof(header), builder.samples.data(), header.payload_bytes);

  const int64_t write_started_us = esp_timer_get_time();
  const esp_err_t write_result =
      esp_partition_write(this->flash_partition_, slot_offset, slot_buffer.data(), slot_buffer.size());
  this->record_flash_write_(elapsed_operation_ms(write_started_us), write_result == ESP_OK);
  if (write_result != ESP_OK) {
    ESP_LOGW(TAG, "Could not write trend flash slot %u: %s", static_cast<unsigned>(slot_index),
             esp_err_to_name(write_result));
    return false;
  }

  FlashBlockInfo info{};
  info.sequence = builder.sequence;
  info.start_timestamp_ms = builder.start_timestamp_ms;
  const size_t last_sample_index = builder.sample_count > 0 ? static_cast<size_t>(builder.sample_count - 1) : 0;
  info.end_timestamp_ms = builder.samples[last_sample_index].timestamp_ms;
  info.flush_timestamp_ms = static_cast<uint64_t>(header.reserved) * 1000ULL;
  info.sample_count = builder.sample_count;
  info.slot_index = slot_index;

  this->flash_latest_timestamp_ms_ = builder.samples[last_sample_index].timestamp_ms;
  this->flash_last_flush_timestamp_ms_ = info.flush_timestamp_ms;
  if (this->flash_valid_block_count_ < FLASH_SLOT_COUNT) {
    ++this->flash_valid_block_count_;
  }
  if (this->flash_valid_block_count_ <= 1 || this->flash_oldest_timestamp_ms_ == 0) {
    this->flash_oldest_timestamp_ms_ = builder.start_timestamp_ms;
  } else if (this->flash_valid_block_count_ >= FLASH_SLOT_COUNT) {
    const uint32_t oldest_sequence = builder.sequence + 1U - static_cast<uint32_t>(FLASH_SLOT_COUNT);
    FlashBlockInfo oldest{};
    std::array<TrendSample, FLASH_SAMPLES_PER_BLOCK> oldest_samples{};
    if (this->read_flash_block_(oldest_sequence % FLASH_SLOT_COUNT, oldest_sequence, &oldest, &oldest_samples)) {
      this->flash_oldest_timestamp_ms_ = oldest.start_timestamp_ms;
    }
  }
  this->next_flash_sequence_ = builder.sequence + 1U;
  this->flash_dirty_ = false;
  this->last_flash_flush_ms_ = static_cast<uint32_t>(millis());
  const int64_t index_update_started_us = esp_timer_get_time();
  const bool index_updated = this->update_flash_index_after_write_(info, erased_sector);
  this->record_flash_index_update_(elapsed_operation_ms(index_update_started_us));
  if (!index_updated) {
    this->invalidate_flash_index_();
  }
  return true;
}

void OpenQuattTrends::record_flash_erase_(uint32_t duration_ms, bool success) {
  portENTER_CRITICAL(&this->flash_io_metrics_lock_);
  this->flash_io_metrics_.last_erase_duration_ms = duration_ms;
  this->flash_io_metrics_.max_erase_duration_ms = std::max(this->flash_io_metrics_.max_erase_duration_ms, duration_ms);
  uint32_t& count = success ? this->flash_io_metrics_.erase_count : this->flash_io_metrics_.erase_failure_count;
  count = count < UINT32_MAX ? count + 1U : UINT32_MAX;
  portEXIT_CRITICAL(&this->flash_io_metrics_lock_);
}

void OpenQuattTrends::record_flash_write_(uint32_t duration_ms, bool success) {
  portENTER_CRITICAL(&this->flash_io_metrics_lock_);
  this->flash_io_metrics_.last_write_duration_ms = duration_ms;
  this->flash_io_metrics_.max_write_duration_ms = std::max(this->flash_io_metrics_.max_write_duration_ms, duration_ms);
  if (!success) {
    this->flash_io_metrics_.write_failure_count = this->flash_io_metrics_.write_failure_count < UINT32_MAX
                                                      ? this->flash_io_metrics_.write_failure_count + 1U
                                                      : UINT32_MAX;
  }
  portEXIT_CRITICAL(&this->flash_io_metrics_lock_);
}

void OpenQuattTrends::record_flash_flush_(uint32_t duration_ms) {
  portENTER_CRITICAL(&this->flash_io_metrics_lock_);
  this->flash_io_metrics_.last_flush_duration_ms = duration_ms;
  this->flash_io_metrics_.max_flush_duration_ms = std::max(this->flash_io_metrics_.max_flush_duration_ms, duration_ms);
  portEXIT_CRITICAL(&this->flash_io_metrics_lock_);
}

void OpenQuattTrends::record_flash_index_update_(uint32_t duration_ms) {
  portENTER_CRITICAL(&this->flash_io_metrics_lock_);
  this->flash_io_metrics_.last_index_update_duration_ms = duration_ms;
  this->flash_io_metrics_.max_index_update_duration_ms =
      std::max(this->flash_io_metrics_.max_index_update_duration_ms, duration_ms);
  portEXIT_CRITICAL(&this->flash_io_metrics_lock_);
}

OpenQuattTrends::FlashIOMetrics OpenQuattTrends::snapshot_flash_io_metrics_() {
  portENTER_CRITICAL(&this->flash_io_metrics_lock_);
  const FlashIOMetrics snapshot = this->flash_io_metrics_;
  portEXIT_CRITICAL(&this->flash_io_metrics_lock_);
  return snapshot;
}

bool OpenQuattTrends::flush_flash_builder_(bool force) {
  if (!this->flash_switch_enabled_() || !this->flash_archive_available_() || !this->time_is_valid_()) {
    return false;
  }

  if (!this->flash_builder_.active || this->flash_builder_.sample_count == 0) {
    return true;
  }

  if (!force && this->flash_builder_.sample_count < FLASH_SAMPLES_PER_BLOCK) {
    return false;
  }

  // A failed erase/write never reaches index maintenance; do not leave the
  // previous successful index duration looking like part of this attempt.
  this->record_flash_index_update_(0U);
  const int64_t flush_started_us = esp_timer_get_time();
  const bool ok = this->write_flash_block_(this->flash_builder_);
  if (ok) {
    this->reset_flash_builder_();
  }
  this->record_flash_flush_(elapsed_operation_ms(flush_started_us));
  return ok;
}

void OpenQuattTrends::invalidate_flash_index_() {
  this->flash_archive_scanned_ = false;
  this->flash_index_count_ = 0;
}

bool OpenQuattTrends::update_flash_index_after_write_(const FlashBlockInfo& info, bool erased_sector) {
  if (!this->flash_archive_scanned_ || !this->flash_index_) {
    return false;
  }

  const uint32_t erased_sector_start = (info.slot_index / FLASH_SLOTS_PER_SECTOR) * FLASH_SLOTS_PER_SECTOR;
  const uint32_t erased_sector_end = erased_sector_start + FLASH_SLOTS_PER_SECTOR;
  size_t write_index = 0;
  for (size_t read_index = 0; read_index < this->flash_index_count_; ++read_index) {
    const FlashBlockInfo& existing = this->flash_index_[read_index];
    const bool same_slot = existing.slot_index == info.slot_index;
    const bool erased_slot =
        erased_sector && existing.slot_index >= erased_sector_start && existing.slot_index < erased_sector_end;
    if (!same_slot && !erased_slot) {
      this->flash_index_[write_index++] = existing;
    }
  }

  this->flash_index_count_ = write_index;
  if (this->flash_index_count_ >= this->flash_index_.size()) {
    return false;
  }

  this->flash_index_[this->flash_index_count_++] = info;
  if (this->flash_index_count_ > 1) {
    std::sort(this->flash_index_.data(), this->flash_index_.data() + this->flash_index_count_,
              [](const FlashBlockInfo& a, const FlashBlockInfo& b) {
                if (a.start_timestamp_ms == b.start_timestamp_ms) {
                  return a.sequence < b.sequence;
                }
                return a.start_timestamp_ms < b.start_timestamp_ms;
              });
  }

  this->rebuild_flash_metadata_from_index_();
  this->flash_archive_scanned_ = true;
  return true;
}

void OpenQuattTrends::rebuild_flash_metadata_from_index_() {
  this->flash_latest_timestamp_ms_ = 0;
  this->flash_oldest_timestamp_ms_ = 0;
  this->flash_last_flush_timestamp_ms_ = 0;
  this->flash_valid_block_count_ = 0;
  if (!this->flash_index_) {
    return;
  }

  this->flash_valid_block_count_ = static_cast<uint16_t>(std::min(this->flash_index_count_, FLASH_SLOT_COUNT));
  for (size_t index = 0; index < this->flash_index_count_; ++index) {
    const FlashBlockInfo& info = this->flash_index_[index];
    if (index == 0 || info.start_timestamp_ms < this->flash_oldest_timestamp_ms_) {
      this->flash_oldest_timestamp_ms_ = info.start_timestamp_ms;
    }
    if (index == 0 || info.end_timestamp_ms >= this->flash_latest_timestamp_ms_) {
      this->flash_latest_timestamp_ms_ = info.end_timestamp_ms;
      this->flash_last_flush_timestamp_ms_ =
          info.flush_timestamp_ms > 0 ? info.flush_timestamp_ms : info.end_timestamp_ms;
    }
  }
}

bool OpenQuattTrends::read_flash_block_(uint32_t slot_index, uint32_t expected_sequence, FlashBlockInfo* info,
                                        std::array<TrendSample, FLASH_SAMPLES_PER_BLOCK>* samples) const {
  if (info == nullptr || samples == nullptr || this->flash_partition_ == nullptr || slot_index >= FLASH_SLOT_COUNT) {
    return false;
  }

  TrendBlockHeader header{};
  const uint32_t slot_offset = slot_index * FLASH_SLOT_SIZE;
  const esp_err_t read_result = esp_partition_read(this->flash_partition_, slot_offset, &header, sizeof(header));
  if (read_result != ESP_OK || header.magic != TAG_MAGIC || header.version != TAG_VERSION || header.sample_count == 0 ||
      header.sample_count > FLASH_SAMPLES_PER_BLOCK ||
      header.payload_bytes != static_cast<uint32_t>(header.sample_count * sizeof(TrendSample))) {
    return false;
  }
  if (expected_sequence != UINT32_MAX && header.sequence != expected_sequence) {
    return false;
  }

  samples->fill(TrendSample{});
  const esp_err_t payload_result =
      esp_partition_read(this->flash_partition_, slot_offset + sizeof(header), samples->data(), header.payload_bytes);
  if (payload_result != ESP_OK) {
    return false;
  }

  if (fnv1a_hash_(reinterpret_cast<const uint8_t*>(samples->data()), header.payload_bytes) != header.crc32) {
    return false;
  }

  info->sequence = header.sequence;
  info->start_timestamp_ms = header.start_timestamp_ms;
  const size_t last_sample_index = header.sample_count > 0 ? static_cast<size_t>(header.sample_count - 1) : 0;
  info->end_timestamp_ms = (*samples)[last_sample_index].timestamp_ms;
  info->flush_timestamp_ms = header.reserved > 0 ? static_cast<uint64_t>(header.reserved) * 1000ULL : 0ULL;
  info->sample_count = header.sample_count;
  info->slot_index = slot_index;
  return true;
}

void OpenQuattTrends::reset_interval_samples_() {
  reset_interval_metric_samples_(this->flow_interval_);
  reset_interval_metric_samples_(this->input_w_interval_);
  reset_interval_metric_samples_(this->output_w_interval_);
}

void OpenQuattTrends::reset_interval_filters_() {
  this->reset_interval_samples_();
  this->flow_interval_.last_saved = 0.0f;
  this->input_w_interval_.last_saved = 0.0f;
  this->output_w_interval_.last_saved = 0.0f;
}

void OpenQuattTrends::capture_sample(float outside_c, float supply_c, float room_c, float room_setpoint_c,
                                     float flow_lph, float input_w, float output_w, bool force) {
  if (!this->capture_enabled_()) {
    return;
  }

  this->update_interval_metric_(this->flow_interval_, flow_lph);
  this->update_interval_metric_(this->input_w_interval_, input_w);
  this->update_interval_metric_(this->output_w_interval_, output_w);

  const uint32_t now_monotonic_ms = static_cast<uint32_t>(millis());
  if (!force && this->last_capture_ms_ != 0 &&
      static_cast<uint32_t>(now_monotonic_ms - static_cast<uint32_t>(this->last_capture_ms_)) < SAMPLE_INTERVAL_MS) {
    return;
  }

  const float flow_to_save = select_interval_metric_value_(this->flow_interval_, flow_lph);
  const float input_w_to_save = select_interval_metric_value_(this->input_w_interval_, input_w);
  const float output_w_to_save = select_interval_metric_value_(this->output_w_interval_, output_w);

  const uint64_t now_ms = this->current_time_ms_();
  const TrendValues values =
      this->pack_values_(outside_c, supply_c, room_c, room_setpoint_c, flow_to_save, input_w_to_save, output_w_to_save);
  const bool any_valid = values.outside_c_x10 != INT16_MIN || values.supply_c_x10 != INT16_MIN ||
                         values.room_c_x10 != INT16_MIN || values.room_setpoint_c_x10 != INT16_MIN ||
                         values.flow_lph != UINT16_MAX || values.input_w != UINT16_MAX || values.output_w != UINT16_MAX;
  if (!any_valid) {
    this->last_capture_ms_ = now_monotonic_ms;
    this->reset_interval_samples_();
    return;
  }

  const TrendSample sample = this->make_sample_(now_ms, values);
  this->push_ram_sample_(sample);
  this->last_capture_ms_ = now_monotonic_ms;

  if (this->flash_switch_enabled_()) {
    this->append_sample_to_flash_(sample);
  }

  update_last_saved_metric_(this->flow_interval_, flow_to_save);
  update_last_saved_metric_(this->input_w_interval_, input_w_to_save);
  update_last_saved_metric_(this->output_w_interval_, output_w_to_save);
  this->reset_interval_samples_();
}

void OpenQuattTrends::set_flash_enabled(bool enabled) {
  if (enabled) {
    this->flash_enabled_ = true;
    this->flash_archive_scanned_ = false;
    this->load_archive_if_needed_();
    return;
  }

  this->reset_flash_builder_();
  this->flash_dirty_ = false;
  this->flash_enabled_ = false;
}

bool OpenQuattTrends::force_flush() {
  if (!this->flash_switch_enabled_()) {
    return false;
  }
  return this->flush_flash_builder_(true);
}

void OpenQuattTrends::clear_history() {
  this->ram_head_ = 0;
  this->ram_count_ = 0;
  this->last_capture_ms_ = 0;
  this->flash_dirty_ = false;
  this->flash_archive_seeded_ = false;
  this->reset_interval_filters_();
  this->reset_flash_builder_();
}

uint64_t OpenQuattTrends::get_window_cutoff_ms_(uint32_t window_hours) const {
  const uint64_t window_ms = static_cast<uint64_t>(std::max<uint32_t>(1, window_hours)) * 60ULL * 60ULL * 1000ULL;
  const uint64_t now_ms = this->current_time_ms_();
  return now_ms > window_ms ? (now_ms - window_ms) : 0ULL;
}

uint32_t OpenQuattTrends::get_window_stride_(uint32_t window_hours) const {
  if (window_hours <= 24) {
    return 1;
  }
  if (window_hours <= 72) {
    return 3;
  }
  if (window_hours <= 168) {
    return 6;
  }
  if (window_hours <= 336) {
    return 12;
  }
  return 24;
}

uint64_t OpenQuattTrends::get_latest_archive_timestamp_ms_() const {
  if (this->flash_latest_timestamp_ms_ > 0) {
    return this->flash_latest_timestamp_ms_;
  }
  TrendSample sample{};
  if (this->ram_count_ == 0 || !this->get_ram_sample_at_(this->ram_count_ - 1, &sample)) {
    return 0;
  }
  return sample.timestamp_ms;
}

void OpenQuattTrends::update_flash_metadata_(uint64_t latest_timestamp_ms) {
  this->flash_latest_timestamp_ms_ = latest_timestamp_ms;
}

void OpenQuattTrends::reset_flash_metadata_() {
  this->flash_latest_timestamp_ms_ = 0;
  this->flash_oldest_timestamp_ms_ = 0;
  this->flash_last_flush_timestamp_ms_ = 0;
  this->flash_valid_block_count_ = 0;
  this->flash_index_count_ = 0;
}

uint64_t OpenQuattTrends::get_flash_oldest_timestamp_ms_() const { return this->flash_oldest_timestamp_ms_; }

uint64_t OpenQuattTrends::get_flash_newest_timestamp_ms_() const { return this->flash_latest_timestamp_ms_; }

uint64_t OpenQuattTrends::get_flash_last_flush_timestamp_ms_() const { return this->flash_last_flush_timestamp_ms_; }

std::string OpenQuattTrends::format_flash_absolute_time_(uint64_t timestamp_ms) const {
  if (timestamp_ms == 0) {
    return "Nog niet";
  }

  if (!this->time_is_valid_()) {
    return "Wacht op tijdsync";
  }

  auto formatted = ESPTime::from_epoch_local(static_cast<time_t>(timestamp_ms / 1000ULL)).strftime("%d-%m %H:%M");
  return formatted.empty() || formatted == "ERROR" ? "—" : formatted;
}

std::string OpenQuattTrends::format_flash_relative_age_(uint64_t timestamp_ms) const {
  if (timestamp_ms == 0) {
    return "Nog leeg";
  }

  const uint64_t now_ms = this->current_time_ms_();
  if (now_ms <= timestamp_ms) {
    return "Zojuist";
  }

  const uint64_t age_minutes = (now_ms - timestamp_ms) / (60ULL * 1000ULL);
  if (age_minutes < 1) {
    return "Zojuist";
  }
  if (age_minutes < 60) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%llu min geleden", static_cast<unsigned long long>(age_minutes));
    return buffer;
  }

  const uint64_t age_hours = age_minutes / 60ULL;
  if (age_hours < 24) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%llu u geleden", static_cast<unsigned long long>(age_hours));
    return buffer;
  }

  const uint64_t age_days = age_hours / 24ULL;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%llu d geleden", static_cast<unsigned long long>(age_days));
  return buffer;
}

std::string OpenQuattTrends::format_flash_available_span_(uint64_t oldest_timestamp_ms,
                                                          uint64_t newest_timestamp_ms) const {
  if (oldest_timestamp_ms == 0 || newest_timestamp_ms == 0 || newest_timestamp_ms <= oldest_timestamp_ms) {
    return "Opgeslagen in flash: leeg";
  }

  const float span_days = static_cast<float>(newest_timestamp_ms - oldest_timestamp_ms) /
                          static_cast<float>(24ULL * 60ULL * 60ULL * 1000ULL);
  if (span_days >= 1.0f) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "Opgeslagen in flash: %.1f dagen", span_days);
    std::string value(buffer);
    std::replace(value.begin(), value.end(), '.', ',');
    return value;
  }

  const uint64_t span_hours = (newest_timestamp_ms - oldest_timestamp_ms) / (60ULL * 60ULL * 1000ULL);
  if (span_hours >= 1) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "Opgeslagen in flash: %llu uur", static_cast<unsigned long long>(span_hours));
    return buffer;
  }

  const uint64_t span_minutes = (newest_timestamp_ms - oldest_timestamp_ms) / (60ULL * 1000ULL);
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "Opgeslagen in flash: %llu min", static_cast<unsigned long long>(span_minutes));
  return buffer;
}

bool OpenQuattTrends::write_sample_line_(ChunkedTextWriter* writer, const TrendSample& sample) const {
  if (writer == nullptr) {
    return false;
  }
  char outside[16];
  char supply[16];
  char room[16];
  char setpoint[16];
  char flow[12];
  char input[12];
  char output[12];
  format_temperature_x10(sample.values.outside_c_x10, outside, sizeof(outside));
  format_temperature_x10(sample.values.supply_c_x10, supply, sizeof(supply));
  format_temperature_x10(sample.values.room_c_x10, room, sizeof(room));
  format_temperature_x10(sample.values.room_setpoint_c_x10, setpoint, sizeof(setpoint));
  format_unsigned_metric(sample.values.flow_lph, flow, sizeof(flow));
  format_unsigned_metric(sample.values.input_w, input, sizeof(input));
  format_unsigned_metric(sample.values.output_w, output, sizeof(output));
  return writer->printf("%llu|%s|%s|%s|%s|%s|%s|%s\n", static_cast<unsigned long long>(sample.timestamp_ms), outside,
                        supply, room, setpoint, flow, input, output);
}

void OpenQuattTrends::write_samples_for_history_(ChunkedTextWriter* writer, uint32_t window_hours) {
  const uint64_t cutoff_ms = this->get_window_cutoff_ms_(window_hours);
  const uint32_t stride = this->get_window_stride_(window_hours);
  if (this->flash_archive_available_() && !this->flash_archive_scanned_) {
    this->scan_flash_archive_();
  }
  const uint64_t oldest_ram_timestamp_ms = this->get_ram_oldest_timestamp_ms_();
  uint64_t emitted_flash_latest = 0;

  uint64_t window_index = 0;
  bool has_last_sample = false;
  bool last_sample_emitted = false;
  TrendSample last_sample{};

  auto emit_sample = [&](const TrendSample& sample, bool count_only) {
    if (sample.timestamp_ms < cutoff_ms) {
      return false;
    }

    last_sample = sample;
    has_last_sample = true;
    const bool should_emit = (window_index % stride) == 0;
    ++window_index;
    if (should_emit && !count_only) {
      this->write_sample_line_(writer, sample);
      last_sample_emitted = true;
    }
    return should_emit;
  };

  const bool ram_covers_window = oldest_ram_timestamp_ms != 0 && oldest_ram_timestamp_ms <= cutoff_ms;
  const bool should_read_flash_archive =
      this->flash_archive_scanned_ && this->flash_index_count_ > 0 && !ram_covers_window;
  if (should_read_flash_archive) {
    for (size_t block_index = 0; block_index < this->flash_index_count_; ++block_index) {
      const FlashBlockInfo& indexed = this->flash_index_[block_index];
      if (indexed.end_timestamp_ms < cutoff_ms) {
        continue;
      }
      std::array<TrendSample, FLASH_SAMPLES_PER_BLOCK> block_samples{};
      FlashBlockInfo info{};
      if (!this->read_flash_block_(indexed.slot_index, indexed.sequence, &info, &block_samples)) {
        continue;
      }
      for (size_t index = 0; index < info.sample_count; ++index) {
        const TrendSample& sample = block_samples[index];
        emit_sample(sample, false);
        if (sample.timestamp_ms >= cutoff_ms && sample.timestamp_ms > emitted_flash_latest) {
          emitted_flash_latest = sample.timestamp_ms;
        }
      }
    }
  }

  const uint64_t ram_cutoff = emitted_flash_latest > 0 ? std::max(cutoff_ms, emitted_flash_latest + 1ULL) : cutoff_ms;
  for (size_t index = 0; index < this->ram_count_; ++index) {
    TrendSample sample{};
    if (!this->get_ram_sample_at_(index, &sample)) {
      continue;
    }
    if (sample.timestamp_ms < ram_cutoff) {
      continue;
    }
    emit_sample(sample, false);
  }

  if (has_last_sample && !last_sample_emitted) {
    this->write_sample_line_(writer, last_sample);
  }
}

void OpenQuattTrends::write_history(httpd_req_t* req, uint32_t window_hours) {
  if (req == nullptr) {
    return;
  }

  const uint64_t now_ms = this->current_time_ms_();
  ChunkedTextWriter writer(req);
  if (!writer.printf("@now|%llu\n", static_cast<unsigned long long>(now_ms)) || !writer.flush()) {
    ESP_LOGW(TAG, "Failed to start trend history response");
    return;
  }
  this->write_samples_for_history_(&writer, window_hours);
  if (!writer.flush() || httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to terminate trend history response");
  }
}

void OpenQuattTrends::write_metadata(httpd_req_t* req) {
  if (req == nullptr) {
    return;
  }

  const uint64_t now_ms = this->current_time_ms_();
  const std::string available = this->get_flash_available_label();
  const std::string oldest = this->get_flash_oldest_point_label();
  const std::string newest = this->get_flash_newest_point_label();
  const std::string last_flush = this->get_flash_last_flush_label();
  const uint32_t size_kib_x10 = static_cast<uint32_t>(std::round(this->get_flash_storage_kib() * 10.0f));
  const FlashIOMetrics flash_io_metrics = this->snapshot_flash_io_metrics_();
  ChunkedTextWriter writer(req);
  if (!writer.printf("@now|%llu\n", static_cast<unsigned long long>(now_ms)) ||
      !writer.printf("@flash|%s|%s|%s|%s|%u.%u|%u\n", available.c_str(), oldest.c_str(), newest.c_str(),
                     last_flush.c_str(), static_cast<unsigned>(size_kib_x10 / 10U),
                     static_cast<unsigned>(size_kib_x10 % 10U), static_cast<unsigned>(this->get_flash_write_count())) ||
      !writer.printf("@flash_io|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u\n",
                     static_cast<unsigned>(flash_io_metrics.erase_count),
                     static_cast<unsigned>(flash_io_metrics.erase_failure_count),
                     static_cast<unsigned>(flash_io_metrics.last_erase_duration_ms),
                     static_cast<unsigned>(flash_io_metrics.max_erase_duration_ms),
                     static_cast<unsigned>(flash_io_metrics.write_failure_count),
                     static_cast<unsigned>(flash_io_metrics.last_write_duration_ms),
                     static_cast<unsigned>(flash_io_metrics.max_write_duration_ms),
                     static_cast<unsigned>(flash_io_metrics.last_flush_duration_ms),
                     static_cast<unsigned>(flash_io_metrics.max_flush_duration_ms),
                     static_cast<unsigned>(flash_io_metrics.last_index_update_duration_ms),
                     static_cast<unsigned>(flash_io_metrics.max_index_update_duration_ms)) ||
      !writer.flush() || httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to write trend history metadata response");
  }
}

std::string OpenQuattTrends::get_flash_available_label() const {
  if (!this->flash_archive_available_()) {
    return "Niet beschikbaar";
  }

  const uint64_t oldest_timestamp_ms = this->get_flash_oldest_timestamp_ms_();
  const uint64_t newest_timestamp_ms = this->get_flash_newest_timestamp_ms_();
  return this->format_flash_available_span_(oldest_timestamp_ms, newest_timestamp_ms);
}

std::string OpenQuattTrends::get_flash_oldest_point_label() const {
  if (!this->flash_archive_available_()) {
    return "—";
  }
  return this->format_flash_absolute_time_(this->get_flash_oldest_timestamp_ms_());
}

std::string OpenQuattTrends::get_flash_newest_point_label() const {
  if (!this->flash_archive_available_()) {
    return "—";
  }
  return this->format_flash_relative_age_(this->get_flash_newest_timestamp_ms_());
}

std::string OpenQuattTrends::get_flash_last_flush_label() const {
  if (!this->flash_archive_available_()) {
    return "—";
  }
  return this->format_flash_absolute_time_(this->get_flash_last_flush_timestamp_ms_());
}

float OpenQuattTrends::get_flash_storage_kib() const {
  if (!this->flash_archive_available_()) {
    return 0.0f;
  }
  return static_cast<float>(static_cast<uint32_t>(this->flash_valid_block_count_) * FLASH_SLOT_SIZE) / 1024.0f;
}

uint32_t OpenQuattTrends::get_flash_write_count() const {
  return this->flash_archive_available_() ? this->next_flash_sequence_ : 0U;
}

}  // namespace openquatt_trends
}  // namespace esphome
