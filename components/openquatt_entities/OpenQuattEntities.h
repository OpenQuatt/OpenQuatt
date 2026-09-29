#pragma once

#include <array>
#include <atomic>

#include <esp_http_server.h>

#include "PsramBuffer.h"
#include "esphome/components/number/number.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/components/web_server/web_server.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_entities {

class OpenQuattEntities : public Component {
 public:
  void set_web_server(web_server::WebServer* web_server) { this->web_server_ = web_server; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* web_auth) { this->web_auth_ = web_auth; }
  void set_curve_point(size_t index, number::Number* point) {
    if (index < this->curve_points_.size()) this->curve_points_[index] = point;
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void write_entities(httpd_req_t* req, const std::string& detail, const std::string& entities) const;
  bool curve_batch_available() const;
  bool request_is_authenticated(AsyncWebServerRequest* request) const;
  std::string get_csrf_token() const;
  bool queue_curve_batch(const std::array<float, 6>& values);

 protected:
  web_server::WebServer* web_server_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* web_auth_{nullptr};
  std::array<number::Number*, 6> curve_points_{};
  std::atomic<bool> curve_batch_pending_{false};
};

}  // namespace openquatt_entities
}  // namespace esphome
