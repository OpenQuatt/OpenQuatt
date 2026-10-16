#pragma once

#include <cstdint>

#include "esphome/core/component.h"
#include "OpenQuattRestartHandoffPolicy.h"
#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif

namespace esphome::openquatt_incident_manager {

class OpenQuattIncidentManager;

// Must run before ESPHome safe-mode and all component setup. A false return means
// the caller must restart immediately: an existing record could not be consumed.
bool initialize_restart_handoff(uint32_t minimum_off_ms);
bool restart_handoff_storage_ready();
bool boot_resume_eligible(uint8_t hp_index = 0U);
bool boot_resume_image_confirmed();
using OtaResumeGuardReader = uint8_t (*)();
void set_ota_resume_guard_reader(OtaResumeGuardReader reader);
bool ota_cycle_hold(uint8_t hp_index, uint32_t now_ms);
using PowerHouseResumeReader = restart_handoff::PowerHouseResumeState (*)();
void set_power_house_resume_reader(PowerHouseResumeReader reader);
bool take_power_house_resume(restart_handoff::PowerHouseResumeState* state);
uint32_t restored_off_credit_ms(uint8_t hp_index);
bool arm_restart_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms);
bool arm_completed_ota_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms, uint8_t resume_mask = 0U,
                               const restart_handoff::PowerHouseResumeState& power_house = {});

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
  bool cycle_hold(uint8_t hp_index, uint32_t now_ms);
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
  bool ota_active_{false};
  restart_handoff::OtaResumePolicy ota_resume_;
  restart_handoff::PowerHouseResumeState ota_power_house_{};
};

}  // namespace esphome::openquatt_incident_manager
