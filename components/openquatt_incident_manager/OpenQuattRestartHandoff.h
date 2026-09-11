#pragma once

#include <array>
#include <cstdint>

#include "esphome/core/component.h"
#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif
#include "OpenQuattRestartHandoffPolicy.h"

namespace esphome::openquatt_incident_manager {

class OpenQuattIncidentManager;

// Must run before ESPHome safe-mode and all component setup. A false return means
// the caller must restart immediately: an existing record could not be consumed.
bool initialize_restart_handoff(uint32_t minimum_off_ms);
bool restart_handoff_storage_ready();
uint32_t restored_off_credit_ms(uint8_t hp_index);
bool arm_restart_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms);
bool arm_ota_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms);
bool clear_restart_handoff();

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
  void loop() override;
#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent* component) override;
#endif

 protected:
  static constexpr uint32_t SAMPLE_INTERVAL_MS = 1000U;
  static constexpr uint32_t FULL_CREDIT_STABLE_MS = 15000U;

  bool hp_eligible_for_full_credit_(uint8_t hp_index, uint32_t now_ms) const;
  void sample_(uint32_t now_ms);

  OpenQuattIncidentManager* incident_manager_{nullptr};
  uint32_t minimum_off_ms_{240000U};
  uint32_t last_sample_ms_{0U};
  std::array<restart_handoff::StableFullCreditLatch, 2U> full_credit_latches_{};
  bool ota_handoff_attempted_{false};
  bool ota_handoff_saved_{false};
};

}  // namespace esphome::openquatt_incident_manager
