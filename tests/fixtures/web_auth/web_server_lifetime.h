#pragma once

#include <cassert>

#include "web_server_base.h"

// Declare immediately after WebServerBase, before components and stack handlers.
// Reverse destruction first unregisters stack handlers, then this guard releases
// the server and remaining heap routes/wrappers that live until reboot on-device.
// Their production destructors do not own the wrapped handler or server.
class WebServerLifetime {
 public:
  explicit WebServerLifetime(esphome::web_server_base::WebServerBase& base) : base_(base) {
    assert(AsyncWebHandler::live_handlers.empty());
    assert(AsyncWebServer::live_servers == 0);
  }
  WebServerLifetime(const WebServerLifetime&) = delete;
  WebServerLifetime& operator=(const WebServerLifetime&) = delete;
  ~WebServerLifetime() {
    base_.deinit();
    delete base_.get_server();
    assert(AsyncWebServer::live_servers == 0);
    while (!AsyncWebHandler::live_handlers.empty()) {
      const auto count = AsyncWebHandler::live_handlers.size();
      delete AsyncWebHandler::live_handlers.back();
      assert(AsyncWebHandler::live_handlers.size() == count - 1);
    }
    if (esphome::web_server_base::global_web_server_base == &base_) {
      esphome::web_server_base::global_web_server_base = nullptr;
    }
  }

 private:
  esphome::web_server_base::WebServerBase& base_;
};
