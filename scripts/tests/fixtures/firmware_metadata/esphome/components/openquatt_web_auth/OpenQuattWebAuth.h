#pragma once
#include "esphome/components/web_server_idf/web_server_idf.h"

namespace esphome {
namespace openquatt_web_auth {
class OpenQuattWebAuth {
 public:
  bool permitted{false};
  bool request_is_authenticated(AsyncWebServerRequest*) const { return this->permitted; }
};
}  // namespace openquatt_web_auth
}  // namespace esphome
