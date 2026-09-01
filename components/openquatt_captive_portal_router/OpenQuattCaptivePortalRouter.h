#pragma once

#include "esphome/components/captive_portal/captive_portal.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_captive_portal_router {

class OpenQuattCaptivePortalRouter final : public AsyncWebHandler, public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool canHandle(AsyncWebServerRequest* request) const override;
  void handleRequest(AsyncWebServerRequest* request) override;

 protected:
  bool request_targets_soft_ap_(AsyncWebServerRequest* request) const;
};

}  // namespace openquatt_captive_portal_router
}  // namespace esphome
