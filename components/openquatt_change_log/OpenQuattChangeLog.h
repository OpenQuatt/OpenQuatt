#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "OpenQuattChangeLogStore.h"
#include "OpenQuattFlashLayout.h"
#include "PsramBuffer.h"
#include "esp_partition.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/components/web_server/web_server.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_change_log {

using openquatt_common::OpenQuattFlashLayout;
using openquatt_common::PsramBuffer;

static_assert(SECTOR_SIZE == OpenQuattFlashLayout::SECTOR_SIZE &&
                  SECTOR_COUNT == OpenQuattFlashLayout::CHANGE_LOG_SECTOR_COUNT,
              "The change log store must match its reserved flash window");

// Window-relative access to the reserved change-log sectors in openquatt_data.
struct ChangeLogPartitionIo {
  const esp_partition_t* partition{nullptr};

  bool read(size_t offset, void* data, size_t size) const;
  bool erase_sector(size_t sector) const;
  bool write(size_t offset, const void* data, size_t size) const;
};

enum class ChangeLogAppendResult { STORED, INVALID, BUSY, UNAVAILABLE };

// Settings changes made through the web UI. The HTTP task validates and queues entries; only the
// main loop writes flash. Entries, the write queue and the response scratch live in strict PSRAM.
class OpenQuattChangeLog : public Component {
 public:
  void set_web_server(web_server::WebServer* web_server) { this->web_server_ = web_server; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* web_auth) { this->web_auth_ = web_auth; }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool request_is_authenticated(AsyncWebServerRequest* request) const {
    return this->web_auth_ != nullptr && this->web_auth_->request_is_authenticated(request);
  }
  std::string get_csrf_token() const { return this->web_auth_ == nullptr ? "" : this->web_auth_->get_csrf_token(); }
  bool storage_available() const { return this->storage_available_.load(std::memory_order_acquire); }

  // HTTP task only. A request holds the response scratch from render_json() until end_request().
  bool try_begin_request();
  void end_request();
  size_t render_json();
  const char* response_data() const { return this->json_buffer_.data(); }
  ChangeLogAppendResult append(uint32_t at, ChangeLogText label, ChangeLogText from, ChangeLogText to, uint32_t* seq);

 protected:
  static constexpr size_t PENDING_CAPACITY = 8U;

  bool lock_state_(TickType_t wait) const;
  void unlock_state_() const;
  void load_flash_();
  bool persist_next_(TickType_t wait);

  web_server::WebServer* web_server_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* web_auth_{nullptr};
  PsramBuffer<ChangeLogRecord> entries_buffer_{};
  PsramBuffer<ChangeLogRecord> pending_buffer_{};
  PsramBuffer<char> json_buffer_{};
  ChangeLogRing entries_{};
  ChangeLogQueue pending_{};
  ChangeLogRateLimiter rate_limiter_{};
  ChangeLogPartitionIo io_{};
  ChangeLogFlashStore<ChangeLogPartitionIo> store_{io_};
  uint32_t next_seq_{1U};
  StaticSemaphore_t state_mutex_storage_{};
  StaticSemaphore_t request_mutex_storage_{};
  mutable SemaphoreHandle_t state_mutex_{nullptr};
  SemaphoreHandle_t request_mutex_{nullptr};
  std::atomic<bool> storage_available_{false};
  std::atomic<bool> persistent_{false};
  std::atomic<size_t> pending_count_{0U};
};

}  // namespace openquatt_change_log
}  // namespace esphome
