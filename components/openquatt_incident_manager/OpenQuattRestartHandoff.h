#pragma once

#include <cstdint>

#include "esphome/core/component.h"
#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif

namespace esphome::openquatt_incident_manager {

class OpenQuattIncidentManager;

// Must run before ESPHome safe-mode and all component setup. A false return means
// the caller must restart immediately: an existing record could not be consumed.
bool initialize_restart_handoff(uint32_t minimum_off_ms);
bool restart_handoff_storage_ready();
uint32_t restored_off_credit_ms(uint8_t hp_index);
bool arm_restart_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms);
bool arm_completed_ota_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms);

class OpenQuattOtaHandoff : public Component
#ifdef USE_OTA_STATE_LISTENER
    ,
                            public ota::OTAGlobalStateListener
#endif
{
 public:
  void set_incident_manager(OpenQuattIncidentManager* value) { this->incident_manager_ = value; }
  void set_minimum_off_ms(uint32_t value) { this->minimum_off_ms_ = value; }

  void setup() override;
#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent* component) override;
#endif

 protected:
  uint32_t full_credit_if_confirmed_(uint8_t hp_index, uint32_t now_ms) const;
  void clear_ota_snapshot_();

  OpenQuattIncidentManager* incident_manager_{nullptr};
  uint32_t minimum_off_ms_{240000U};
  uint32_t ota_hp1_credit_ms_{0U};
  uint32_t ota_hp2_credit_ms_{0U};
  bool ota_handoff_attempted_{false};
};

}  // namespace esphome::openquatt_incident_manager
