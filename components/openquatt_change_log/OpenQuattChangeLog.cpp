#include "OpenQuattChangeLog.h"

#include "OpenQuattHttpStatus.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_change_log {
namespace {

static const char* const TAG = "openquatt.change_log";
static constexpr const char* CHANGE_LOG_PATH = "/openquatt/change-log";
static constexpr const char* FLASH_PARTITION_LABEL = "openquatt_data";
static constexpr TickType_t HTTP_LOCK_WAIT = pdMS_TO_TICKS(100);
static constexpr TickType_t SHUTDOWN_LOCK_WAIT = pdMS_TO_TICKS(50);

bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) {
    return false;
  }
  const size_t path_length = std::strlen(path);
  return std::strncmp(url, path, path_length) == 0 && (url[path_length] == '\0' || url[path_length] == '?');
}

bool header_matches_host(const std::string& value, const std::string& host) {
  if (value.empty() || host.empty()) return false;
  size_t start = 0U;
  const size_t scheme = value.find("://");
  if (scheme != std::string::npos) start = scheme + 3U;
  const size_t end = value.find_first_of("/?#", start);
  return value.substr(start, end == std::string::npos ? std::string::npos : end - start) == host;
}

bool passes_same_origin(AsyncWebServerRequest* request) {
  const auto host = request->get_header("Host");
  if (!host.has_value() || host->empty()) return false;
  const auto origin = request->get_header("Origin");
  if (origin.has_value() && !header_matches_host(origin.value(), host.value())) return false;
  const auto referer = request->get_header("Referer");
  return !referer.has_value() || header_matches_host(referer.value(), host.value());
}

ChangeLogText text_view(const std::string& value) { return ChangeLogText{value.data(), value.size()}; }

class OpenQuattChangeLogRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattChangeLogRequestHandler(OpenQuattChangeLog* parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    return (request->method() == HTTP_GET || request->method() == HTTP_POST) && url_path_matches(url, CHANGE_LOG_PATH);
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    if (!this->parent_->request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    if (request->method() == HTTP_POST) {
      this->handle_append_(request);
      return;
    }
    this->handle_list_(request);
  }

 protected:
  void handle_list_(AsyncWebServerRequest* request) {
    if (!this->parent_->storage_available()) {
      openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::SERVICE_UNAVAILABLE,
                                         R"({"ok":false,"error":"psram_unavailable"})");
      return;
    }
    if (!this->parent_->try_begin_request()) {
      openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::TOO_MANY_REQUESTS,
                                         R"({"ok":false,"error":"request_busy"})");
      return;
    }
    const size_t length = this->parent_->render_json();
    if (length == 0U) {
      this->parent_->end_request();
      openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::SERVICE_UNAVAILABLE,
                                         R"({"ok":false,"error":"snapshot_unavailable"})");
      return;
    }
    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, this->parent_->response_data(), static_cast<ssize_t>(length));
    this->parent_->end_request();
  }

  void handle_append_(AsyncWebServerRequest* request) {
    const std::string token = request->arg("csrf_token");
    if (!passes_same_origin(request) || token.empty() || token != this->parent_->get_csrf_token()) {
      openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::FORBIDDEN,
                                         R"({"ok":false,"error":"forbidden"})");
      return;
    }
    const std::string at_text = request->arg("at");
    const std::string label = request->arg("label");
    const std::string from = request->arg("from");
    const std::string to = request->arg("to");
    uint32_t at = 0U;
    uint32_t seq = 0U;
    const ChangeLogAppendResult result =
        parse_timestamp(at_text.data(), at_text.size(), &at)
            ? this->parent_->append(at, text_view(label), text_view(from), text_view(to), &seq)
            : ChangeLogAppendResult::INVALID;
    switch (result) {
      case ChangeLogAppendResult::STORED: {
        char body[48];
        std::snprintf(body, sizeof(body), R"({"ok":true,"seq":%)" PRIu32 "}", seq);
        auto* response = request->beginResponse(200, "application/json", body);
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
        return;
      }
      case ChangeLogAppendResult::INVALID:
        request->send(400, "application/json", R"({"ok":false,"error":"invalid_entry"})");
        return;
      case ChangeLogAppendResult::BUSY:
        openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::TOO_MANY_REQUESTS,
                                           R"({"ok":false,"error":"busy"})");
        return;
      case ChangeLogAppendResult::UNAVAILABLE:
      default:
        openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::SERVICE_UNAVAILABLE,
                                           R"({"ok":false,"error":"unavailable"})");
        return;
    }
  }

  OpenQuattChangeLog* parent_;
};

}  // namespace

bool ChangeLogPartitionIo::read(size_t offset, void* data, size_t size) const {
  return this->partition != nullptr &&
         esp_partition_read(this->partition, OpenQuattFlashLayout::CHANGE_LOG_OFFSET + offset, data, size) == ESP_OK;
}

bool ChangeLogPartitionIo::erase_sector(size_t sector) const {
  return this->partition != nullptr && sector < SECTOR_COUNT &&
         esp_partition_erase_range(this->partition, OpenQuattFlashLayout::CHANGE_LOG_OFFSET + (sector * SECTOR_SIZE),
                                   SECTOR_SIZE) == ESP_OK;
}

bool ChangeLogPartitionIo::write(size_t offset, const void* data, size_t size) const {
  return this->partition != nullptr &&
         esp_partition_write(this->partition, OpenQuattFlashLayout::CHANGE_LOG_OFFSET + offset, data, size) == ESP_OK;
}

float OpenQuattChangeLog::get_setup_priority() const { return setup_priority::WIFI; }

void OpenQuattChangeLog::setup() {
  this->state_mutex_ = xSemaphoreCreateMutexStatic(&this->state_mutex_storage_);
  this->request_mutex_ = xSemaphoreCreateMutexStatic(&this->request_mutex_storage_);

  const bool allocated = this->state_mutex_ != nullptr && this->request_mutex_ != nullptr &&
                         this->entries_buffer_.allocate_external(CAPACITY) &&
                         this->pending_buffer_.allocate_external(PENDING_CAPACITY) &&
                         this->json_buffer_.allocate_external(JSON_BUFFER_SIZE);
  if (allocated) {
    this->entries_.bind(this->entries_buffer_.data(), CAPACITY);
    this->pending_.bind(this->pending_buffer_.data(), PENDING_CAPACITY);
    this->load_flash_();
  } else {
    this->entries_buffer_.release();
    this->pending_buffer_.release();
    this->json_buffer_.release();
    ESP_LOGE(TAG, "Change log requires strict PSRAM buffers (%u/%u/%u bytes)",
             static_cast<unsigned>(CAPACITY * sizeof(ChangeLogRecord)),
             static_cast<unsigned>(PENDING_CAPACITY * sizeof(ChangeLogRecord)),
             static_cast<unsigned>(JSON_BUFFER_SIZE));
  }
  this->storage_available_.store(allocated, std::memory_order_release);

  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    return;
  }
  // Registered without PSRAM too, so the endpoint reports 503 instead of looking unsupported.
  web_server_base::global_web_server_base->add_handler(new OpenQuattChangeLogRequestHandler(this));
}

void OpenQuattChangeLog::load_flash_() {
  this->io_.partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, FLASH_PARTITION_LABEL);
  if (this->io_.partition == nullptr || this->io_.partition->size < OpenQuattFlashLayout::CHANGE_LOG_END_OFFSET) {
    this->io_.partition = nullptr;
    ESP_LOGW(TAG, "Change log flash window is unavailable in '%s'; changes are kept until reboot",
             FLASH_PARTITION_LABEL);
    return;
  }
  if (!this->store_.load([this](const ChangeLogRecord& record) { this->entries_.push(record); })) {
    this->entries_.clear();
    ESP_LOGW(TAG, "Change log flash window could not be read; changes are kept until reboot");
    return;
  }
  const uint32_t last = this->store_.last_sequence();
  this->next_seq_ = (last + 1U == UINT32_MAX || last + 1U == 0U) ? 1U : last + 1U;
  this->persistent_.store(true, std::memory_order_release);
  if (this->store_.skipped_records() > 0U) {
    ESP_LOGW(TAG, "Skipped %u unreadable change log records", static_cast<unsigned>(this->store_.skipped_records()));
  }
}

void OpenQuattChangeLog::loop() {
  if (this->pending_count_.load(std::memory_order_acquire) == 0U) {
    return;
  }
  // One record per loop keeps flash work, including the occasional sector erase, short.
  this->persist_next_(0);
}

void OpenQuattChangeLog::on_shutdown() {
  for (size_t attempt = 0U; attempt < PENDING_CAPACITY && this->pending_count_.load(std::memory_order_acquire) > 0U;
       ++attempt) {
    if (!this->persist_next_(SHUTDOWN_LOCK_WAIT)) {
      break;
    }
  }
}

bool OpenQuattChangeLog::persist_next_(TickType_t wait) {
  ChangeLogRecord record{};
  if (!this->lock_state_(wait)) {
    return false;
  }
  const bool have_record = this->pending_.pop(&record);
  this->pending_count_.store(this->pending_.size(), std::memory_order_release);
  this->unlock_state_();
  if (!have_record || !this->persistent_.load(std::memory_order_acquire)) {
    return false;
  }

  const uint32_t started_ms = millis();
  if (this->store_.append(record)) {
    ESP_LOGD(TAG, "Stored change %" PRIu32 " in %" PRIu32 " ms", record.seq, millis() - started_ms);
    return true;
  }
  // Fail closed for flash: later changes stay visible in RAM until reboot.
  this->persistent_.store(false, std::memory_order_release);
  if (this->lock_state_(wait)) {
    this->pending_.clear();
    this->pending_count_.store(0U, std::memory_order_release);
    this->unlock_state_();
  }
  ESP_LOGW(TAG, "Change log flash write failed; persistence is disabled until reboot");
  return false;
}

ChangeLogAppendResult OpenQuattChangeLog::append(uint32_t at, ChangeLogText label, ChangeLogText from, ChangeLogText to,
                                                 uint32_t* seq) {
  if (!this->storage_available() || seq == nullptr) {
    return ChangeLogAppendResult::UNAVAILABLE;
  }
  if (!this->lock_state_(HTTP_LOCK_WAIT)) {
    return ChangeLogAppendResult::BUSY;
  }
  ChangeLogRecord record{};
  ChangeLogAppendResult result = ChangeLogAppendResult::INVALID;
  if (encode_record(this->next_seq_, at, label, from, to, &record)) {
    const bool persistent = this->persistent_.load(std::memory_order_acquire);
    result = ChangeLogAppendResult::BUSY;
    if ((!persistent || !this->pending_.full()) && this->rate_limiter_.try_take(millis())) {
      if (persistent) {
        this->pending_.push(record);
        this->pending_count_.store(this->pending_.size(), std::memory_order_release);
      }
      this->entries_.push(record);
      *seq = record.seq;
      this->next_seq_ = this->next_seq_ + 1U == UINT32_MAX ? 1U : this->next_seq_ + 1U;
      result = ChangeLogAppendResult::STORED;
    }
  }
  this->unlock_state_();
  return result;
}

bool OpenQuattChangeLog::try_begin_request() {
  return this->request_mutex_ != nullptr && xSemaphoreTake(this->request_mutex_, 0) == pdTRUE;
}

void OpenQuattChangeLog::end_request() {
  if (this->request_mutex_ != nullptr) {
    xSemaphoreGive(this->request_mutex_);
  }
}

size_t OpenQuattChangeLog::render_json() {
  if (!this->storage_available() || !this->lock_state_(HTTP_LOCK_WAIT)) {
    return 0U;
  }
  const size_t length = write_change_log_json(this->json_buffer_.data(), this->json_buffer_.size(), this->entries_,
                                              this->persistent_.load(std::memory_order_acquire));
  this->unlock_state_();
  return length;
}

bool OpenQuattChangeLog::lock_state_(TickType_t wait) const {
  return this->state_mutex_ != nullptr && xSemaphoreTake(this->state_mutex_, wait) == pdTRUE;
}

void OpenQuattChangeLog::unlock_state_() const { xSemaphoreGive(this->state_mutex_); }

void OpenQuattChangeLog::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt settings change log");
  ESP_LOGCONFIG(TAG, "  Web authentication: %s", this->web_auth_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Entries: %s (%u x %u bytes)", this->storage_available() ? "PSRAM" : "unavailable",
                static_cast<unsigned>(CAPACITY), static_cast<unsigned>(sizeof(ChangeLogRecord)));
  ESP_LOGCONFIG(TAG, "  Flash: %s (%u sectors at 0x%06" PRIX32 ")",
                this->persistent_.load(std::memory_order_acquire) ? "persistent" : "unavailable",
                static_cast<unsigned>(SECTOR_COUNT), OpenQuattFlashLayout::CHANGE_LOG_OFFSET);
}

}  // namespace openquatt_change_log
}  // namespace esphome
