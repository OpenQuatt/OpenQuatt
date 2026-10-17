#include "OpenQuattEnergyHistory.h"

#include "OpenQuattHttpStatus.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_random.h"
#include "lwip/sockets.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_energy_history {

static const char* const TAG = "openquatt.energy_history";

namespace {

static std::string base64_encode_bytes_(const uint8_t* data, size_t length) {
  static constexpr char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((length + 2U) / 3U) * 4U);

  for (size_t i = 0; i < length; i += 3U) {
    const uint32_t byte_a = data[i];
    const uint32_t byte_b = i + 1U < length ? data[i + 1U] : 0U;
    const uint32_t byte_c = i + 2U < length ? data[i + 2U] : 0U;
    const uint32_t triple = (byte_a << 16U) | (byte_b << 8U) | byte_c;

    out.push_back(TABLE[(triple >> 18U) & 0x3FU]);
    out.push_back(TABLE[(triple >> 12U) & 0x3FU]);
    out.push_back(i + 1U < length ? TABLE[(triple >> 6U) & 0x3FU] : '=');
    out.push_back(i + 2U < length ? TABLE[triple & 0x3FU] : '=');
  }

  return out;
}

static void fill_random_token_(std::array<uint8_t, 32>* key) {
  if (key == nullptr) {
    return;
  }
  for (size_t i = 0; i < key->size(); i += sizeof(uint32_t)) {
    const uint32_t rnd = esp_random();
    for (size_t byte_index = 0; byte_index < sizeof(uint32_t) && i + byte_index < key->size(); ++byte_index) {
      (*key)[i + byte_index] = static_cast<uint8_t>(rnd >> (byte_index * 8U));
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

static bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) {
    return false;
  }
  const size_t path_len = std::strlen(path);
  return std::strncmp(url, path, path_len) == 0 && (url[path_len] == '\0' || url[path_len] == '?');
}

static bool read_query_value(httpd_req_t* req, const char* key, char* value, size_t value_size) {
  if (req == nullptr || key == nullptr || value == nullptr || value_size == 0U) {
    return false;
  }
  const size_t query_len = httpd_req_get_url_query_len(req);
  if (query_len == 0U || query_len >= 192U) {
    return false;
  }
  char query[192];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return false;
  }
  return httpd_query_key_value(query, key, value, value_size) == ESP_OK;
}

static uint32_t read_query_date_key(httpd_req_t* req, const char* key) {
  char value[16];
  if (!read_query_value(req, key, value, sizeof(value))) {
    return 0;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (end == value || *end != '\0' || parsed < 20200101UL || parsed > 20991231UL) {
    return 0;
  }
  return static_cast<uint32_t>(parsed);
}

static bool read_query_flag(httpd_req_t* req, const char* key, bool default_value = false) {
  char value[8];
  if (!read_query_value(req, key, value, sizeof(value))) {
    return default_value;
  }
  return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 || std::strcmp(value, "yes") == 0;
}

enum class EnergyHistoryExportMode : uint8_t {
  DAYS,
  DAYS_AND_HOURS,
  HOURS,
};

static EnergyHistoryExportMode read_export_mode(httpd_req_t* req) {
  char value[24];
  if (!read_query_value(req, "mode", value, sizeof(value))) {
    return EnergyHistoryExportMode::DAYS_AND_HOURS;
  }
  if (std::strcmp(value, "days") == 0 || std::strcmp(value, "daily") == 0) {
    return EnergyHistoryExportMode::DAYS;
  }
  if (std::strcmp(value, "hours") == 0 || std::strcmp(value, "hourly") == 0) {
    return EnergyHistoryExportMode::HOURS;
  }
  return EnergyHistoryExportMode::DAYS_AND_HOURS;
}

static const char* export_mode_id(EnergyHistoryExportMode mode) {
  switch (mode) {
    case EnergyHistoryExportMode::DAYS:
      return "days";
    case EnergyHistoryExportMode::HOURS:
      return "hours";
    case EnergyHistoryExportMode::DAYS_AND_HOURS:
    default:
      return "days_and_hours";
  }
}

static bool export_mode_includes_days(EnergyHistoryExportMode mode) {
  return mode == EnergyHistoryExportMode::DAYS || mode == EnergyHistoryExportMode::DAYS_AND_HOURS;
}

static bool export_mode_includes_hours(EnergyHistoryExportMode mode) {
  return mode == EnergyHistoryExportMode::HOURS || mode == EnergyHistoryExportMode::DAYS_AND_HOURS;
}

static std::string trim_copy(const std::string& value) {
  size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1U]))) {
    --last;
  }
  return value.substr(first, last - first);
}

}  // namespace

class ChunkedTextWriter {
 public:
  explicit ChunkedTextWriter(httpd_req_t* req) : req_(req) {}

  bool begin_response(const char* content_type, bool download = false) {
    if (!this->scratch_.allocate_external(192U)) {
      httpd_resp_set_status(this->req_, "503 Service Unavailable");
      httpd_resp_set_type(this->req_, "application/json");
      httpd_resp_set_hdr(this->req_, "Cache-Control", "no-store");
      httpd_resp_send(this->req_, R"({"ok":false,"error":"serialization_unavailable"})", HTTPD_RESP_USE_STRLEN);
      return false;
    }
    httpd_resp_set_status(this->req_, HTTPD_200);
    httpd_resp_set_type(this->req_, content_type);
    httpd_resp_set_hdr(this->req_, "Cache-Control", "no-store");
    if (download) {
      httpd_resp_set_hdr(this->req_, "Content-Disposition", "attachment; filename=\"openquatt-energy-history.json\"");
    }
    return true;
  }

  bool printf(const char* format, ...) {
    if (!this->scratch_ || this->failed_) {
      return false;
    }
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(this->scratch_.data(), this->scratch_.size(), format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= this->scratch_.size()) {
      return this->fail_();
    }
    if (httpd_resp_send_chunk(this->req_, this->scratch_.data(), static_cast<ssize_t>(written)) != ESP_OK) {
      return this->fail_();
    }
    return true;
  }

 private:
  bool fail_() {
    this->failed_ = true;
    // Abort this active request synchronously; HTTPD retains descriptor ownership.
    shutdown(httpd_req_to_sockfd(this->req_), SHUT_RDWR);
    return false;
  }

  httpd_req_t* req_;
  openquatt_common::PsramBuffer<char> scratch_;
  bool failed_{false};
};

class OpenQuattEnergyHistoryRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattEnergyHistoryRequestHandler(OpenQuattEnergyHistory* parent) : parent_(parent) {}

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

  bool canHandle(AsyncWebServerRequest* request) const override { return route_(request) != Route::NOT_FOUND; }

  void handleRequest(AsyncWebServerRequest* request) override {
    if (!this->parent_->runtime_storage_ready()) {
      openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::SERVICE_UNAVAILABLE,
                                         R"({"ok":false,"error":"psram_unavailable"})");
      return;
    }
    switch (route_(request)) {
      case Route::IMPORT:
        this->handle_import_(request);
        return;
      case Route::EXPORT:
        this->parent_->write_history_export(*request);
        return;
      case Route::HISTORY:
        this->parent_->write_history(*request);
        return;
      case Route::NOT_FOUND:
        request->send(404);
        return;
    }
  }

 protected:
  enum class Route : uint8_t { NOT_FOUND, IMPORT, EXPORT, HISTORY };

  __attribute__((noinline)) static Route route_(AsyncWebServerRequest* request) {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    if (url_path_matches(url_buf, "/energy/history/import") && request->method() == HTTP_POST) {
      return Route::IMPORT;
    }
    if (url_path_matches(url_buf, "/energy/history/export") && request->method() == HTTP_GET) {
      return Route::EXPORT;
    }
    if (url_path_matches(url_buf, "/energy/history") && request->method() == HTTP_GET) {
      return Route::HISTORY;
    }
    return Route::NOT_FOUND;
  }

  __attribute__((noinline)) void handle_import_(AsyncWebServerRequest* request) {
    if (!this->passes_same_origin_(request) || !this->passes_csrf_(request)) {
      request->send(409, "application/json", R"({"ok":false,"error":"forbidden"})");
      return;
    }
    const std::string response = this->parent_->import_history_records(request->arg("records"));
    request->send(200, "application/json", response.c_str());
  }

  OpenQuattEnergyHistory* parent_;
};

void OpenQuattEnergyHistory::setup() {
  this->rotate_csrf_token_();

  if (!this->external_state_storage_.allocate()) {
    ESP_LOGE(TAG, "Could not allocate %u-byte energy history state in PSRAM",
             static_cast<unsigned>(sizeof(ExternalState)));
    this->mark_failed();
    if (web_server_base::global_web_server_base != nullptr) {
      web_server_base::global_web_server_base->add_handler(new OpenQuattEnergyHistoryRequestHandler(this));
    }
    return;
  }

  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    return;
  }

  this->flash_partition_ =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "openquatt_data");
  if (this->flash_partition_ == nullptr) {
    ESP_LOGW(TAG, "Energy history flash partition 'openquatt_data' not found");
  } else {
    const size_t available_bytes =
        this->flash_partition_->size > BASE_OFFSET ? this->flash_partition_->size - BASE_OFFSET : 0U;
    const size_t sector_count = std::min(MAX_FLASH_SECTOR_COUNT, available_bytes / FLASH_SECTOR_SIZE);
    if (sector_count == 0U) {
      ESP_LOGW(TAG, "Energy history flash window does not fit in 'openquatt_data' (partition=%u, base=%u)",
               static_cast<unsigned>(this->flash_partition_->size), static_cast<unsigned>(BASE_OFFSET));
    } else {
      this->flash_sector_count_ = sector_count;
      this->flash_slot_count_ = (FLASH_SECTOR_SIZE / FLASH_SLOT_SIZE) * this->flash_sector_count_;
      this->flash_total_bytes_ = FLASH_SECTOR_SIZE * this->flash_sector_count_;
      this->partition_available_ = true;
      this->scan_archive_();
      this->configure_hour_flash_window_();
      this->scan_hour_archive_();
    }
  }

  web_server_base::global_web_server_base->add_handler(new OpenQuattEnergyHistoryRequestHandler(this));
}

void OpenQuattEnergyHistory::loop() {}

void OpenQuattEnergyHistory::rotate_csrf_token_() {
  std::array<uint8_t, 32> token_bytes{};
  fill_random_token_(&token_bytes);
  this->csrf_token_ = base64_encode_bytes_(token_bytes.data(), token_bytes.size());
}

void OpenQuattEnergyHistory::on_shutdown() {
  if (this->enabled_() && this->has_current_day_ && record_has_values_(this->current_values_)) {
    this->write_record_(this->active_date_key_, this->current_values_, true);
    this->write_hour_day_record_(this->active_date_key_, true);
  }
}

bool OpenQuattEnergyHistory::force_flush() {
  if (!this->enabled_() || !this->has_current_day_ || !record_has_values_(this->current_values_)) {
    return false;
  }
  const bool day_written = this->write_record_(this->active_date_key_, this->current_values_, true);
  const bool hours_written = this->write_hour_day_record_(this->active_date_key_, true);
  return day_written || hours_written;
}

void OpenQuattEnergyHistory::refresh_hourly_retention() {
  if (!this->runtime_storage_ready()) {
    return;
  }
  this->configure_hour_flash_window_();
  this->scan_hour_archive_();
}

void OpenQuattEnergyHistory::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt energy history");
  ESP_LOGCONFIG(TAG, "  Clock: %s", this->clock_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Enabled switch: %s", this->enabled_switch_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Runtime state: %s (%u bytes)", this->runtime_storage_ready() ? "PSRAM" : "<allocation failed>",
                static_cast<unsigned>(sizeof(ExternalState)));
  ESP_LOGCONFIG(TAG, "  Flash partition: %s", this->is_partition_ready_() ? "configured" : "<missing>");
  ESP_LOGCONFIG(TAG, "  Flash window: %u KiB / %u slots", static_cast<unsigned>(this->flash_total_bytes_ / 1024U),
                static_cast<unsigned>(this->flash_slot_count_));
  ESP_LOGCONFIG(TAG, "  Hour detail window: %u KiB / %u days / requested %u days",
                static_cast<unsigned>(this->hour_flash_total_bytes_ / 1024U),
                static_cast<unsigned>(this->hour_flash_slot_count_),
                static_cast<unsigned>(this->hour_flash_requested_retention_days_));
  ESP_LOGCONFIG(TAG, "  Records: %u / days: %u", static_cast<unsigned>(this->record_count_),
                static_cast<unsigned>(this->stored_day_count_));
  ESP_LOGCONFIG(TAG, "  Hour records: %u / %u", static_cast<unsigned>(this->get_hour_record_count_()),
                static_cast<unsigned>(HOURLY_SLOT_COUNT));
  ESP_LOGCONFIG(TAG, "  Hour detail flash records: %u / days: %u",
                static_cast<unsigned>(this->hour_flash_record_count_),
                static_cast<unsigned>(this->hour_flash_stored_day_count_));
}

float OpenQuattEnergyHistory::get_setup_priority() const { return setup_priority::WIFI; }

bool OpenQuattEnergyHistory::enabled_() const {
  return this->enabled_switch_ != nullptr && this->enabled_switch_->state;
}

bool OpenQuattEnergyHistory::time_is_valid_() const {
  return this->clock_ != nullptr && this->clock_->now().is_valid();
}

uint64_t OpenQuattEnergyHistory::current_time_ms_() const {
  if (this->time_is_valid_()) {
    const auto now = this->clock_->now();
    return static_cast<uint64_t>(now.timestamp) * 1000ULL;
  }
  return static_cast<uint64_t>(millis());
}

uint32_t OpenQuattEnergyHistory::get_current_date_key_() const {
  if (!this->time_is_valid_()) {
    return 0;
  }
  const auto now = this->clock_->now();
  return (static_cast<uint32_t>(now.year) * 10000U) + (static_cast<uint32_t>(now.month) * 100U) +
         static_cast<uint32_t>(now.day_of_month);
}

uint8_t OpenQuattEnergyHistory::get_current_hour_() const {
  if (!this->time_is_valid_()) {
    return 0;
  }
  return static_cast<uint8_t>(this->clock_->now().hour);
}

bool OpenQuattEnergyHistory::is_partition_ready_() const {
  return this->runtime_storage_ready() && this->partition_available_ && this->flash_partition_ != nullptr;
}

uint32_t OpenQuattEnergyHistory::encode_kwh_(float value) {
  if (!std::isfinite(value) || value < 0.0f) {
    return UNKNOWN_WH;
  }
  const float wh = value * 1000.0f;
  if (wh >= static_cast<float>(UNKNOWN_WH - 1U)) {
    return UNKNOWN_WH - 1U;
  }
  return static_cast<uint32_t>(std::round(wh));
}

int32_t OpenQuattEnergyHistory::printable_wh_(uint32_t value) {
  return value == UNKNOWN_WH ? -1 : static_cast<int32_t>(value);
}

uint32_t OpenQuattEnergyHistory::fnv1a_hash_(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < len; ++index) {
    hash ^= data[index];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t OpenQuattEnergyHistory::record_crc_(const EnergyHistoryRecord& record) {
  return fnv1a_hash_(reinterpret_cast<const uint8_t*>(&record), offsetof(EnergyHistoryRecord, crc32));
}

uint32_t OpenQuattEnergyHistory::hour_day_record_crc_(const EnergyHistoryHourDayRecord& record) {
  return fnv1a_hash_(reinterpret_cast<const uint8_t*>(&record), offsetof(EnergyHistoryHourDayRecord, crc32));
}

bool OpenQuattEnergyHistory::record_has_values_(const EnergyHistoryValues& values) {
  return values.electrical_input_wh != UNKNOWN_WH || values.heating_input_wh != UNKNOWN_WH ||
         values.cooling_input_wh != UNKNOWN_WH || values.heatpump_heat_output_wh != UNKNOWN_WH ||
         values.heatpump_cooling_output_wh != UNKNOWN_WH || values.boiler_heat_output_wh != UNKNOWN_WH ||
         values.system_heat_output_wh != UNKNOWN_WH;
}

bool OpenQuattEnergyHistory::date_key_valid_(uint32_t date_key) {
  const uint16_t year = static_cast<uint16_t>(date_key / 10000U);
  const uint8_t month = static_cast<uint8_t>((date_key / 100U) % 100U);
  const uint8_t day = static_cast<uint8_t>(date_key % 100U);
  if (year < MIN_DATE_YEAR || year > MAX_DATE_YEAR || month < 1U || month > 12U || day < 1U) {
    return false;
  }

  static constexpr uint8_t days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t max_day = days_per_month[month - 1U];
  const bool leap_year = (year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U);
  if (month == 2U && leap_year) {
    max_day = 29U;
  }
  return day <= max_day;
}

bool OpenQuattEnergyHistory::date_bitmap_index_(uint32_t date_key, size_t* byte_index, uint8_t* bit_mask) {
  if (byte_index == nullptr || bit_mask == nullptr || !date_key_valid_(date_key)) {
    return false;
  }
  const uint16_t year = static_cast<uint16_t>(date_key / 10000U);
  const uint8_t month = static_cast<uint8_t>((date_key / 100U) % 100U);
  const uint8_t day = static_cast<uint8_t>(date_key % 100U);
  const size_t date_index =
      ((static_cast<size_t>(year - MIN_DATE_YEAR) * 12U) + static_cast<size_t>(month - 1U)) * 31U +
      static_cast<size_t>(day - 1U);
  *byte_index = date_index / 8U;
  *bit_mask = static_cast<uint8_t>(1U << (date_index % 8U));
  return *byte_index < DATE_BITMAP_BYTES;
}

bool OpenQuattEnergyHistory::date_bitmap_get_(const uint8_t* bitmap, uint32_t date_key) {
  if (bitmap == nullptr) {
    return false;
  }
  size_t byte_index = 0;
  uint8_t bit_mask = 0;
  return date_bitmap_index_(date_key, &byte_index, &bit_mask) && (bitmap[byte_index] & bit_mask) != 0U;
}

void OpenQuattEnergyHistory::date_bitmap_set_(uint8_t* bitmap, uint32_t date_key) {
  if (bitmap == nullptr) {
    return;
  }
  size_t byte_index = 0;
  uint8_t bit_mask = 0;
  if (date_bitmap_index_(date_key, &byte_index, &bit_mask)) {
    bitmap[byte_index] |= bit_mask;
  }
}

bool OpenQuattEnergyHistory::parse_import_uint32_(const std::string& value, uint32_t* out) {
  if (out == nullptr) {
    return false;
  }
  const std::string trimmed = trim_copy(value);
  if (trimmed.empty() || trimmed[0] == '-') {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(trimmed.c_str(), &end, 10);
  if (end == trimmed.c_str() || *end != '\0' || parsed >= UNKNOWN_WH) {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

std::vector<std::string> OpenQuattEnergyHistory::split_import_fields_(const std::string& line) {
  std::vector<std::string> fields;
  size_t start = 0;
  while (start <= line.size()) {
    const size_t separator = line.find('|', start);
    const size_t end = separator == std::string::npos ? line.size() : separator;
    fields.push_back(trim_copy(line.substr(start, end - start)));
    if (separator == std::string::npos) {
      break;
    }
    start = separator + 1U;
  }
  return fields;
}

uint32_t OpenQuattEnergyHistory::add_wh_(uint32_t base, uint32_t delta) {
  if (delta == UNKNOWN_WH) {
    return base;
  }
  if (base == UNKNOWN_WH) {
    return delta;
  }
  if (base >= UNKNOWN_WH - delta) {
    return UNKNOWN_WH - 1U;
  }
  return base + delta;
}

uint32_t OpenQuattEnergyHistory::delta_wh_(uint32_t current, uint32_t previous) {
  if (current == UNKNOWN_WH || previous == UNKNOWN_WH || current < previous) {
    return UNKNOWN_WH;
  }
  return current - previous;
}

std::string OpenQuattEnergyHistory::format_date_key_(uint32_t date_key) {
  if (date_key == 0) {
    return "Geen data";
  }
  const uint32_t year = date_key / 10000U;
  const uint32_t month = (date_key / 100U) % 100U;
  const uint32_t day = date_key % 100U;
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%02u-%02u-%04u", static_cast<unsigned>(day), static_cast<unsigned>(month),
                static_cast<unsigned>(year));
  return buffer;
}

std::string OpenQuattEnergyHistory::format_date_key_iso_(uint32_t date_key) {
  if (date_key == 0) {
    return "";
  }
  const uint32_t year = date_key / 10000U;
  const uint32_t month = (date_key / 100U) % 100U;
  const uint32_t day = date_key % 100U;
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", static_cast<unsigned>(year), static_cast<unsigned>(month),
                static_cast<unsigned>(day));
  return buffer;
}

bool OpenQuattEnergyHistory::write_export_wh_field_(ChunkedTextWriter* writer, const char* field, uint32_t value) {
  if (value == UNKNOWN_WH) {
    return true;
  }
  if (writer == nullptr || field == nullptr) {
    return false;
  }
  return writer->printf(",\"%s\":%u", field, static_cast<unsigned>(value));
}

bool OpenQuattEnergyHistory::date_key_in_range_(uint32_t date_key, uint32_t from_date_key, uint32_t to_date_key) {
  return date_key != 0U && (from_date_key == 0U || date_key >= from_date_key) &&
         (to_date_key == 0U || date_key <= to_date_key);
}

uint16_t OpenQuattEnergyHistory::parse_hourly_retention_days_(const std::string& option) {
  if (option.find("365") != std::string::npos) {
    return 365;
  }
  if (option.find("180") != std::string::npos) {
    return 180;
  }
  if (option.find("90") != std::string::npos) {
    return 90;
  }
  if (option.find("30") != std::string::npos) {
    return 30;
  }
  return DEFAULT_FLASH_HOURLY_RETENTION_DAYS;
}

OpenQuattEnergyHistory::EnergyHistoryValues OpenQuattEnergyHistory::pack_values_(
    float electrical_input_kwh, float heating_input_kwh, float cooling_input_kwh, float heatpump_heat_output_kwh,
    float heatpump_cooling_output_kwh, float boiler_heat_output_kwh, float system_heat_output_kwh) const {
  return EnergyHistoryValues{
      encode_kwh_(electrical_input_kwh),        encode_kwh_(heating_input_kwh),
      encode_kwh_(cooling_input_kwh),           encode_kwh_(heatpump_heat_output_kwh),
      encode_kwh_(heatpump_cooling_output_kwh), encode_kwh_(boiler_heat_output_kwh),
      encode_kwh_(system_heat_output_kwh),
  };
}

bool OpenQuattEnergyHistory::read_record_(uint32_t slot_index, EnergyHistoryRecord* record) const {
  if (record == nullptr || !this->is_partition_ready_() || slot_index >= this->flash_slot_count_) {
    return false;
  }
  const uint32_t slot_offset = BASE_OFFSET + (slot_index * FLASH_SLOT_SIZE);
  const esp_err_t result = esp_partition_read(this->flash_partition_, slot_offset, record, sizeof(*record));
  return result == ESP_OK;
}

bool OpenQuattEnergyHistory::record_valid_(const EnergyHistoryRecord& record) const {
  return record.magic == RECORD_MAGIC && record.version == RECORD_VERSION && date_key_valid_(record.date_key) &&
         record.crc32 == record_crc_(record);
}

uint16_t OpenQuattEnergyHistory::requested_flash_hourly_retention_days_() const {
  if (this->hourly_retention_select_ != nullptr) {
    return parse_hourly_retention_days_(this->hourly_retention_select_->current_option());
  }
  return DEFAULT_FLASH_HOURLY_RETENTION_DAYS;
}

size_t OpenQuattEnergyHistory::max_hour_flash_total_bytes_() const {
  if (this->flash_partition_ == nullptr || this->flash_partition_->size <= HOUR_FLASH_BASE_OFFSET) {
    return 0U;
  }

  const size_t available_bytes = this->flash_partition_->size - HOUR_FLASH_BASE_OFFSET;
  const size_t available_sectors = available_bytes / FLASH_SECTOR_SIZE;
  const size_t slots_per_sector = FLASH_SECTOR_SIZE / HOUR_FLASH_SLOT_SIZE;
  const size_t max_requested_sectors =
      (static_cast<size_t>(MAX_FLASH_HOURLY_RETENTION_DAYS) + slots_per_sector - 1U) / slots_per_sector;
  return std::min(max_requested_sectors, available_sectors) * FLASH_SECTOR_SIZE;
}

void OpenQuattEnergyHistory::configure_hour_flash_window_() {
  this->hour_flash_requested_retention_days_ = this->requested_flash_hourly_retention_days_();
  this->hour_partition_available_ = false;
  this->hour_flash_sector_count_ = 0;
  this->hour_flash_slot_count_ = 0;
  this->hour_flash_total_bytes_ = 0;

  if (this->flash_partition_ == nullptr || this->flash_partition_->size <= HOUR_FLASH_BASE_OFFSET) {
    return;
  }

  const size_t available_bytes = this->flash_partition_->size - HOUR_FLASH_BASE_OFFSET;
  const size_t requested_slots = std::min(static_cast<size_t>(this->hour_flash_requested_retention_days_),
                                          static_cast<size_t>(MAX_FLASH_HOURLY_RETENTION_DAYS));
  const size_t requested_sectors =
      (requested_slots + (FLASH_SECTOR_SIZE / HOUR_FLASH_SLOT_SIZE) - 1U) / (FLASH_SECTOR_SIZE / HOUR_FLASH_SLOT_SIZE);
  const size_t available_sectors = available_bytes / FLASH_SECTOR_SIZE;
  const size_t sector_count = std::min(requested_sectors, available_sectors);
  if (sector_count == 0U) {
    return;
  }

  this->hour_flash_sector_count_ = sector_count;
  this->hour_flash_slot_count_ =
      std::min(requested_slots, this->hour_flash_sector_count_ * (FLASH_SECTOR_SIZE / HOUR_FLASH_SLOT_SIZE));
  this->hour_flash_total_bytes_ = this->hour_flash_sector_count_ * FLASH_SECTOR_SIZE;
  this->hour_partition_available_ = this->hour_flash_slot_count_ > 0U;
}

bool OpenQuattEnergyHistory::is_hour_partition_ready_() const {
  return this->runtime_storage_ready() && this->hour_partition_available_ && this->flash_partition_ != nullptr &&
         this->hour_flash_slot_count_ > 0U;
}

bool OpenQuattEnergyHistory::read_hour_day_record_(uint32_t slot_index, EnergyHistoryHourDayRecord* record) const {
  if (record == nullptr || !this->is_hour_partition_ready_() || slot_index >= this->hour_flash_slot_count_) {
    return false;
  }
  const uint32_t slot_offset = HOUR_FLASH_BASE_OFFSET + (slot_index * HOUR_FLASH_SLOT_SIZE);
  const esp_err_t result = esp_partition_read(this->flash_partition_, slot_offset, record, sizeof(*record));
  return result == ESP_OK;
}

bool OpenQuattEnergyHistory::hour_day_record_valid_(const EnergyHistoryHourDayRecord& record) const {
  return record.magic == HOUR_DAY_RECORD_MAGIC && record.version == HOUR_DAY_RECORD_VERSION &&
         date_key_valid_(record.date_key) && record.hour_mask != 0U && record.crc32 == hour_day_record_crc_(record);
}

bool OpenQuattEnergyHistory::scan_archive_() {
  if (!this->runtime_storage_ready()) {
    return false;
  }
  std::memset(this->state_()->stored_day_bitmap, 0, sizeof(this->state_()->stored_day_bitmap));
  if (!this->is_partition_ready_()) {
    this->record_count_ = 0;
    this->stored_day_count_ = 0;
    this->next_sequence_ = 0;
    this->oldest_date_key_ = 0;
    this->newest_date_key_ = 0;
    this->last_write_timestamp_s_ = 0;
    return false;
  }

  bool found = false;
  uint32_t min_sequence = 0;
  uint32_t max_sequence = 0;
  uint32_t count = 0;
  uint32_t stored_day_count = 0;
  uint32_t oldest_date = 0;
  uint32_t newest_date = 0;
  uint32_t newest_write_ts = 0;

  for (uint32_t slot_index = 0; slot_index < this->flash_slot_count_; ++slot_index) {
    EnergyHistoryRecord record{};
    if (!this->read_record_(slot_index, &record) || !this->record_valid_(record)) {
      continue;
    }
    if (!date_bitmap_get_(this->state_()->stored_day_bitmap, record.date_key)) {
      date_bitmap_set_(this->state_()->stored_day_bitmap, record.date_key);
      ++stored_day_count;
    }
    if (!found || record.sequence < min_sequence) {
      min_sequence = record.sequence;
    }
    if (!found || record.sequence > max_sequence) {
      max_sequence = record.sequence;
      newest_write_ts = record.write_timestamp_s;
    }
    if (oldest_date == 0 || record.date_key < oldest_date) {
      oldest_date = record.date_key;
    }
    if (newest_date == 0 || record.date_key > newest_date) {
      newest_date = record.date_key;
    }
    found = true;
    ++count;
  }

  this->record_count_ = count;
  this->stored_day_count_ = stored_day_count;
  this->next_sequence_ = found ? max_sequence + 1U : 0U;
  this->oldest_date_key_ = oldest_date;
  this->newest_date_key_ = newest_date;
  this->last_write_timestamp_s_ = newest_write_ts;
  return true;
}

bool OpenQuattEnergyHistory::scan_hour_archive_() {
  if (!this->runtime_storage_ready()) {
    return false;
  }
  std::memset(this->state_()->stored_hour_day_bitmap, 0, sizeof(this->state_()->stored_hour_day_bitmap));
  if (!this->is_hour_partition_ready_()) {
    this->hour_flash_record_count_ = 0;
    this->hour_flash_stored_day_count_ = 0;
    this->next_hour_flash_sequence_ = 0;
    this->hour_flash_oldest_date_key_ = 0;
    this->hour_flash_newest_date_key_ = 0;
    this->hour_flash_last_write_timestamp_s_ = 0;
    return false;
  }

  bool found = false;
  uint32_t max_sequence = 0;
  uint32_t count = 0;
  uint32_t stored_day_count = 0;
  uint32_t oldest_date = 0;
  uint32_t newest_date = 0;
  uint32_t newest_write_ts = 0;

  for (uint32_t slot_index = 0; slot_index < this->hour_flash_slot_count_; ++slot_index) {
    auto& record = this->hour_flash_record_buffer_;
    if (!this->read_hour_day_record_(slot_index, &record) || !this->hour_day_record_valid_(record)) {
      continue;
    }
    if (!date_bitmap_get_(this->state_()->stored_hour_day_bitmap, record.date_key)) {
      date_bitmap_set_(this->state_()->stored_hour_day_bitmap, record.date_key);
      ++stored_day_count;
    }
    if (!found || record.sequence > max_sequence) {
      max_sequence = record.sequence;
      newest_write_ts = record.write_timestamp_s;
    }
    if (oldest_date == 0 || record.date_key < oldest_date) {
      oldest_date = record.date_key;
    }
    if (newest_date == 0 || record.date_key > newest_date) {
      newest_date = record.date_key;
    }
    found = true;
    ++count;
  }

  this->hour_flash_record_count_ = count;
  this->hour_flash_stored_day_count_ = stored_day_count;
  this->next_hour_flash_sequence_ = found ? max_sequence + 1U : 0U;
  this->hour_flash_oldest_date_key_ = oldest_date;
  this->hour_flash_newest_date_key_ = newest_date;
  this->hour_flash_last_write_timestamp_s_ = newest_write_ts;
  return true;
}

bool OpenQuattEnergyHistory::write_record_(uint32_t date_key, const EnergyHistoryValues& values, bool partial,
                                           bool rescan, bool require_enabled) {
  if ((require_enabled && !this->enabled_()) || !this->is_partition_ready_() || this->flash_slot_count_ == 0U ||
      !this->time_is_valid_() || !date_key_valid_(date_key) || !record_has_values_(values)) {
    return false;
  }

  const uint32_t sequence = this->next_sequence_;
  const uint32_t slot_index = sequence % static_cast<uint32_t>(this->flash_slot_count_);
  const uint32_t slot_offset = BASE_OFFSET + (slot_index * FLASH_SLOT_SIZE);
  bool sector_erased = false;
  if ((slot_offset - BASE_OFFSET) % FLASH_SECTOR_SIZE == 0) {
    const esp_err_t erase_result = esp_partition_erase_range(this->flash_partition_, slot_offset, FLASH_SECTOR_SIZE);
    if (erase_result != ESP_OK) {
      ESP_LOGW(TAG, "Could not erase energy history sector %u: %s",
               static_cast<unsigned>((slot_offset - BASE_OFFSET) / FLASH_SECTOR_SIZE), esp_err_to_name(erase_result));
      return false;
    }
    sector_erased = true;
  }

  EnergyHistoryRecord record{};
  record.magic = RECORD_MAGIC;
  record.version = RECORD_VERSION;
  record.flags = partial ? FLAG_PARTIAL : 0U;
  record.sequence = sequence;
  record.date_key = date_key;
  record.write_timestamp_s = static_cast<uint32_t>(this->current_time_ms_() / 1000ULL);
  record.values = values;
  record.crc32 = record_crc_(record);

  uint8_t slot_buffer[FLASH_SLOT_SIZE];
  std::memset(slot_buffer, 0xFF, sizeof(slot_buffer));
  std::memcpy(slot_buffer, &record, sizeof(record));
  const esp_err_t write_result =
      esp_partition_write(this->flash_partition_, slot_offset, slot_buffer, sizeof(slot_buffer));
  if (write_result != ESP_OK) {
    ESP_LOGW(TAG, "Could not write energy history slot %u: %s", static_cast<unsigned>(slot_index),
             esp_err_to_name(write_result));
    if (sector_erased && !rescan) {
      this->scan_archive_();
    }
    return false;
  }

  if (rescan || sector_erased) {
    this->scan_archive_();
  } else {
    ++this->next_sequence_;
    date_bitmap_set_(this->state_()->stored_day_bitmap, date_key);
  }
  return true;
}

bool OpenQuattEnergyHistory::snapshot_hour_day_values_(uint32_t date_key, EnergyHistoryValues hours[24],
                                                       uint32_t* hour_mask) const {
  if (!this->runtime_storage_ready() || hours == nullptr || hour_mask == nullptr || date_key == 0U) {
    return false;
  }

  *hour_mask = 0U;
  for (uint8_t hour = 0; hour < 24U; ++hour) {
    hours[hour] =
        EnergyHistoryValues{UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH};
  }

  for (const auto& record : this->state_()->hour_records) {
    if (!record.valid || record.date_key != date_key || record.hour > 23U || !record_has_values_(record.values)) {
      continue;
    }
    hours[record.hour] = record.values;
    *hour_mask |= (1UL << record.hour);
  }

  return *hour_mask != 0U;
}

bool OpenQuattEnergyHistory::write_hour_day_record_(uint32_t date_key, bool partial) {
  if (!this->enabled_() || !this->is_hour_partition_ready_() || !this->time_is_valid_() || date_key == 0U) {
    return false;
  }

  auto* hours = this->state_()->hour_snapshot_values;
  uint32_t hour_mask = 0;
  if (!this->snapshot_hour_day_values_(date_key, hours, &hour_mask)) {
    return false;
  }

  const uint32_t sequence = this->next_hour_flash_sequence_;
  const uint32_t slot_index = sequence % static_cast<uint32_t>(this->hour_flash_slot_count_);
  const uint32_t slot_offset = HOUR_FLASH_BASE_OFFSET + (slot_index * HOUR_FLASH_SLOT_SIZE);
  if ((slot_offset - HOUR_FLASH_BASE_OFFSET) % FLASH_SECTOR_SIZE == 0) {
    const esp_err_t erase_result = esp_partition_erase_range(this->flash_partition_, slot_offset, FLASH_SECTOR_SIZE);
    if (erase_result != ESP_OK) {
      ESP_LOGW(TAG, "Could not erase energy hour history sector %u: %s",
               static_cast<unsigned>((slot_offset - HOUR_FLASH_BASE_OFFSET) / FLASH_SECTOR_SIZE),
               esp_err_to_name(erase_result));
      return false;
    }
  }

  auto& record = this->hour_flash_record_buffer_;
  record = EnergyHistoryHourDayRecord{};
  record.magic = HOUR_DAY_RECORD_MAGIC;
  record.version = HOUR_DAY_RECORD_VERSION;
  record.flags = partial ? FLAG_PARTIAL : 0U;
  record.sequence = sequence;
  record.date_key = date_key;
  record.write_timestamp_s = static_cast<uint32_t>(this->current_time_ms_() / 1000ULL);
  record.hour_mask = hour_mask;
  std::memcpy(record.hours, hours, sizeof(record.hours));
  record.crc32 = hour_day_record_crc_(record);

  auto* slot_buffer = this->hour_flash_slot_buffer_;
  std::memset(slot_buffer, 0xFF, HOUR_FLASH_SLOT_SIZE);
  std::memcpy(slot_buffer, &record, sizeof(record));
  const esp_err_t write_result =
      esp_partition_write(this->flash_partition_, slot_offset, slot_buffer, HOUR_FLASH_SLOT_SIZE);
  if (write_result != ESP_OK) {
    ESP_LOGW(TAG, "Could not write energy hour history slot %u: %s", static_cast<unsigned>(slot_index),
             esp_err_to_name(write_result));
    return false;
  }

  this->scan_hour_archive_();
  return true;
}

bool OpenQuattEnergyHistory::write_hour_day_import_record_(uint32_t date_key, uint32_t hour_mask,
                                                           const EnergyHistoryValues hours[24], bool rescan) {
  if (!this->is_hour_partition_ready_() || !this->time_is_valid_() || !date_key_valid_(date_key) || hour_mask == 0U ||
      hours == nullptr) {
    return false;
  }

  bool has_values = false;
  for (uint8_t hour = 0; hour < 24U; ++hour) {
    if ((hour_mask & (1UL << hour)) != 0U && record_has_values_(hours[hour])) {
      has_values = true;
      break;
    }
  }
  if (!has_values) {
    return false;
  }

  const uint32_t sequence = this->next_hour_flash_sequence_;
  const uint32_t slot_index = sequence % static_cast<uint32_t>(this->hour_flash_slot_count_);
  const uint32_t slot_offset = HOUR_FLASH_BASE_OFFSET + (slot_index * HOUR_FLASH_SLOT_SIZE);
  bool sector_erased = false;
  if ((slot_offset - HOUR_FLASH_BASE_OFFSET) % FLASH_SECTOR_SIZE == 0) {
    const esp_err_t erase_result = esp_partition_erase_range(this->flash_partition_, slot_offset, FLASH_SECTOR_SIZE);
    if (erase_result != ESP_OK) {
      ESP_LOGW(TAG, "Could not erase imported energy hour history sector %u: %s",
               static_cast<unsigned>((slot_offset - HOUR_FLASH_BASE_OFFSET) / FLASH_SECTOR_SIZE),
               esp_err_to_name(erase_result));
      return false;
    }
    sector_erased = true;
  }

  auto& record = this->hour_flash_record_buffer_;
  record = EnergyHistoryHourDayRecord{};
  record.magic = HOUR_DAY_RECORD_MAGIC;
  record.version = HOUR_DAY_RECORD_VERSION;
  record.flags = 0U;
  record.sequence = sequence;
  record.date_key = date_key;
  record.write_timestamp_s = static_cast<uint32_t>(this->current_time_ms_() / 1000ULL);
  record.hour_mask = hour_mask;
  std::memcpy(record.hours, hours, sizeof(record.hours));
  record.crc32 = hour_day_record_crc_(record);

  auto* slot_buffer = this->hour_flash_slot_buffer_;
  std::memset(slot_buffer, 0xFF, HOUR_FLASH_SLOT_SIZE);
  std::memcpy(slot_buffer, &record, sizeof(record));
  const esp_err_t write_result =
      esp_partition_write(this->flash_partition_, slot_offset, slot_buffer, HOUR_FLASH_SLOT_SIZE);
  if (write_result != ESP_OK) {
    ESP_LOGW(TAG, "Could not write imported energy hour history slot %u: %s", static_cast<unsigned>(slot_index),
             esp_err_to_name(write_result));
    if (sector_erased && !rescan) {
      this->scan_hour_archive_();
    }
    return false;
  }

  if (rescan || sector_erased) {
    this->scan_hour_archive_();
  } else {
    ++this->next_hour_flash_sequence_;
    date_bitmap_set_(this->state_()->stored_hour_day_bitmap, date_key);
  }
  return true;
}

OpenQuattEnergyHistory::EnergyHistoryValues OpenQuattEnergyHistory::delta_values_(
    const EnergyHistoryValues& current, const EnergyHistoryValues& previous) const {
  return EnergyHistoryValues{
      delta_wh_(current.electrical_input_wh, previous.electrical_input_wh),
      delta_wh_(current.heating_input_wh, previous.heating_input_wh),
      delta_wh_(current.cooling_input_wh, previous.cooling_input_wh),
      delta_wh_(current.heatpump_heat_output_wh, previous.heatpump_heat_output_wh),
      delta_wh_(current.heatpump_cooling_output_wh, previous.heatpump_cooling_output_wh),
      delta_wh_(current.boiler_heat_output_wh, previous.boiler_heat_output_wh),
      delta_wh_(current.system_heat_output_wh, previous.system_heat_output_wh),
  };
}

void OpenQuattEnergyHistory::restore_current_day_hours_(uint32_t date_key, uint8_t current_hour,
                                                        const EnergyHistoryValues& current_values) {
  if (!this->runtime_storage_ready() || date_key == 0U || current_hour > 23U) {
    return;
  }

  EnergyHistoryValues restored[24];
  uint32_t restored_sequences[24]{};
  bool restored_hours[24]{};
  for (auto& values : restored) {
    values = EnergyHistoryValues{UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH};
  }

  for (uint32_t slot_index = 0; slot_index < this->hour_flash_slot_count_; ++slot_index) {
    auto& record = this->hour_flash_record_buffer_;
    if (!this->read_hour_day_record_(slot_index, &record) || !this->hour_day_record_valid_(record) ||
        record.date_key != date_key) {
      continue;
    }
    for (uint8_t hour = 0; hour < 24U; ++hour) {
      if ((record.hour_mask & (1UL << hour)) == 0U || !record_has_values_(record.hours[hour]) ||
          (restored_hours[hour] && record.sequence < restored_sequences[hour])) {
        continue;
      }
      restored[hour] = record.hours[hour];
      restored_sequences[hour] = record.sequence;
      restored_hours[hour] = true;
    }
  }

  EnergyHistoryValues restored_totals{0U, 0U, 0U, 0U, 0U, 0U, 0U};
  for (uint8_t hour = 0; hour < 24U; ++hour) {
    if (!restored_hours[hour]) {
      continue;
    }
    this->capture_hour_delta_(date_key, hour, restored[hour]);
    restored_totals.electrical_input_wh =
        add_wh_(restored_totals.electrical_input_wh, restored[hour].electrical_input_wh);
    restored_totals.heating_input_wh = add_wh_(restored_totals.heating_input_wh, restored[hour].heating_input_wh);
    restored_totals.cooling_input_wh = add_wh_(restored_totals.cooling_input_wh, restored[hour].cooling_input_wh);
    restored_totals.heatpump_heat_output_wh =
        add_wh_(restored_totals.heatpump_heat_output_wh, restored[hour].heatpump_heat_output_wh);
    restored_totals.heatpump_cooling_output_wh =
        add_wh_(restored_totals.heatpump_cooling_output_wh, restored[hour].heatpump_cooling_output_wh);
    restored_totals.boiler_heat_output_wh =
        add_wh_(restored_totals.boiler_heat_output_wh, restored[hour].boiler_heat_output_wh);
    restored_totals.system_heat_output_wh =
        add_wh_(restored_totals.system_heat_output_wh, restored[hour].system_heat_output_wh);
  }

  // Older firmware stored per-boot hour fragments. Put any missing restored daily energy in the
  // reboot hour so the live cumulative snapshot reconciles exactly with the restored day counters.
  this->capture_hour_delta_(date_key, current_hour, this->delta_values_(current_values, restored_totals));
}

void OpenQuattEnergyHistory::capture_hour_delta_(uint32_t date_key, uint8_t hour, const EnergyHistoryValues& values) {
  if (!this->runtime_storage_ready() || date_key == 0 || hour > 23 || !record_has_values_(values)) {
    return;
  }

  EnergyHistoryHourRecord* record = nullptr;
  for (auto& candidate : this->state_()->hour_records) {
    if (candidate.valid && candidate.date_key == date_key && candidate.hour == hour) {
      record = &candidate;
      break;
    }
  }

  if (record == nullptr) {
    record = &this->state_()->hour_records[this->next_hour_sequence_ % HOURLY_SLOT_COUNT];
    record->sequence = this->next_hour_sequence_++;
    record->date_key = date_key;
    record->hour = hour;
    record->valid = true;
    record->values =
        EnergyHistoryValues{UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH};
  }

  record->values.electrical_input_wh = add_wh_(record->values.electrical_input_wh, values.electrical_input_wh);
  record->values.heating_input_wh = add_wh_(record->values.heating_input_wh, values.heating_input_wh);
  record->values.cooling_input_wh = add_wh_(record->values.cooling_input_wh, values.cooling_input_wh);
  record->values.heatpump_heat_output_wh =
      add_wh_(record->values.heatpump_heat_output_wh, values.heatpump_heat_output_wh);
  record->values.heatpump_cooling_output_wh =
      add_wh_(record->values.heatpump_cooling_output_wh, values.heatpump_cooling_output_wh);
  record->values.boiler_heat_output_wh = add_wh_(record->values.boiler_heat_output_wh, values.boiler_heat_output_wh);
  record->values.system_heat_output_wh = add_wh_(record->values.system_heat_output_wh, values.system_heat_output_wh);
}

uint32_t OpenQuattEnergyHistory::get_hour_record_count_() const {
  if (!this->runtime_storage_ready()) {
    return 0;
  }
  uint32_t count = 0;
  for (const auto& record : this->state_()->hour_records) {
    if (record.valid && record_has_values_(record.values)) {
      ++count;
    }
  }
  return count;
}

void OpenQuattEnergyHistory::clear_hour_records_() {
  if (!this->runtime_storage_ready()) {
    return;
  }
  std::memset(this->state_()->hour_records, 0, sizeof(this->state_()->hour_records));
  this->next_hour_sequence_ = 0;
  this->has_last_hour_sample_ = false;
  this->last_hour_sample_date_key_ = 0;
  this->last_hour_sample_hour_ = 0;
  this->last_hour_sample_values_ =
      EnergyHistoryValues{UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH};
}

void OpenQuattEnergyHistory::capture_day_totals(float electrical_input_kwh, float heating_input_kwh,
                                                float cooling_input_kwh, float heatpump_heat_output_kwh,
                                                float heatpump_cooling_output_kwh, float boiler_heat_output_kwh,
                                                float system_heat_output_kwh) {
  if (!this->runtime_storage_ready() || !this->enabled_() || !this->time_is_valid_()) {
    return;
  }

  const uint32_t date_key = this->get_current_date_key_();
  if (date_key == 0) {
    return;
  }

  const EnergyHistoryValues values =
      this->pack_values_(electrical_input_kwh, heating_input_kwh, cooling_input_kwh, heatpump_heat_output_kwh,
                         heatpump_cooling_output_kwh, boiler_heat_output_kwh, system_heat_output_kwh);
  if (!record_has_values_(values)) {
    return;
  }

  const uint8_t hour = this->get_current_hour_();
  const bool starts_new_day_or_boot = !this->has_last_hour_sample_ || this->last_hour_sample_date_key_ != date_key;
  if (starts_new_day_or_boot) {
    this->restore_current_day_hours_(date_key, hour, values);
  } else {
    this->capture_hour_delta_(date_key, hour, this->delta_values_(values, this->last_hour_sample_values_));
  }

  if (this->has_current_day_ && this->active_date_key_ != date_key) {
    this->write_hour_day_record_(this->active_date_key_, false);
    this->write_record_(this->active_date_key_, this->current_values_, false);
  }

  this->active_date_key_ = date_key;
  this->current_values_ = values;
  this->has_current_day_ = true;
  this->last_hour_sample_date_key_ = date_key;
  this->last_hour_sample_hour_ = hour;
  this->last_hour_sample_values_ = values;
  this->has_last_hour_sample_ = true;
}

void OpenQuattEnergyHistory::clear_history() {
  if (!this->is_partition_ready_()) {
    return;
  }
  const esp_err_t erase_result =
      esp_partition_erase_range(this->flash_partition_, BASE_OFFSET, this->flash_total_bytes_);
  if (erase_result != ESP_OK) {
    ESP_LOGW(TAG, "Could not erase energy history: %s", esp_err_to_name(erase_result));
  }
  const size_t max_hour_flash_total_bytes = this->max_hour_flash_total_bytes_();
  if (max_hour_flash_total_bytes > 0U) {
    const esp_err_t erase_hour_result =
        esp_partition_erase_range(this->flash_partition_, HOUR_FLASH_BASE_OFFSET, max_hour_flash_total_bytes);
    if (erase_hour_result != ESP_OK) {
      ESP_LOGW(TAG, "Could not erase energy hour history: %s", esp_err_to_name(erase_hour_result));
    }
  }
  this->has_current_day_ = false;
  this->active_date_key_ = 0;
  this->current_values_ =
      EnergyHistoryValues{UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH};
  this->clear_hour_records_();
  this->scan_archive_();
  this->scan_hour_archive_();
}

std::string OpenQuattEnergyHistory::import_history_records(const std::string& records) {
  if (!this->is_partition_ready_()) {
    return R"({"ok":false,"error":"partition_unavailable"})";
  }
  if (!this->time_is_valid_()) {
    return R"({"ok":false,"error":"time_unavailable"})";
  }
  if (records.empty()) {
    return R"({"ok":false,"error":"empty_records"})";
  }
  if (records.size() > 24576U) {
    return R"({"ok":false,"error":"payload_too_large"})";
  }

  uint32_t written = 0;
  uint32_t hour_written = 0;
  uint32_t duplicates = 0;
  uint32_t skipped = 0;
  uint32_t invalid = 0;
  uint32_t unsupported = 0;

  size_t line_start = 0;
  while (line_start <= records.size()) {
    size_t line_end = records.find('\n', line_start);
    if (line_end == std::string::npos) {
      line_end = records.size();
    }

    std::string line = trim_copy(records.substr(line_start, line_end - line_start));
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
      line = trim_copy(line);
    }
    line_start = line_end + 1U;

    if (line.empty()) {
      continue;
    }
    if (line[0] == '@') {
      if (line == "@schema|openquatt.energy_history_import.v1") {
        continue;
      }
      ++unsupported;
      continue;
    }

    const auto fields = split_import_fields_(line);
    if (fields.empty()) {
      continue;
    }

    if (fields[0] == "day") {
      const bool compact_quatt_day = fields.size() == 5U;
      const bool compact_day = fields.size() == 6U;
      if (!compact_quatt_day && !compact_day && fields.size() != 9U) {
        ++invalid;
        continue;
      }

      uint32_t date_key = 0;
      uint32_t parsed_values[7]{};
      EnergyHistoryValues values{};
      bool parsed = parse_import_uint32_(fields[1], &date_key) && date_key_valid_(date_key);
      if (parsed && compact_quatt_day) {
        parsed = parse_import_uint32_(fields[2], &parsed_values[0]) &&
                 parse_import_uint32_(fields[3], &parsed_values[3]) &&
                 parse_import_uint32_(fields[4], &parsed_values[5]);
        parsed_values[1] = parsed_values[0];
        parsed_values[2] = 0U;
        parsed_values[4] = 0U;
        const uint64_t system_heat = static_cast<uint64_t>(parsed_values[3]) + parsed_values[5];
        parsed_values[6] = system_heat > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(system_heat);
      } else if (parsed && compact_day) {
        parsed =
            parse_import_uint32_(fields[2], &parsed_values[0]) && parse_import_uint32_(fields[3], &parsed_values[3]) &&
            parse_import_uint32_(fields[4], &parsed_values[5]) && parse_import_uint32_(fields[5], &parsed_values[6]);
        parsed_values[1] = parsed_values[0];
        parsed_values[2] = 0U;
        parsed_values[4] = 0U;
      } else if (parsed) {
        parsed =
            parse_import_uint32_(fields[2], &parsed_values[0]) && parse_import_uint32_(fields[3], &parsed_values[1]) &&
            parse_import_uint32_(fields[4], &parsed_values[2]) && parse_import_uint32_(fields[5], &parsed_values[3]) &&
            parse_import_uint32_(fields[6], &parsed_values[4]) && parse_import_uint32_(fields[7], &parsed_values[5]) &&
            parse_import_uint32_(fields[8], &parsed_values[6]);
      }
      if (!parsed) {
        ++invalid;
        continue;
      }
      values = EnergyHistoryValues{parsed_values[0], parsed_values[1], parsed_values[2], parsed_values[3],
                                   parsed_values[4], parsed_values[5], parsed_values[6]};
      if (!record_has_values_(values)) {
        ++invalid;
        continue;
      }

      if (date_bitmap_get_(this->state_()->stored_day_bitmap, date_key)) {
        ++duplicates;
        continue;
      }

      if (this->write_record_(date_key, values, false, false, false)) {
        ++written;
      } else {
        ++skipped;
      }
      continue;
    }

    if (fields[0] == "hourday") {
      const bool compact_quatt_hour_day = fields.size() == 75U;
      const bool compact_hour_day = fields.size() == 99U;
      if (!compact_quatt_hour_day && !compact_hour_day && fields.size() != 171U) {
        ++invalid;
        continue;
      }
      if (!this->is_hour_partition_ready_()) {
        ++skipped;
        continue;
      }

      uint32_t date_key = 0;
      uint32_t hour_mask = 0;
      if (!parse_import_uint32_(fields[1], &date_key) || !date_key_valid_(date_key) ||
          !parse_import_uint32_(fields[2], &hour_mask) || (hour_mask & 0xFF000000UL) != 0U || hour_mask == 0U) {
        ++invalid;
        continue;
      }

      for (uint8_t hour = 0; hour < 24U; ++hour) {
        this->state_()->hour_import_values[hour] =
            EnergyHistoryValues{UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH};
      }

      bool valid = true;
      for (uint8_t hour = 0; hour < 24U && valid; ++hour) {
        if ((hour_mask & (1UL << hour)) == 0U) {
          continue;
        }
        const size_t fields_per_hour = compact_quatt_hour_day ? 3U : (compact_hour_day ? 4U : 7U);
        const size_t offset = 3U + (static_cast<size_t>(hour) * fields_per_hour);
        uint32_t parsed_values[7]{};
        auto& values = this->state_()->hour_import_values[hour];
        if (compact_quatt_hour_day) {
          valid = parse_import_uint32_(fields[offset], &parsed_values[0]) &&
                  parse_import_uint32_(fields[offset + 1U], &parsed_values[3]) &&
                  parse_import_uint32_(fields[offset + 2U], &parsed_values[5]);
          parsed_values[1] = parsed_values[0];
          parsed_values[2] = 0U;
          parsed_values[4] = 0U;
          const uint64_t system_heat = static_cast<uint64_t>(parsed_values[3]) + parsed_values[5];
          parsed_values[6] = system_heat > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(system_heat);
        } else if (compact_hour_day) {
          valid = parse_import_uint32_(fields[offset], &parsed_values[0]) &&
                  parse_import_uint32_(fields[offset + 1U], &parsed_values[3]) &&
                  parse_import_uint32_(fields[offset + 2U], &parsed_values[5]) &&
                  parse_import_uint32_(fields[offset + 3U], &parsed_values[6]);
          parsed_values[1] = parsed_values[0];
          parsed_values[2] = 0U;
          parsed_values[4] = 0U;
        } else {
          valid = parse_import_uint32_(fields[offset], &parsed_values[0]) &&
                  parse_import_uint32_(fields[offset + 1U], &parsed_values[1]) &&
                  parse_import_uint32_(fields[offset + 2U], &parsed_values[2]) &&
                  parse_import_uint32_(fields[offset + 3U], &parsed_values[3]) &&
                  parse_import_uint32_(fields[offset + 4U], &parsed_values[4]) &&
                  parse_import_uint32_(fields[offset + 5U], &parsed_values[5]) &&
                  parse_import_uint32_(fields[offset + 6U], &parsed_values[6]);
        }
        if (valid) {
          values = EnergyHistoryValues{parsed_values[0], parsed_values[1], parsed_values[2], parsed_values[3],
                                       parsed_values[4], parsed_values[5], parsed_values[6]};
          valid = record_has_values_(values);
        }
      }
      if (!valid) {
        ++invalid;
        continue;
      }

      if (date_bitmap_get_(this->state_()->stored_hour_day_bitmap, date_key)) {
        ++duplicates;
        continue;
      }

      if (this->write_hour_day_import_record_(date_key, hour_mask, this->state_()->hour_import_values, false)) {
        ++hour_written;
      } else {
        ++skipped;
      }
      continue;
    }

    ++unsupported;
  }

  if (written > 0U) {
    this->scan_archive_();
  }
  if (hour_written > 0U) {
    this->scan_hour_archive_();
  }

  char response[192];
  std::snprintf(response, sizeof(response),
                "{\"ok\":true,\"written\":%u,\"hour_written\":%u,\"duplicates\":%u,\"skipped\":%u,\"invalid\":%u,"
                "\"unsupported\":%u}",
                static_cast<unsigned>(written), static_cast<unsigned>(hour_written), static_cast<unsigned>(duplicates),
                static_cast<unsigned>(skipped), static_cast<unsigned>(invalid), static_cast<unsigned>(unsupported));
  return response;
}

bool OpenQuattEnergyHistory::write_export_record_(ChunkedTextWriter* writer, uint32_t date_key, int16_t hour,
                                                  const EnergyHistoryValues& values, bool* first) const {
  if (writer == nullptr || first == nullptr || !date_key_valid_(date_key) || !record_has_values_(values)) {
    return false;
  }
  if (!*first && !writer->printf(",\n")) {
    return false;
  }
  *first = false;

  const std::string date = format_date_key_iso_(date_key);
  if (!writer->printf("    {\"date\":\"%s\"", date.c_str())) {
    return false;
  }
  if (hour >= 0 && !writer->printf(",\"hour\":%u", static_cast<unsigned>(hour))) {
    return false;
  }
  return write_export_wh_field_(writer, "energy_hp_electric", values.electrical_input_wh) &&
         write_export_wh_field_(writer, "energy_hp_heat", values.heatpump_heat_output_wh) &&
         write_export_wh_field_(writer, "energy_hp_cooling", values.heatpump_cooling_output_wh) &&
         write_export_wh_field_(writer, "energy_boiler_heat", values.boiler_heat_output_wh) &&
         write_export_wh_field_(writer, "heating_input_wh", values.heating_input_wh) &&
         write_export_wh_field_(writer, "cooling_input_wh", values.cooling_input_wh) && writer->printf("}");
}

void OpenQuattEnergyHistory::clear_export_hour_marks_() {
  if (!this->runtime_storage_ready()) {
    return;
  }
  std::memset(this->state_()->export_hour_date_keys, 0, sizeof(this->state_()->export_hour_date_keys));
  std::memset(this->state_()->export_hour_masks, 0, sizeof(this->state_()->export_hour_masks));
}

bool OpenQuattEnergyHistory::export_hour_marked_(uint32_t date_key, uint8_t hour) const {
  if (!this->runtime_storage_ready() || !date_key_valid_(date_key) || hour > 23U) {
    return false;
  }

  const uint32_t bit = 1UL << hour;
  for (size_t index = 0; index < EXPORT_HOUR_DATE_COUNT; ++index) {
    const uint32_t stored_date_key = this->state_()->export_hour_date_keys[index];
    if (stored_date_key == 0U) {
      return false;
    }
    if (stored_date_key == date_key) {
      return (this->state_()->export_hour_masks[index] & bit) != 0U;
    }
  }
  return false;
}

bool OpenQuattEnergyHistory::mark_export_hour_(uint32_t date_key, uint8_t hour) {
  if (!this->runtime_storage_ready() || !date_key_valid_(date_key) || hour > 23U) {
    return false;
  }

  const uint32_t bit = 1UL << hour;
  for (size_t index = 0; index < EXPORT_HOUR_DATE_COUNT; ++index) {
    if (this->state_()->export_hour_date_keys[index] == date_key) {
      this->state_()->export_hour_masks[index] |= bit;
      return true;
    }
    if (this->state_()->export_hour_date_keys[index] == 0U) {
      this->state_()->export_hour_date_keys[index] = date_key;
      this->state_()->export_hour_masks[index] = bit;
      return true;
    }
  }
  return false;
}

void OpenQuattEnergyHistory::write_history_export(httpd_req_t* req) {
  if (req == nullptr || !this->runtime_storage_ready()) {
    return;
  }

  uint32_t from_date_key = read_query_date_key(req, "from");
  uint32_t to_date_key = read_query_date_key(req, "to");
  if (from_date_key != 0U && to_date_key != 0U && from_date_key > to_date_key) {
    std::swap(from_date_key, to_date_key);
  }

  const EnergyHistoryExportMode mode = read_export_mode(req);
  const bool include_days = export_mode_includes_days(mode);
  const bool include_hours = export_mode_includes_hours(mode);
  ChunkedTextWriter writer(req);
  if (!writer.begin_response("application/json; charset=utf-8", true)) {
    return;
  }

  if (!writer.printf("{\n") || !writer.printf("  \"schema\":\"openquatt.quatt_insights_daily.v1\",\n") ||
      !writer.printf("  \"source\":\"openquatt.energy_history_export.v1\",\n")) {
    ESP_LOGW(TAG, "Failed to start energy history export response");
    return;
  }

  if (this->time_is_valid_()) {
    const std::string exported_at = this->clock_->now().strftime("%Y-%m-%dT%H:%M:%S");
    if (!exported_at.empty() && !writer.printf("  \"exported_at\":\"%s\",\n", exported_at.c_str())) {
      ESP_LOGW(TAG, "Failed to write energy history export metadata");
      return;
    }
  }

  if (!writer.printf("  \"query\":{\"source\":\"openquatt\",\"export_mode\":\"%s\"},\n", export_mode_id(mode)) ||
      !writer.printf("  \"days\":[\n")) {
    ESP_LOGW(TAG, "Failed to write energy history export query");
    return;
  }

  bool first_day = true;
  std::memset(this->state_()->export_date_bitmap, 0, sizeof(this->state_()->export_date_bitmap));
  if (include_days) {
    if (this->has_current_day_ && record_has_values_(this->current_values_) &&
        date_key_in_range_(this->active_date_key_, from_date_key, to_date_key)) {
      if (!this->write_export_record_(&writer, this->active_date_key_, -1, this->current_values_, &first_day)) {
        ESP_LOGW(TAG, "Failed to write current energy export day record");
        return;
      }
      date_bitmap_set_(this->state_()->export_date_bitmap, this->active_date_key_);
    }

    if (this->flash_slot_count_ > 0U) {
      const uint32_t slot_count = static_cast<uint32_t>(this->flash_slot_count_);
      const uint32_t start_sequence = this->next_sequence_ > slot_count ? this->next_sequence_ - slot_count : 0U;
      for (uint32_t sequence = this->next_sequence_; sequence > start_sequence; --sequence) {
        const uint32_t record_sequence = sequence - 1U;
        const uint32_t slot_index = record_sequence % slot_count;
        EnergyHistoryRecord record{};
        if (!this->read_record_(slot_index, &record) || !this->record_valid_(record) ||
            record.sequence != record_sequence || !date_key_in_range_(record.date_key, from_date_key, to_date_key)) {
          continue;
        }
        if (date_bitmap_get_(this->state_()->export_date_bitmap, record.date_key)) {
          continue;
        }
        date_bitmap_set_(this->state_()->export_date_bitmap, record.date_key);
        if (!this->write_export_record_(&writer, record.date_key, -1, record.values, &first_day)) {
          ESP_LOGW(TAG, "Failed to write energy history export day record");
          return;
        }
      }
    }
  }

  if (!writer.printf("\n  ],\n") || !writer.printf("  \"hourly\":[\n")) {
    ESP_LOGW(TAG, "Failed to write energy history export separator");
    return;
  }

  bool first_hour = true;
  if (include_hours) {
    this->clear_export_hour_marks_();

    const uint32_t live_slot_count = static_cast<uint32_t>(HOURLY_SLOT_COUNT);
    const uint32_t live_start_sequence =
        this->next_hour_sequence_ > live_slot_count ? this->next_hour_sequence_ - live_slot_count : 0U;
    for (uint32_t sequence = live_start_sequence; sequence < this->next_hour_sequence_; ++sequence) {
      const auto& record = this->state_()->hour_records[sequence % live_slot_count];
      if (!record.valid || !record_has_values_(record.values) || record.sequence != sequence ||
          !date_key_in_range_(record.date_key, from_date_key, to_date_key) ||
          this->export_hour_marked_(record.date_key, record.hour)) {
        continue;
      }
      if (!this->write_export_record_(&writer, record.date_key, record.hour, record.values, &first_hour)) {
        ESP_LOGW(TAG, "Failed to write live energy hour export record");
        return;
      }
      this->mark_export_hour_(record.date_key, record.hour);
    }

    if (this->hour_flash_slot_count_ > 0U) {
      const uint32_t slot_count = static_cast<uint32_t>(this->hour_flash_slot_count_);
      const uint32_t start_sequence =
          this->next_hour_flash_sequence_ > slot_count ? this->next_hour_flash_sequence_ - slot_count : 0U;
      for (uint32_t sequence = this->next_hour_flash_sequence_; sequence > start_sequence; --sequence) {
        const uint32_t record_sequence = sequence - 1U;
        const uint32_t slot_index = record_sequence % slot_count;
        auto& record = this->hour_flash_record_buffer_;
        if (!this->read_hour_day_record_(slot_index, &record) || !this->hour_day_record_valid_(record) ||
            record.sequence != record_sequence || !date_key_in_range_(record.date_key, from_date_key, to_date_key)) {
          continue;
        }
        for (uint8_t hour = 0; hour < 24U; ++hour) {
          if ((record.hour_mask & (1UL << hour)) == 0U || !record_has_values_(record.hours[hour])) {
            continue;
          }
          if (this->export_hour_marked_(record.date_key, hour)) {
            continue;
          }
          if (!this->write_export_record_(&writer, record.date_key, hour, record.hours[hour], &first_hour)) {
            ESP_LOGW(TAG, "Failed to write persisted energy hour export record");
            return;
          }
          this->mark_export_hour_(record.date_key, hour);
        }
      }
    }
  }

  if (!writer.printf("\n  ]\n") || !writer.printf("}\n") || httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to finish energy history export response");
  }
}

void OpenQuattEnergyHistory::write_history(httpd_req_t* req) {
  if (req == nullptr || !this->runtime_storage_ready()) {
    return;
  }

  uint32_t from_date_key = read_query_date_key(req, "from");
  uint32_t to_date_key = read_query_date_key(req, "to");
  if (from_date_key != 0U && to_date_key != 0U && from_date_key > to_date_key) {
    std::swap(from_date_key, to_date_key);
  }
  const bool has_query = httpd_req_get_url_query_len(req) > 0U;
  const bool meta_only = read_query_flag(req, "meta", false);
  const bool include_hours = read_query_flag(req, "hours", !has_query);

  ChunkedTextWriter writer(req);
  if (!writer.begin_response("text/plain; charset=utf-8")) {
    return;
  }
  const uint32_t hour_count = this->get_hour_record_count_();
  if (!writer.printf("@schema|3\n") || !writer.printf("@enabled|%u\n", this->enabled_() ? 1U : 0U) ||
      !writer.printf("@csrf|%s\n", this->csrf_token_.c_str()) ||
      !writer.printf("@now|%llu\n", static_cast<unsigned long long>(this->current_time_ms_())) ||
      !writer.printf("@records|%u\n", static_cast<unsigned>(this->record_count_)) ||
      !writer.printf("@hours|%u|%u\n", static_cast<unsigned>(hour_count),
                     static_cast<unsigned>(HOURLY_RETENTION_DAYS)) ||
      !writer.printf("@range|%u|%u|%u\n", static_cast<unsigned>(from_date_key), static_cast<unsigned>(to_date_key),
                     include_hours ? 1U : 0U) ||
      !writer.printf("@bounds|%u|%u|%u|%u|%u|%u\n", static_cast<unsigned>(this->stored_day_count_),
                     static_cast<unsigned>(this->oldest_date_key_), static_cast<unsigned>(this->newest_date_key_),
                     static_cast<unsigned>(this->hour_flash_stored_day_count_),
                     static_cast<unsigned>(this->hour_flash_oldest_date_key_),
                     static_cast<unsigned>(this->hour_flash_newest_date_key_)) ||
      !writer.printf("@day_retention|%u|%u|%u|%u\n", this->is_partition_ready_() ? 1U : 0U,
                     static_cast<unsigned>(this->flash_total_bytes_ / 1024U),
                     static_cast<unsigned>(this->next_sequence_),
                     static_cast<unsigned>(this->last_write_timestamp_s_)) ||
      !writer.printf(
          "@hour_retention|%u|%u|%u|%u|%u|%u|%u\n", static_cast<unsigned>(this->hour_flash_requested_retention_days_),
          static_cast<unsigned>(this->hour_flash_slot_count_), this->is_hour_partition_ready_() ? 1U : 0U,
          static_cast<unsigned>(this->hour_flash_record_count_), static_cast<unsigned>(this->next_hour_flash_sequence_),
          static_cast<unsigned>(this->hour_flash_total_bytes_ / 1024U),
          static_cast<unsigned>(this->hour_flash_last_write_timestamp_s_))) {
    ESP_LOGW(TAG, "Failed to start energy history response");
    return;
  }

  if (!meta_only && this->flash_slot_count_ > 0U) {
    const uint32_t slot_count = static_cast<uint32_t>(this->flash_slot_count_);
    const uint32_t start_sequence = this->next_sequence_ > slot_count ? this->next_sequence_ - slot_count : 0U;
    for (uint32_t sequence = start_sequence; sequence < this->next_sequence_; ++sequence) {
      const uint32_t slot_index = sequence % slot_count;
      EnergyHistoryRecord record{};
      if (!this->read_record_(slot_index, &record) || !this->record_valid_(record) || record.sequence != sequence ||
          !date_key_in_range_(record.date_key, from_date_key, to_date_key)) {
        continue;
      }
      if (!writer.printf(
              "%u|%u|%u|%d|%d|%d|%d|%d|%d|%d\n", static_cast<unsigned>(record.sequence),
              static_cast<unsigned>(record.date_key), static_cast<unsigned>(record.flags),
              printable_wh_(record.values.electrical_input_wh), printable_wh_(record.values.heating_input_wh),
              printable_wh_(record.values.cooling_input_wh), printable_wh_(record.values.heatpump_heat_output_wh),
              printable_wh_(record.values.heatpump_cooling_output_wh),
              printable_wh_(record.values.boiler_heat_output_wh), printable_wh_(record.values.system_heat_output_wh))) {
        ESP_LOGW(TAG, "Failed to write energy history response");
        return;
      }
    }
  }

  if (this->has_current_day_ && record_has_values_(this->current_values_) &&
      date_key_in_range_(this->active_date_key_, from_date_key, to_date_key)) {
    writer.printf("@current|%u|%d|%d|%d|%d|%d|%d|%d\n", static_cast<unsigned>(this->active_date_key_),
                  printable_wh_(this->current_values_.electrical_input_wh),
                  printable_wh_(this->current_values_.heating_input_wh),
                  printable_wh_(this->current_values_.cooling_input_wh),
                  printable_wh_(this->current_values_.heatpump_heat_output_wh),
                  printable_wh_(this->current_values_.heatpump_cooling_output_wh),
                  printable_wh_(this->current_values_.boiler_heat_output_wh),
                  printable_wh_(this->current_values_.system_heat_output_wh));
  }

  if (include_hours && !meta_only) {
    for (uint32_t slot_index = 0; slot_index < this->hour_flash_slot_count_; ++slot_index) {
      auto& record = this->hour_flash_record_buffer_;
      if (!this->read_hour_day_record_(slot_index, &record) || !this->hour_day_record_valid_(record) ||
          !date_key_in_range_(record.date_key, from_date_key, to_date_key)) {
        continue;
      }
      for (uint8_t hour = 0; hour < 24U; ++hour) {
        if ((record.hour_mask & (1UL << hour)) == 0U || !record_has_values_(record.hours[hour])) {
          continue;
        }
        if (!writer.printf("@hour|%u|%u|%u|%d|%d|%d|%d|%d|%d|%d\n", static_cast<unsigned>(record.sequence),
                           static_cast<unsigned>(record.date_key), static_cast<unsigned>(hour),
                           printable_wh_(record.hours[hour].electrical_input_wh),
                           printable_wh_(record.hours[hour].heating_input_wh),
                           printable_wh_(record.hours[hour].cooling_input_wh),
                           printable_wh_(record.hours[hour].heatpump_heat_output_wh),
                           printable_wh_(record.hours[hour].heatpump_cooling_output_wh),
                           printable_wh_(record.hours[hour].boiler_heat_output_wh),
                           printable_wh_(record.hours[hour].system_heat_output_wh))) {
          ESP_LOGW(TAG, "Failed to write energy persisted hour history response");
          return;
        }
      }
    }

    for (const auto& record : this->state_()->hour_records) {
      if (!record.valid || !record_has_values_(record.values) ||
          !date_key_in_range_(record.date_key, from_date_key, to_date_key)) {
        continue;
      }
      if (!writer.printf(
              "@hour|%u|%u|%u|%d|%d|%d|%d|%d|%d|%d\n", static_cast<unsigned>(0x80000000UL + record.sequence),
              static_cast<unsigned>(record.date_key), static_cast<unsigned>(record.hour),
              printable_wh_(record.values.electrical_input_wh), printable_wh_(record.values.heating_input_wh),
              printable_wh_(record.values.cooling_input_wh), printable_wh_(record.values.heatpump_heat_output_wh),
              printable_wh_(record.values.heatpump_cooling_output_wh),
              printable_wh_(record.values.boiler_heat_output_wh), printable_wh_(record.values.system_heat_output_wh))) {
        ESP_LOGW(TAG, "Failed to write energy hour history response");
        return;
      }
    }
  }

  if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to terminate energy history response");
  }
}

std::string OpenQuattEnergyHistory::get_available_label() const {
  if (!this->is_partition_ready_()) {
    return "Niet beschikbaar";
  }
  if (this->stored_day_count_ == 0) {
    return "Geen data";
  }
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%u %s", static_cast<unsigned>(this->stored_day_count_),
                this->stored_day_count_ == 1U ? "dag" : "dagen");
  return buffer;
}

std::string OpenQuattEnergyHistory::get_oldest_day_label() const { return format_date_key_(this->oldest_date_key_); }

std::string OpenQuattEnergyHistory::get_newest_day_label() const { return format_date_key_(this->newest_date_key_); }

std::string OpenQuattEnergyHistory::get_last_write_label() const {
  if (this->last_write_timestamp_s_ == 0) {
    return "Geen data";
  }
  if (!this->time_is_valid_()) {
    return "Onbekend";
  }
  auto formatted =
      ESPTime::from_epoch_local(static_cast<time_t>(this->last_write_timestamp_s_)).strftime("%d-%m %H:%M");
  return formatted.empty() ? "Onbekend" : formatted;
}

float OpenQuattEnergyHistory::get_storage_kib() const { return static_cast<float>(this->flash_total_bytes_) / 1024.0f; }

uint32_t OpenQuattEnergyHistory::get_write_count() const { return this->next_sequence_; }

}  // namespace openquatt_energy_history
}  // namespace esphome
