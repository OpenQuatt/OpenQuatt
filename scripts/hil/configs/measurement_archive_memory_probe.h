#pragma once

#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

namespace oq_archive_hil {

// Same handler in the baseline and candidate. The stack watermark belongs
// to HTTPD, and includes the stack cost of handling this probe.
class MemoryProbe : public AsyncWebHandler {
 public:
  explicit MemoryProbe(esphome::openquatt_web_auth::OpenQuattWebAuth& auth) : auth_(auth) {}
  bool canHandle(AsyncWebServerRequest* request) const override {
    const httpd_req_t* raw = *request;
    return request->method() == HTTP_GET && raw && std::strcmp(raw->uri, "/openquatt/hil/archive-memory") == 0;
  }
  void handleRequest(AsyncWebServerRequest* request) override {
    if (!auth_.request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    constexpr auto caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    char response[192];
    const int size = std::snprintf(
        response, sizeof(response),
        R"({"uptime_ms":%llu,"heap_b":%u,"heap_min_b":%u,"largest_b":%u,"httpd_stack_b":%u,"psram_b":%u})",
        static_cast<unsigned long long>(esp_timer_get_time() / 1000),
        static_cast<unsigned>(heap_caps_get_free_size(caps)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(caps)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(caps)),
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    if (size < 0 || static_cast<size_t>(size) >= sizeof(response)) {
      request->send(500);
      return;
    }
    httpd_resp_set_hdr(*request, "Cache-Control", "no-store");
    httpd_resp_set_type(*request, "application/json");
    httpd_resp_send(*request, response, size);
  }

 private:
  esphome::openquatt_web_auth::OpenQuattWebAuth& auth_;
};

inline void setup(esphome::openquatt_web_auth::OpenQuattWebAuth& auth) {
  if (esphome::web_server_base::global_web_server_base)
    esphome::web_server_base::global_web_server_base->add_handler(new MemoryProbe(auth));
}
}  // namespace oq_archive_hil
