#pragma once

#include <atomic>
#include <cstdint>

#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_firmware_metadata {

class OpenQuattFirmwareMetadata : public Component, public AsyncWebHandler {
 public:
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* web_auth) { this->web_auth_ = web_auth; }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;
  bool canHandle(AsyncWebServerRequest* request) const override;
  void handleRequest(AsyncWebServerRequest* request) override;

  // The update callback runs on the main loop; HTTP metadata reads run on HTTPD.
  // Only successful manifest publications call this, including unchanged versions.
  void record_manifest_publication() { this->manifest_revision_.fetch_add(1, std::memory_order_relaxed); }

 protected:
  openquatt_web_auth::OpenQuattWebAuth* web_auth_{nullptr};
  std::atomic<uint32_t> manifest_revision_{0};
  // Immutable before listener startup; prevents a revision reset on reboot from
  // confirming an outstanding check from the previous boot.
  char boot_id_[17]{};
};

}  // namespace openquatt_firmware_metadata
}  // namespace esphome
