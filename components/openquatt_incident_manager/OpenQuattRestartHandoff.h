#pragma once

#include <cstdint>

namespace esphome::openquatt_incident_manager {

// Must run before ESPHome safe-mode and all component setup. A false return means
// the caller must restart immediately: an existing record could not be consumed.
bool initialize_restart_handoff(uint32_t minimum_off_ms);
bool restart_handoff_storage_ready();
uint32_t restored_off_credit_ms(uint8_t hp_index);
bool arm_restart_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms);

}  // namespace esphome::openquatt_incident_manager
