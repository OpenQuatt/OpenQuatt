#pragma once

#include "esphome/components/captive_portal/captive_portal.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/components/wifi/wifi_component.h"
#include <atomic>
#include <mutex>

namespace esphome {
namespace openquatt_captive_portal_router {

class OpenQuattCaptivePortalRouter final : public AsyncWebHandler, public Component {
 public:
  OpenQuattCaptivePortalRouter();
  void loop() override;
  bool is_portal_active() const { return this->portal_active_.load(std::memory_order_acquire); }
  void set_portal_active(bool active) { this->portal_active_.store(active, std::memory_order_release); }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool canHandle(AsyncWebServerRequest* request) const override;
  void handleRequest(AsyncWebServerRequest* request) override;

 protected:
  struct JobStatus {
    uint32_t generation;
    const char* state;
  };
  JobStatus job_status_();
  std::mutex lock_;
  char ssid_[33]{};
  char password_[65]{};
  uint32_t job_generation_{0};
  uint32_t wifi_generation_{0};
  uint32_t wifi_started_{0};
  bool queued_{false};
  bool processing_{false};
  const char* job_state_{"IDLE"};
  std::atomic<bool> portal_active_{false};
};

extern OpenQuattCaptivePortalRouter* global_captive_portal_router;
bool portal_routes_active();
void set_portal_routes_active(bool active);

}  // namespace openquatt_captive_portal_router
}  // namespace esphome
