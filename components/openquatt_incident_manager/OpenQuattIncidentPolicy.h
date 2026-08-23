#pragma once

#include <cstddef>

#include "includes/incidents/oq_hp_incident_engine.h"

namespace esphome {
namespace openquatt_incident_manager {

inline bool link_round_timeout_elapsed(uint32_t now_ms, uint32_t last_round_ms, uint32_t timeout_ms) {
  return static_cast<uint32_t>(now_ms - last_round_ms) >= timeout_ms;
}

inline bool feedback_generation_is_newer(uint32_t generation, uint32_t baseline) {
  return static_cast<int32_t>(generation - baseline) > 0;
}

inline bool post_command_feedback_complete(uint32_t mode_generation, uint32_t mode_baseline,
                                           uint32_t frequency_generation, uint32_t frequency_baseline) {
  return feedback_generation_is_newer(mode_generation, mode_baseline) &&
         feedback_generation_is_newer(frequency_generation, frequency_baseline);
}

inline bool run_observation_is_fresh(bool waiting_for_start, bool waiting_for_stop,
                                     bool stop_confirmation_requires_command, bool stop_feedback_armed,
                                     bool post_command_feedback) {
  if (waiting_for_start) return post_command_feedback;
  if (waiting_for_stop && (stop_confirmation_requires_command || stop_feedback_armed)) {
    return stop_feedback_armed && post_command_feedback;
  }
  return true;
}

enum class HpThermalCommand : uint8_t {
  UNKNOWN = 0,
  HEATING = 1,
  COOLING = 2,
};

inline HpThermalCommand thermal_command_for_expected_mode(uint8_t expected_mode) {
  // Current ODU mode 1 and legacy mode 3 both represent cooling.
  if (expected_mode == 1U || expected_mode == 3U) {
    return HpThermalCommand::COOLING;
  }
  if (expected_mode == 2U) {
    return HpThermalCommand::HEATING;
  }
  return HpThermalCommand::UNKNOWN;
}

inline bool should_emit_start_confirmation(bool start_feedback_armed, bool previous_running_confirmed,
                                           bool current_running_confirmed) {
  return start_feedback_armed && !previous_running_confirmed && current_running_confirmed;
}

inline bool should_emit_stop_confirmation(bool stop_feedback_armed, bool previous_stop_confirmed,
                                          bool current_stop_confirmed) {
  return stop_feedback_armed && !previous_stop_confirmed && current_stop_confirmed;
}

inline bool should_emit_operator_stop_confirmation(bool stop_confirmation_edge,
                                                   bool run_seen_since_last_confirmed_stop) {
  return stop_confirmation_edge && run_seen_since_last_confirmed_stop;
}

inline bool availability_baseline_ready(oq_incidents::LinkState link_state, bool persistence_initialization_pending) {
  if (persistence_initialization_pending) return false;
  return link_state == oq_incidents::LinkState::HEALTHY || link_state == oq_incidents::LinkState::LOST;
}

inline oq_incidents::StartFailureResetResult perform_start_failure_retry(oq_incidents::HpIncidentEngine* engine,
                                                                         uint32_t now_ms) {
  if (engine == nullptr) {
    return oq_incidents::StartFailureResetResult::HP_NOT_CONFIGURED;
  }
  const oq_incidents::StartFailureResetResult status = engine->start_failure_reset_status();
  if (status != oq_incidents::StartFailureResetResult::READY) {
    return status;
  }
  return engine->clear_start_failure(now_ms) ? oq_incidents::StartFailureResetResult::CLEARED
                                             : engine->start_failure_reset_status();
}

inline bool all_unavailable_hps_allow_fallback(const oq_incidents::DerivedOutputs* outputs, size_t configured_count) {
  if (outputs == nullptr || configured_count == 0U) {
    return false;
  }
  for (size_t index = 0U; index < configured_count; ++index) {
    if (outputs[index].available_for_start || !outputs[index].fallback_cause_present ||
        oq_incidents::has_effect(outputs[index].active_effects, oq_incidents::IncidentEffect::BLOCK_BOILER)) {
      return false;
    }
  }
  return true;
}

inline bool all_hp_outputs_safe_for_fallback(const oq_incidents::DerivedOutputs* outputs, size_t configured_count) {
  if (outputs == nullptr || configured_count == 0U) {
    return false;
  }
  for (size_t index = 0U; index < configured_count; ++index) {
    if (!outputs[index].stop_confirmed || outputs[index].stop_unconfirmed) {
      return false;
    }
  }
  return true;
}

inline oq_incidents::DerivedOutputs incident_storage_failure_outputs() {
  oq_incidents::DerivedOutputs outputs{};
  outputs.protection_state = oq_incidents::ProtectionState::FAULT_ACTIVE;
  outputs.active_effects = oq_incidents::IncidentEffect::DISPLAY | oq_incidents::IncidentEffect::BLOCK_START |
                           oq_incidents::IncidentEffect::STOP_COMPRESSOR |
                           oq_incidents::IncidentEffect::MARK_HP_UNAVAILABLE |
                           oq_incidents::IncidentEffect::BLOCK_BOILER;
  outputs.must_stop = true;
  outputs.fault_active = true;
  outputs.protection_active = true;
  outputs.stop_unconfirmed = true;
  return outputs;
}

inline oq_incidents::DerivedOutputs apply_persistence_safety_gate(oq_incidents::DerivedOutputs outputs,
                                                                  bool persistence_blocks) {
  if (!persistence_blocks) return outputs;

  outputs.available_for_start = false;
  outputs.fallback_eligible = false;
  outputs.fault_active = true;
  outputs.active_effects |= oq_incidents::IncidentEffect::DISPLAY | oq_incidents::IncidentEffect::BLOCK_START |
                            oq_incidents::IncidentEffect::MARK_HP_UNAVAILABLE |
                            oq_incidents::IncidentEffect::BLOCK_BOILER;
  outputs.active_incident_count = outputs.active_incident_count == UINT8_MAX
                                      ? outputs.active_incident_count
                                      : static_cast<uint8_t>(outputs.active_incident_count + 1U);
  if (outputs.primary_incident_id == oq_incidents::kNoIncident) {
    outputs.primary_incident_id = oq_incidents::kPersistenceFailureIncidentId;
  }
  if (outputs.protection_state == oq_incidents::ProtectionState::CLEAR ||
      outputs.protection_state == oq_incidents::ProtectionState::LIMITED) {
    outputs.protection_state = oq_incidents::ProtectionState::START_BLOCKED;
  }
  return outputs;
}

inline oq_incidents::DerivedOutputs apply_persistence_initialization_gate(oq_incidents::DerivedOutputs outputs,
                                                                          bool initialization_pending) {
  if (!initialization_pending) return outputs;

  outputs.available_for_start = false;
  outputs.fallback_eligible = false;
  outputs.active_effects |= oq_incidents::IncidentEffect::BLOCK_START | oq_incidents::IncidentEffect::BLOCK_BOILER;
  if (outputs.protection_state == oq_incidents::ProtectionState::CLEAR ||
      outputs.protection_state == oq_incidents::ProtectionState::LIMITED) {
    outputs.protection_state = oq_incidents::ProtectionState::START_BLOCKED;
  }
  return outputs;
}

}  // namespace openquatt_incident_manager
}  // namespace esphome
