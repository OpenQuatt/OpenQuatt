#include "OpenQuattHouseLearningStatus.h"

#include <cstring>

#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_house_learning_status {
namespace {

static const char* const TAG = "openquatt.house_learning";
static constexpr const char* STATUS_PATH = "/openquatt/learning/status";
static constexpr const char* EXPORT_PATH = "/openquatt/learning/export";

bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) {
    return false;
  }
  const size_t path_length = std::strlen(path);
  return std::strncmp(url, path, path_length) == 0 && (url[path_length] == '\0' || url[path_length] == '?');
}

class OpenQuattHouseLearningStatusRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattHouseLearningStatusRequestHandler(OpenQuattHouseLearningStatus* parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    return request->method() == HTTP_GET && (url_path_matches(url, STATUS_PATH) || url_path_matches(url, EXPORT_PATH));
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    if (!this->parent_->request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    if (!this->parent_->storage_available()) {
      request->send(503, "application/json", R"({"ok":false,"error":"psram_unavailable"})");
      return;
    }
    if (!this->parent_->try_begin_request()) {
      request->send(429, "application/json", R"({"ok":false,"error":"request_busy"})");
      return;
    }

    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    const bool exporting = url_path_matches(url, EXPORT_PATH);
    size_t snapshot_length = 0U;
    const bool snapshot_ready =
        exporting ? this->parent_->snapshot_export(&snapshot_length) : this->parent_->snapshot_status(&snapshot_length);
    if (!snapshot_ready) {
      this->parent_->end_request();
      request->send(503, "application/json", R"({"ok":false,"error":"snapshot_unavailable"})");
      return;
    }

    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (exporting) {
      httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"openquatt-house-learning.json\"");
    }
    this->parent_->write_snapshot(req, snapshot_length);
    this->parent_->end_request();
  }

 protected:
  OpenQuattHouseLearningStatus* parent_;
};

}  // namespace

float OpenQuattHouseLearningStatus::get_setup_priority() const { return setup_priority::WIFI - 1.0f; }

void OpenQuattHouseLearningStatus::setup() {
  this->main_loop_task_ = xTaskGetCurrentTaskHandle();
  this->cache_mutex_ = xSemaphoreCreateMutexStatic(&this->cache_mutex_storage_);
  this->request_mutex_ = xSemaphoreCreateMutexStatic(&this->request_mutex_storage_);

  const bool allocated = this->cache_mutex_ != nullptr && this->request_mutex_ != nullptr &&
                         this->status_buffer_.allocate_external(STATUS_BUFFER_SIZE) &&
                         this->export_buffer_.allocate_external(EXPORT_BUFFER_SIZE) &&
                         this->request_buffer_.allocate_external(REQUEST_BUFFER_SIZE);
  this->storage_available_.store(allocated, std::memory_order_release);
  if (!allocated) {
    this->status_buffer_.release();
    this->export_buffer_.release();
    this->request_buffer_.release();
    ESP_LOGE(TAG, "House-learning endpoint requires three strict PSRAM buffers (%u/%u/%u bytes)",
             static_cast<unsigned>(STATUS_BUFFER_SIZE), static_cast<unsigned>(EXPORT_BUFFER_SIZE),
             static_cast<unsigned>(REQUEST_BUFFER_SIZE));
  }

  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    return;
  }
  web_server_base::global_web_server_base->add_handler(new OpenQuattHouseLearningStatusRequestHandler(this));
}

void OpenQuattHouseLearningStatus::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt passive house-learning endpoint");
  ESP_LOGCONFIG(TAG, "  Web server: %s", this->web_server_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Web authentication: %s", this->web_auth_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Status cache: %s (%u bytes)", this->storage_available() ? "PSRAM" : "unavailable",
                static_cast<unsigned>(STATUS_BUFFER_SIZE));
  ESP_LOGCONFIG(TAG, "  Export cache: %s (%u bytes)", this->storage_available() ? "PSRAM" : "unavailable",
                static_cast<unsigned>(EXPORT_BUFFER_SIZE));
  ESP_LOGCONFIG(TAG, "  Request scratch: %s (%u bytes)", this->storage_available() ? "PSRAM" : "unavailable",
                static_cast<unsigned>(REQUEST_BUFFER_SIZE));
}

bool OpenQuattHouseLearningStatus::cache_lock_() const {
  return this->cache_mutex_ != nullptr && xSemaphoreTake(this->cache_mutex_, portMAX_DELAY) == pdTRUE;
}

void OpenQuattHouseLearningStatus::cache_unlock_() const { xSemaphoreGive(this->cache_mutex_); }

bool OpenQuattHouseLearningStatus::publish_json_(PsramBuffer<char>& destination, size_t* stored_length,
                                                 const char* json, size_t length) {
  if (!this->storage_available() || xTaskGetCurrentTaskHandle() != this->main_loop_task_ || json == nullptr ||
      stored_length == nullptr || length == 0U || length >= destination.size() ||
      std::memchr(json, '\0', length) != nullptr || !this->cache_lock_()) {
    return false;
  }
  std::memcpy(destination.data(), json, length);
  destination[length] = '\0';
  *stored_length = length;
  this->cache_unlock_();
  return true;
}

bool OpenQuattHouseLearningStatus::publish_status_json(const char* json, size_t length) {
  return this->publish_json_(this->status_buffer_, &this->status_length_, json, length);
}

bool OpenQuattHouseLearningStatus::publish_export_json(const char* json, size_t length) {
  return this->publish_json_(this->export_buffer_, &this->export_length_, json, length);
}

bool OpenQuattHouseLearningStatus::try_begin_request() const {
  return this->request_mutex_ != nullptr && xSemaphoreTake(this->request_mutex_, 0) == pdTRUE;
}

void OpenQuattHouseLearningStatus::end_request() const {
  if (this->request_mutex_ != nullptr) {
    xSemaphoreGive(this->request_mutex_);
  }
}

bool OpenQuattHouseLearningStatus::snapshot_json_(const PsramBuffer<char>& source, const size_t* stored_length,
                                                  size_t* snapshot_length) const {
  if (stored_length == nullptr || snapshot_length == nullptr || !this->storage_available() || !this->cache_lock_()) {
    return false;
  }
  const size_t length = *stored_length;
  const bool valid = length > 0U && length < source.size() && length < this->request_buffer_.size();
  if (valid) {
    std::memcpy(this->request_buffer_.data(), source.data(), length);
    this->request_buffer_[length] = '\0';
    *snapshot_length = length;
  }
  this->cache_unlock_();
  return valid;
}

bool OpenQuattHouseLearningStatus::snapshot_status(size_t* length) const {
  return this->snapshot_json_(this->status_buffer_, &this->status_length_, length);
}

bool OpenQuattHouseLearningStatus::snapshot_export(size_t* length) const {
  return this->snapshot_json_(this->export_buffer_, &this->export_length_, length);
}

void OpenQuattHouseLearningStatus::write_snapshot(httpd_req_t* req, size_t length) const {
  if (req == nullptr || !this->storage_available() || length == 0U || length >= this->request_buffer_.size()) {
    return;
  }
  httpd_resp_send(req, this->request_buffer_.data(), static_cast<ssize_t>(length));
}

}  // namespace openquatt_house_learning_status
}  // namespace esphome
