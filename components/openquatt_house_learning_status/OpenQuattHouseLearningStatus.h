#pragma once

#include <atomic>
#include <cstddef>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "PsramBuffer.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/components/web_server/web_server.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_house_learning_status {

using openquatt_common::PsramBuffer;

class OpenQuattHouseLearningStatus : public Component {
 public:
  void set_web_server(web_server::WebServer* web_server) { this->web_server_ = web_server; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* web_auth) { this->web_auth_ = web_auth; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

  // These publishers copy a complete UTF-8 JSON document into immutable endpoint
  // storage. Call them only from the ESPHome main loop task.
  bool publish_status_json(const char* json, size_t length);
  bool publish_export_json(const char* json, size_t length);

  bool request_is_authenticated(AsyncWebServerRequest* request) const {
    return this->web_auth_ != nullptr && this->web_auth_->request_is_authenticated(request);
  }
  bool storage_available() const { return this->storage_available_.load(std::memory_order_acquire); }
  bool try_begin_request() const;
  void end_request() const;
  bool snapshot_status(size_t* length) const;
  bool snapshot_export(size_t* length) const;
  void write_snapshot(httpd_req_t* req, size_t length) const;

 protected:
  static constexpr size_t STATUS_BUFFER_SIZE = 4U * 1024U;
  static constexpr size_t EXPORT_BUFFER_SIZE = 32U * 1024U;
  static constexpr size_t REQUEST_BUFFER_SIZE = EXPORT_BUFFER_SIZE;

  bool publish_json_(PsramBuffer<char>& destination, size_t* stored_length, const char* json, size_t length);
  bool snapshot_json_(const PsramBuffer<char>& source, const size_t* stored_length, size_t* snapshot_length) const;
  bool cache_lock_() const;
  void cache_unlock_() const;

  web_server::WebServer* web_server_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* web_auth_{nullptr};
  PsramBuffer<char> status_buffer_{};
  PsramBuffer<char> export_buffer_{};
  mutable PsramBuffer<char> request_buffer_{};
  size_t status_length_{0U};
  size_t export_length_{0U};
  StaticSemaphore_t cache_mutex_storage_{};
  StaticSemaphore_t request_mutex_storage_{};
  mutable SemaphoreHandle_t cache_mutex_{nullptr};
  mutable SemaphoreHandle_t request_mutex_{nullptr};
  TaskHandle_t main_loop_task_{nullptr};
  std::atomic<bool> storage_available_{false};
};

}  // namespace openquatt_house_learning_status
}  // namespace esphome
