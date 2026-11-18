#include "OpenQuattHouseLearningStatus.h"

#include "OpenQuattHttpStatus.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_random.h>
#include <esp_timer.h>

#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_house_learning_status {
namespace {

static const char* const TAG = "openquatt.house_learning";
static constexpr const char* STATUS_PATH = "/openquatt/learning/status";
static constexpr const char* EXPORT_PATH = "/openquatt/learning/export";
static constexpr const char* LINE_PATH = "/openquatt/learning/line";
using LineResult = OpenQuattHouseLearningStatus::LineResult;

bool parse_line_value(const std::string& value, float& result) {
  if (value.empty() || value.size() > 16) return false;
  errno = 0;
  char* end = nullptr;
  result = std::strtof(value.c_str(), &end);
  return errno == 0 && end == value.data() + value.size() && std::isfinite(result);
}

void send_line_json(AsyncWebServerRequest* request, const char* status, const char* json) {
  httpd_req_t* req = *request;
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

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
    const auto method = request->method();
    if (url_path_matches(url, LINE_PATH)) return method == HTTP_GET || method == HTTP_POST;
    return method == HTTP_GET && (url_path_matches(url, STATUS_PATH) || url_path_matches(url, EXPORT_PATH));
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    if (!this->parent_->request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    if (url_path_matches(url, LINE_PATH)) {
      this->handle_line_(request);
      return;
    }
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

    const bool exporting = url_path_matches(url, EXPORT_PATH);
    size_t snapshot_length = 0U;
    const bool snapshot_ready =
        exporting ? this->parent_->snapshot_export(&snapshot_length) : this->parent_->snapshot_status(&snapshot_length);
    if (!snapshot_ready) {
      this->parent_->end_request();
      openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::SERVICE_UNAVAILABLE,
                                         R"({"ok":false,"error":"snapshot_unavailable"})");
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
  void handle_line_(AsyncWebServerRequest* request) {
    if (request->method() == HTTP_GET) {
      const auto raw = request->arg("ticket");
      char* end = nullptr;
      errno = 0;
      const auto ticket = std::strtoul(raw.c_str(), &end, 10);
      const auto result = !raw.empty() && raw.size() <= 10 &&
                                  raw.find_first_not_of("0123456789") == std::string::npos && errno == 0 &&
                                  end == raw.data() + raw.size() && ticket > 0 && ticket <= UINT32_MAX
                              ? this->parent_->line_result(static_cast<uint32_t>(ticket))
                              : LineResult::UNKNOWN;
      switch (result) {
        case LineResult::STORED:
          send_line_json(request, "200 OK", R"({"stored":true})");
          break;
        case LineResult::REFUSED:
          send_line_json(request, "409 Conflict", R"({"stored":false})");
          break;
        case LineResult::UNCERTAIN:
          send_line_json(request, "503 Service Unavailable", R"({"stored":null})");
          break;
        case LineResult::QUEUED:
          send_line_json(request, "202 Accepted", "{}");
          break;
        case LineResult::UNKNOWN:
          send_line_json(request, "404 Not Found", "{}");
          break;
      }
      return;
    }
    if (!this->parent_->request_allows_write(request)) {
      send_line_json(request, "403 Forbidden", "{}");
      return;
    }
    const auto action = request->arg("action");
    const bool restore = action == "restore";
    constexpr std::array<const char*, 5> keys{"cold", "rated", "cutoff", "proposed_rated", "proposed_cutoff"};
    std::array<float, 5> values{};
    bool valid = restore || action == "apply";
    for (size_t i = 0; valid && i < (restore ? 3U : keys.size()); ++i)
      valid = parse_line_value(request->arg(keys[i]), values[i]);
    if (!valid) {
      send_line_json(request, "400 Bad Request", "{}");
      return;
    }
    const auto ticket = this->parent_->queue_line_change(values, restore);
    if (ticket == 0) {
      send_line_json(request, "409 Conflict", "{}");
      return;
    }
    char json[32];
    std::snprintf(json, sizeof(json), R"({"ticket":%u})", static_cast<unsigned>(ticket));
    send_line_json(request, "202 Accepted", json);
  }

  OpenQuattHouseLearningStatus* parent_;
};

}  // namespace

float OpenQuattHouseLearningStatus::get_setup_priority() const { return setup_priority::WIFI; }

void OpenQuattHouseLearningStatus::setup() {
  this->main_loop_task_ = xTaskGetCurrentTaskHandle();
  this->line_ticket_ = esp_random();
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

uint32_t OpenQuattHouseLearningStatus::queue_line_change(const std::array<float, 5>& values, bool restore) {
  auto* base = web_server_base::global_web_server_base;
  if (this->change_line_ == nullptr || base == nullptr) return 0;
  const auto epoch = base->recovery_epoch();
  if (base->is_recovery_active() || !this->cache_lock_()) return 0;
  if (this->line_result_ == LineResult::QUEUED) {
    this->cache_unlock_();
    return 0;
  }
  if (++this->line_ticket_ == 0) ++this->line_ticket_;
  const auto ticket = this->line_ticket_;
  this->line_result_ = LineResult::QUEUED;
  this->cache_unlock_();
  this->defer([this, values, restore, epoch]() {
    auto* active_base = web_server_base::global_web_server_base;
    auto result = LineResult::REFUSED;
    if (active_base != nullptr && !active_base->is_recovery_active() && active_base->recovery_epoch() == epoch)
      result =
          static_cast<LineResult>(this->change_line_(values[0], values[1], values[2], values[3], values[4], restore));
    if (this->cache_lock_()) {
      this->line_result_ = result;
      this->cache_unlock_();
    }
  });
  return ticket;
}

OpenQuattHouseLearningStatus::LineResult OpenQuattHouseLearningStatus::line_result(uint32_t ticket) const {
  if (!this->cache_lock_()) return LineResult::UNKNOWN;
  const auto result = ticket == this->line_ticket_ ? this->line_result_ : LineResult::UNKNOWN;
  this->cache_unlock_();
  return result;
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

void OpenQuattHouseLearningStatus::invalidate_shadow() {
  auto generation = this->shadow_generation_.load(std::memory_order_relaxed);
  while (generation != UINT32_MAX &&
         !this->shadow_generation_.compare_exchange_weak(generation, generation + 1U, std::memory_order_release,
                                                         std::memory_order_relaxed)) {
  }
}

bool OpenQuattHouseLearningStatus::publish_status_json(const char* json, size_t length, size_t shadow_prefix_length,
                                                       uint64_t shadow_expires_ms, uint32_t shadow_generation,
                                                       uint32_t context_revision) {
  if (!this->storage_available() || xTaskGetCurrentTaskHandle() != this->main_loop_task_ || json == nullptr ||
      length == 0U || length >= this->status_buffer_.size() || std::memchr(json, '\0', length) != nullptr ||
      (shadow_prefix_length != 0U &&
       (shadow_prefix_length >= length || json[shadow_prefix_length] != ',' || json[length - 1U] != '}' ||
        context_revision == 0U || (shadow_expires_ms != 0U && shadow_generation == 0U))) ||
      !this->cache_lock_())
    return false;
  std::memcpy(this->status_buffer_.data(), json, length);
  this->status_buffer_[length] = '\0';
  this->status_length_ = length;
  this->shadow_prefix_length_ = shadow_prefix_length;
  this->shadow_expires_ms_ = shadow_expires_ms;
  this->status_shadow_generation_ = shadow_generation;
  this->status_context_revision_ = context_revision;
  this->cache_unlock_();
  return true;
}

bool OpenQuattHouseLearningStatus::publish_shadow_json(const char* json, size_t length, uint64_t shadow_expires_ms,
                                                       uint32_t shadow_generation, uint32_t context_revision) {
  // A failed refresh must not leave the previous available comparison visible.
  if (!this->storage_available() || xTaskGetCurrentTaskHandle() != this->main_loop_task_ || json == nullptr ||
      length == 0U || length >= this->status_buffer_.size() || json[0] != ',' || json[length - 1U] != '}' ||
      std::memchr(json, '\0', length) != nullptr || context_revision == 0U ||
      (shadow_expires_ms != 0U && shadow_generation == 0U) || !this->cache_lock_()) {
    this->invalidate_shadow();
    return false;
  }
  const size_t prefix_length = this->shadow_prefix_length_;
  const bool valid =
      prefix_length != 0U && prefix_length < this->status_length_ && prefix_length < this->status_buffer_.size() &&
      length < this->status_buffer_.size() - prefix_length && context_revision == this->status_context_revision_;
  if (valid) {
    std::memcpy(this->status_buffer_.data() + prefix_length, json, length);
    this->status_length_ = prefix_length + length;
    this->status_buffer_[this->status_length_] = '\0';
    this->shadow_expires_ms_ = shadow_expires_ms;
    this->status_shadow_generation_ = shadow_generation;
  } else {
    this->invalidate_shadow();
  }
  this->cache_unlock_();
  return valid;
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
  if (length == nullptr || !this->storage_available() || !this->cache_lock_()) return false;
  size_t copy_length = this->status_length_;
  if (copy_length == 0U || copy_length >= this->status_buffer_.size()) {
    this->cache_unlock_();
    return false;
  }
  uint32_t ttl_ms = 0U;
  const bool leased_shadow = this->shadow_prefix_length_ != 0U && this->shadow_expires_ms_ != 0U;
  if (leased_shadow) {
    const auto now_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
    const auto generation = this->shadow_generation();
    if (generation == this->status_shadow_generation_ && generation != UINT32_MAX &&
        this->shadow_expires_ms_ > now_ms && this->shadow_expires_ms_ - now_ms <= UINT32_MAX) {
      ttl_ms = static_cast<uint32_t>(this->shadow_expires_ms_ - now_ms);
    }
    copy_length = ttl_ms == 0U ? this->shadow_prefix_length_ : copy_length - 1U;
  }
  char* const destination = this->request_buffer_.data();
  std::memcpy(destination, this->status_buffer_.data(), copy_length);
  this->cache_unlock_();

  // Expired or invalidated comparisons never leave the cache. The TTL in a
  // valid response is calculated at the read, not at status publication.
  if (leased_shadow) {
    const size_t capacity = this->request_buffer_.size() - copy_length;
    const int written = ttl_ms == 0U ? std::snprintf(destination + copy_length, capacity, ",\"shadow_expired\":true}")
                                     : std::snprintf(destination + copy_length, capacity, ",\"shadow_ttl_ms\":%u}",
                                                     static_cast<unsigned>(ttl_ms));
    if (written < 0 || static_cast<size_t>(written) >= capacity) return false;
    copy_length += static_cast<size_t>(written);
  }
  destination[copy_length] = '\0';
  *length = copy_length;
  return true;
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
