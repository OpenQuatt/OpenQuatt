#pragma once
#include "esphome/components/web_server_idf/web_server_idf.h"

namespace esphome {
namespace web_server_base {
class WebServerBase {
 public:
  void add_handler(AsyncWebHandler* handler) { this->handlers.push_back(handler); }
  std::vector<AsyncWebHandler*> handlers;
};
inline WebServerBase* global_web_server_base = nullptr;
}  // namespace web_server_base
}  // namespace esphome
