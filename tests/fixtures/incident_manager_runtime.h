#pragma once

#include "openquatt/includes/incidents/oq_manual_reset_latch_policy.h"

namespace esphome::openquatt_decision_log {
constexpr uint8_t REASON_UNKNOWN = 0U;
}

namespace esphome::openquatt_incident_manager {

// The harness omits diagnostic messages, snapshot locking and ESP32 storage.
// Generation tracking, feedback validity and rest credit use the compiled firmware methods.
struct IncidentRuntime {
  struct UnitState {
    oq_incidents::HpIncidentEngine engine{};
    oq_hp_restart_guard::Policy restart_guard{};
    oq_boot_resume::Policy boot_resume{};
    struct {
    } pump_context;
    bool configured{false}, startup_released{true};
    bool transport_seen{false}, transport_online{false};
    bool runtime_frequency_mapping_valid{true}, runtime_frequency_mapping_observed{true};
    bool restart_credit_pending{false};
    bool working_mode_valid{false}, compressor_frequency_valid{false};
    float working_mode{0.0F}, compressor_frequency_hz{0.0F};
    uint32_t working_mode_generation{0}, compressor_frequency_generation{0};
    uint32_t command_mode_generation{0}, command_frequency_generation{0};
    uint32_t rest_mode_observed_ms{0}, rest_mode_generation{0}, last_link_round_ms{0};
    uint8_t expected_mode{0}, active_command_mode{0}, stop_confirmation_reason{0};
    bool start_feedback_armed{false}, stop_feedback_armed{false};
  };

  std::array<UnitState, 2> units{};
  oq_incidents::ManualResetLatchPersistencePolicy manual_reset_persistence_{};
  uint32_t minimum_off_ms_{0};
  bool restarting_{false};

  static size_t hp_slot_(uint8_t hp) { return hp - 1U; }
  UnitState* unit_(uint8_t hp) { return units[hp_slot_(hp)].configured ? &units[hp_slot_(hp)] : nullptr; }
  const UnitState* unit_(uint8_t hp) const { return units[hp_slot_(hp)].configured ? &units[hp_slot_(hp)] : nullptr; }
  const auto& units_() const { return units; }
  bool storage_ready_() const { return true; }
  bool restart_handoff_storage_ready() const { return true; }
  static uint8_t reason_for_incident_id_(oq_incidents::IncidentId) { return openquatt_decision_log::REASON_UNKNOWN; }
  static uint8_t incident_reason_(const oq_incidents::IncidentDefinition&) {
    return openquatt_decision_log::REASON_UNKNOWN;
  }
  static uint8_t cleared_command_reason(uint8_t) { return openquatt_decision_log::REASON_UNKNOWN; }
  void publish_transitions_(UnitState&, size_t, uint32_t) {}
  void publish_snapshot_(uint32_t) {}

#include "oq_incident_manager_methods.inc"
};

struct TestIncidentManager : FakeIncidentManager {
  IncidentRuntime runtime;
  std::array<unsigned, 2> starts{};
  std::array<uint16_t, 2> simulated_modes{};

  oq_incidents::DerivedOutputs get_outputs(unsigned hp) const {
    return runtime.unit_(hp) ? runtime.outputs_for_slot_(hp - 1U) : FakeIncidentManager::get_outputs(hp);
  }

  uint32_t minimum_off_remaining_ms(unsigned hp, uint32_t now) const {
    return runtime.unit_(hp) ? runtime.minimum_off_remaining_ms(hp, now)
                             : FakeIncidentManager::minimum_off_remaining_ms(hp, now);
  }

  void invalidate_restart_credit(unsigned hp) {
    if (runtime.unit_(hp))
      runtime.invalidate_restart_credit(hp);
    else
      FakeIncidentManager::invalidate_restart_credit(hp);
  }

  bool request_start(unsigned hp, uint8_t mode, uint32_t now) {
    const bool was_stopped = get_outputs(hp).run_state == oq_incidents::RunState::STOPPED;
    if (runtime.unit_(hp)) {
      if (!runtime.request_start(hp, mode, now)) return false;
    } else {
      auto& output = outputs[hp - 1U];
      if (!was_stopped || !output.available_for_start) return false;
      output.run_state = oq_incidents::RunState::START_REQUESTED;
      output.stop_confirmed = false;
    }
    if (was_stopped) ++starts[hp - 1U];
    return true;
  }

  void request_stop(unsigned hp, uint32_t now) {
    if (runtime.unit_(hp)) {
      runtime.request_stop(hp, now);
      return;
    }
    auto& output = outputs[hp - 1U];
    output.run_state = oq_incidents::RunState::STOPPING;
    output.stop_confirmation_pending = true;
  }

  void observe_mode(unsigned hp, uint16_t mode, uint32_t now) {
    if (runtime.unit_(hp))
      runtime.observe_working_mode(hp, mode, now);
    else
      simulated_modes[hp - 1U] = mode;
  }

  void observe_frequency(unsigned hp, float hz, uint32_t now) {
    if (runtime.unit_(hp)) {
      runtime.observe_compressor_frequency(hp, hz, now);
      return;
    }
    const bool running = simulated_modes[hp - 1U] > 0 && hz > 0;
    auto& output = outputs[hp - 1U];
    output.run_state = running ? oq_incidents::RunState::RUNNING : oq_incidents::RunState::STOPPED;
    output.running_confirmed = running;
    output.stop_confirmed = !running;
    output.stop_confirmation_pending = false;
  }
};

}  // namespace esphome::openquatt_incident_manager
