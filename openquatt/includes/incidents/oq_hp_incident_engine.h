#pragma once

#include "oq_hp_incident_catalog.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace oq_incidents {

class HpIncidentEngine {
 public:
  explicit HpIncidentEngine(const EngineTuning& tuning = EngineTuning{}) : tuning_(tuning) { refresh_derived(); }

  const DerivedOutputs& outputs() const { return outputs_; }

  const IncidentRuntime& incident(uint16_t register_address, uint8_t bit) const {
    if (!valid_fault_location(register_address, bit)) {
      return empty_incident_;
    }
    return incidents_[incident_slot(register_address, bit)];
  }

  const IncidentRuntime& incident(IncidentId id) const {
    if (id == kNoIncident || id > static_cast<IncidentId>(kRawIncidentSlotCount)) {
      return empty_incident_;
    }
    return incidents_[static_cast<size_t>(id - 1U)];
  }

  void observe_link_round(uint32_t now_ms, bool critical_snapshot_complete) {
    advance_time(now_ms);

    if (critical_snapshot_complete) {
      observe_complete_link_round(now_ms);
    } else {
      observe_incomplete_link_round(now_ms);
    }
    refresh_derived();
  }

  void observe_fault_words(const FaultWordsObservation& observation) {
    advance_time(observation.now_ms);
    const bool hard_fault_before = hard_fault_effective();

    for (size_t bank = 0U; bank < kFaultRegisterCount; ++bank) {
      if (!observation.fresh[bank]) {
        continue;
      }
      const uint16_t register_address = static_cast<uint16_t>(kFirstFaultRegister + bank);
      for (uint8_t bit = 0U; bit < kBitsPerFaultRegister; ++bit) {
        const bool raw_active = (observation.words[bank] & (1U << bit)) != 0U;
        update_incident(definition_for(register_address, bit), raw_active, observation.now_ms);
      }
    }

    const bool complete_snapshot = observation.fresh[0U] && observation.fresh[1U] && observation.fresh[2U];
    update_fault_recovery(observation.now_ms, complete_snapshot);
    const bool hard_fault_after = hard_fault_effective();
    if (!hard_fault_before && hard_fault_after) {
      // A complete or partial stop confirmation predating the fault cannot
      // authorize CM4. Re-arm the stop command and its feedback generations.
      invalidate_stop_confirmation_();
    }
    refresh_derived();
  }

  bool request_start(uint32_t now_ms) {
    advance_time(now_ms);
    refresh_derived();
    if (!outputs_.available_for_start) {
      return false;
    }
    if (run_state_ == RunState::RUNNING) {
      return true;
    }
    if (run_state_ != RunState::STOPPED) {
      return false;
    }

    run_state_ = RunState::START_REQUESTED;
    compressor_running_confirmed_ = false;
    stop_requested_ = false;
    stop_request_initialized_ = false;
    stopped_read_streak_ = 0U;
    start_watchdog_elapsed_ms_ = 0U;
    start_watchdog_last_ms_ = now_ms;
    start_watchdog_initialized_ = true;
    start_mode_seen_ = false;
    start_mode_ack_timed_out_ = false;
    start_timed_out_ = false;
    wrong_mode_compressor_active_ = false;
    stop_confirmation_pending_ = false;
    refresh_derived();
    return true;
  }

  bool request_stop(uint32_t now_ms) {
    advance_time(now_ms);
    if (run_state_ == RunState::STOPPED) {
      stop_requested_ = false;
      refresh_derived();
      return false;
    }

    stop_requested_ = true;
    const bool newly_initialized = !stop_request_initialized_;
    if (newly_initialized) {
      stop_requested_at_ms_ = now_ms;
      stop_request_initialized_ = true;
      stopped_read_streak_ = 0U;
      run_state_ = run_state_ == RunState::STOP_UNCONFIRMED ? RunState::STOP_UNCONFIRMED : RunState::STOPPING;
    }
    refresh_derived();
    return newly_initialized;
  }

  void observe_run(const RunObservation& observation) {
    advance_time(observation.now_ms);
    if (!observation.fresh || !observation.compressor_frequency_valid) {
      refresh_derived();
      return;
    }
    if (stop_confirmation_pending_ && !stop_request_initialized_) {
      // Revalidation may only consume observations after the new safe-stop
      // command has established its feedback baseline.
      refresh_derived();
      return;
    }

    if (observation.mode_matches_request) {
      start_mode_seen_ = true;
    }

    const bool compressor_active = observation.compressor_frequency_hz > tuning_.compressor_running_threshold_hz;
    if (compressor_active) {
      stopped_read_streak_ = 0U;
      if (stop_requested_) {
        run_state_ = run_state_ == RunState::STOP_UNCONFIRMED ? RunState::STOP_UNCONFIRMED : RunState::STOPPING;
      } else if ((run_state_ == RunState::START_REQUESTED || run_state_ == RunState::WAIT_MODE ||
                  run_state_ == RunState::WAIT_COMPRESSOR) &&
                 !observation.mode_matches_request) {
        // Frequency proves physical compressor activity, but not a successful
        // start in the requested heating/cooling mode. Keep the start
        // watchdog armed so a persistent wrong mode becomes a latched start
        // failure instead of being accepted or retried indefinitely.
        compressor_running_confirmed_ = false;
        wrong_mode_compressor_active_ = true;
        run_state_ = RunState::WAIT_MODE;
      } else {
        compressor_running_confirmed_ = true;
        wrong_mode_compressor_active_ = false;
        run_state_ = RunState::RUNNING;
        start_watchdog_initialized_ = false;
      }
      refresh_derived();
      return;
    }

    if (run_state_ == RunState::START_REQUESTED || run_state_ == RunState::WAIT_MODE ||
        run_state_ == RunState::WAIT_COMPRESSOR) {
      wrong_mode_compressor_active_ = false;
      run_state_ = observation.mode_matches_request ? RunState::WAIT_COMPRESSOR : RunState::WAIT_MODE;
      refresh_derived();
      return;
    }

    if (!observation.stop_mode_confirmed) {
      stopped_read_streak_ = 0U;
      if (stop_requested_ && run_state_ != RunState::STOP_UNCONFIRMED) {
        run_state_ = RunState::STOPPING;
      }
      refresh_derived();
      return;
    }

    if (run_state_ == RunState::STOPPED && !stop_requested_) {
      // Repeated passive standby telemetry confirms the existing state; it
      // must not reopen the two-read stop confirmation on every poll.
      stopped_read_streak_ = 0U;
      refresh_derived();
      return;
    }

    stopped_read_streak_ = saturating_increment(stopped_read_streak_);
    if (stopped_read_streak_ >= std::max<uint8_t>(1U, tuning_.stop_confirm_reads)) {
      run_state_ = RunState::STOPPED;
      compressor_running_confirmed_ = false;
      stop_requested_ = false;
      stop_request_initialized_ = false;
      stop_confirmation_pending_ = false;
      stopped_read_streak_ = 0U;
      wrong_mode_compressor_active_ = false;
    } else {
      // A real stop-confirmation timeout remains active until the complete
      // two-read confirmation succeeds. A single stopped observation must not
      // prematurely clear the fault incident.
      run_state_ = run_state_ == RunState::STOP_UNCONFIRMED ? RunState::STOP_UNCONFIRMED : RunState::STOPPING;
    }
    refresh_derived();
  }

  void tick(uint32_t now_ms) {
    advance_time(now_ms);
    refresh_derived();
  }

  StartFailureResetResult start_failure_reset_status() const {
    if (!start_timed_out_) {
      return StartFailureResetResult::NO_START_FAILURE;
    }
    if (run_state_ != RunState::STOPPED) {
      return StartFailureResetResult::STOP_NOT_CONFIRMED;
    }
    if (link_state_ != LinkState::HEALTHY) {
      return StartFailureResetResult::LINK_NOT_HEALTHY;
    }
    if (hard_fault_effective()) {
      return StartFailureResetResult::HARD_FAULT_ACTIVE;
    }
    if (fault_recovery_active_) {
      return StartFailureResetResult::FAULT_RECOVERY_PENDING;
    }
    return StartFailureResetResult::READY;
  }

  bool clear_start_failure(uint32_t now_ms) {
    advance_time(now_ms);
    if (start_failure_reset_status() != StartFailureResetResult::READY) {
      refresh_derived();
      return false;
    }
    start_watchdog_initialized_ = false;
    start_watchdog_elapsed_ms_ = 0U;
    start_mode_seen_ = false;
    start_mode_ack_timed_out_ = false;
    start_timed_out_ = false;
    wrong_mode_compressor_active_ = false;
    refresh_derived();
    return true;
  }

  bool acknowledge(IncidentId id) {
    if (id == kNoIncident || id > static_cast<IncidentId>(kRawIncidentSlotCount)) {
      return false;
    }
    IncidentRuntime& runtime = incidents_[static_cast<size_t>(id - 1U)];
    if (!runtime.confirmed_active && !runtime.latched) {
      return false;
    }
    const IncidentDefinition definition = definition_for_id(id);
    // A power-cycle latch is a safety gate, not an acknowledgeable history
    // item. Keeping acknowledged=false prevents a still-controlling latch
    // from disappearing from presentation before explicit confirmation.
    if (definition.clear_policy == ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE) {
      return false;
    }
    runtime.acknowledged = true;
    if (!runtime.confirmed_active && definition.clear_policy == ClearPolicy::AFTER_STABLE_READS) {
      runtime.latched = false;
    }
    refresh_derived();
    return true;
  }

  bool has_cleared_power_cycle_latch() const {
    for (size_t slot = 0U; slot < incidents_.size(); ++slot) {
      const IncidentDefinition definition = definition_for(register_for_slot(slot), bit_for_slot(slot));
      const IncidentRuntime& runtime = incidents_[slot];
      if (definition.clear_policy == ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE && !runtime.confirmed_active &&
          !runtime.raw_active && runtime.latched) {
        return true;
      }
    }
    return false;
  }

  bool has_power_cycle_latch() const {
    for (size_t slot = 0U; slot < incidents_.size(); ++slot) {
      const IncidentDefinition definition = definition_for(register_for_slot(slot), bit_for_slot(slot));
      if (definition.clear_policy == ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE && incidents_[slot].latched) {
        return true;
      }
    }
    return false;
  }

  bool restore_power_cycle_latch(IncidentId id) {
    if (id == kNoIncident || id > static_cast<IncidentId>(kRawIncidentSlotCount)) {
      return false;
    }
    const IncidentDefinition definition = definition_for_id(id);
    if (definition.clear_policy != ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE) {
      return false;
    }
    IncidentRuntime& runtime = incidents_[static_cast<size_t>(id - 1U)];
    runtime.latched = true;
    runtime.acknowledged = false;
    refresh_derived();
    return true;
  }

  // This must only be called after the ODU power cycle has been independently
  // confirmed. A controller restart or a cleared/stale Modbus word is not
  // sufficient. Active or raw fault bits are deliberately never released.
  bool confirm_odu_power_cycle(uint32_t now_ms) {
    advance_time(now_ms);
    bool released_latch = false;
    bool released_hard_latch = false;
    for (size_t slot = 0U; slot < incidents_.size(); ++slot) {
      const IncidentDefinition definition = definition_for(register_for_slot(slot), bit_for_slot(slot));
      IncidentRuntime& runtime = incidents_[slot];
      if (definition.clear_policy != ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE || runtime.confirmed_active ||
          runtime.raw_active || !runtime.latched) {
        continue;
      }
      runtime.latched = false;
      // Treat the independently confirmed reset as the auditable manual
      // acknowledgement edge. The incident manager emits that transition
      // with FLAG_MANUAL_RESET_REQUIRED.
      runtime.acknowledged = true;
      released_latch = true;
      released_hard_latch = released_hard_latch || has_effect(definition.effects, IncidentEffect::STOP_COMPRESSOR);
    }
    if (released_hard_latch) {
      hard_fault_seen_ = true;
      fault_recovery_active_ = true;
      fault_recovery_started_ms_ = now_ms;
      fault_recovery_rounds_ = 0U;
    }
    refresh_derived();
    return released_latch;
  }

 private:
  static uint32_t elapsed_ms(uint32_t now_ms, uint32_t since_ms) { return static_cast<uint32_t>(now_ms - since_ms); }

  static bool elapsed_at_least(uint32_t now_ms, uint32_t since_ms, uint32_t duration_ms) {
    return elapsed_ms(now_ms, since_ms) >= duration_ms;
  }

  static uint8_t saturating_increment(uint8_t value) {
    return value == std::numeric_limits<uint8_t>::max() ? value : static_cast<uint8_t>(value + 1U);
  }

  static uint32_t saturating_add(uint32_t lhs, uint32_t rhs) {
    return rhs > std::numeric_limits<uint32_t>::max() - lhs ? std::numeric_limits<uint32_t>::max() : lhs + rhs;
  }

  bool incident_effective(size_t slot, const IncidentDefinition& definition) const {
    const IncidentRuntime& runtime = incidents_[slot];
    return runtime.confirmed_active ||
           (runtime.latched && has_effect(definition.effects, IncidentEffect::REQUIRE_CONFIRMED_ODU_POWER_CYCLE));
  }

  bool preheat_active() const {
    const size_t slot = incident_slot(2119U, 6U);
    return incident_effective(slot, definition_for(2119U, 6U));
  }

  bool hard_fault_effective() const {
    for (size_t slot = 0U; slot < incidents_.size(); ++slot) {
      const IncidentDefinition definition = definition_for(register_for_slot(slot), bit_for_slot(slot));
      if (incident_effective(slot, definition) && has_effect(definition.effects, IncidentEffect::STOP_COMPRESSOR)) {
        return true;
      }
    }
    return false;
  }

  void advance_time(uint32_t now_ms) {
    if (start_watchdog_initialized_) {
      const uint32_t delta_ms = elapsed_ms(now_ms, start_watchdog_last_ms_);
      start_watchdog_last_ms_ = now_ms;
      const bool waiting_for_start = run_state_ == RunState::START_REQUESTED || run_state_ == RunState::WAIT_MODE ||
                                     run_state_ == RunState::WAIT_COMPRESSOR;
      if (waiting_for_start && !preheat_active()) {
        start_watchdog_elapsed_ms_ = saturating_add(start_watchdog_elapsed_ms_, delta_ms);
        if (!start_mode_seen_ && start_watchdog_elapsed_ms_ >= tuning_.mode_ack_timeout_ms) {
          start_mode_ack_timed_out_ = true;
          if (wrong_mode_compressor_active_) {
            start_timed_out_ = true;
          }
        }
        if (start_watchdog_elapsed_ms_ >= tuning_.start_timeout_ms) {
          start_timed_out_ = true;
        }
      }
    }

    if (stop_request_initialized_ && run_state_ != RunState::STOPPED &&
        elapsed_at_least(now_ms, stop_requested_at_ms_, tuning_.stop_confirm_timeout_ms)) {
      run_state_ = RunState::STOP_UNCONFIRMED;
      compressor_running_confirmed_ = false;
      stop_confirmation_pending_ = false;
    }
  }

  void observe_complete_link_round(uint32_t now_ms) {
    missed_link_rounds_ = 0U;
    link_suspect_started_ = false;

    if (link_state_ == LinkState::HEALTHY) {
      return;
    }
    if (link_state_ == LinkState::SUSPECT) {
      if (ever_healthy_) {
        link_state_ = LinkState::HEALTHY;
        return;
      }
      link_state_ = LinkState::RECOVERING;
      recovering_from_loss_ = false;
      link_recovery_started_ms_ = now_ms;
      link_recovery_rounds_ = 1U;
    }
    if (link_state_ == LinkState::BOOTSTRAP || link_state_ == LinkState::LOST) {
      recovering_from_loss_ = link_state_ == LinkState::LOST;
      link_state_ = LinkState::RECOVERING;
      link_recovery_started_ms_ = now_ms;
      link_recovery_rounds_ = 1U;
    } else if (link_state_ == LinkState::RECOVERING) {
      link_recovery_rounds_ = saturating_increment(link_recovery_rounds_);
    }

    if (link_recovery_rounds_ >= std::max<uint8_t>(1U, tuning_.link_recovery_rounds) &&
        elapsed_at_least(now_ms, link_recovery_started_ms_, tuning_.link_recovery_ms)) {
      link_state_ = LinkState::HEALTHY;
      link_recovery_rounds_ = 0U;
      recovering_from_loss_ = false;
      ever_healthy_ = true;
    }
  }

  void observe_incomplete_link_round(uint32_t now_ms) {
    if (link_state_ == LinkState::LOST) {
      return;
    }
    if (link_state_ == LinkState::RECOVERING && recovering_from_loss_) {
      mark_link_lost();
      return;
    }

    if (link_state_ != LinkState::SUSPECT) {
      link_state_ = LinkState::SUSPECT;
      link_suspect_started_ = true;
      link_suspect_since_ms_ = now_ms;
      missed_link_rounds_ = 1U;
    } else {
      missed_link_rounds_ = saturating_increment(missed_link_rounds_);
    }

    if (link_suspect_started_ && missed_link_rounds_ >= std::max<uint8_t>(1U, tuning_.link_lost_rounds) &&
        elapsed_at_least(now_ms, link_suspect_since_ms_, tuning_.link_lost_ms)) {
      mark_link_lost();
    }
  }

  void mark_link_lost() {
    link_state_ = LinkState::LOST;
    link_recovery_rounds_ = 0U;
    recovering_from_loss_ = true;
    // A stop observation made before link loss is stale by definition. Always
    // re-arm the command so the manager records new feedback baselines.
    invalidate_stop_confirmation_();
  }

  void invalidate_stop_confirmation_() {
    // A hard fault or confirmed link loss makes the old stop observation
    // stale. This is a neutral revalidation phase; only the stop timeout may
    // promote it to STOP_UNCONFIRMED and incident 1003.
    if (run_state_ != RunState::STOP_UNCONFIRMED) {
      run_state_ = RunState::STOPPING;
      stop_confirmation_pending_ = true;
    }
    compressor_running_confirmed_ = false;
    stop_requested_ = false;
    stop_request_initialized_ = false;
    stopped_read_streak_ = 0U;
  }

  void update_incident(const IncidentDefinition& definition, bool raw_active, uint32_t now_ms) {
    if (!valid_fault_location(definition.register_address, definition.bit)) {
      return;
    }
    IncidentRuntime& runtime = incidents_[incident_slot(definition.register_address, definition.bit)];

    if (raw_active) {
      if (!runtime.raw_active && !runtime.confirmed_active) {
        runtime.first_seen_ms = now_ms;
        runtime.cleared_at_ms = 0U;
        runtime.trip_streak = 0U;
      }
      runtime.raw_active = true;
      runtime.last_seen_ms = now_ms;
      runtime.clear_streak = 0U;
      runtime.trip_streak = saturating_increment(runtime.trip_streak);

      if (!runtime.confirmed_active && runtime.trip_streak >= std::max<uint8_t>(1U, definition.trip_reads)) {
        runtime.confirmed_active = true;
        // Informational operating states are useful while present, but must
        // not turn normal ODU behaviour into acknowledgeable alarm history.
        runtime.latched = definition.category != IncidentCategory::STATUS;
        runtime.acknowledged = false;
        runtime.occurrence_count = runtime.occurrence_count == std::numeric_limits<uint32_t>::max()
                                       ? runtime.occurrence_count
                                       : runtime.occurrence_count + 1U;
      }
      return;
    }

    runtime.raw_active = false;
    runtime.trip_streak = 0U;
    if (!runtime.confirmed_active) {
      runtime.clear_streak = 0U;
      return;
    }

    runtime.clear_streak = saturating_increment(runtime.clear_streak);
    if (runtime.clear_streak < std::max<uint8_t>(1U, definition.clear_reads)) {
      return;
    }

    runtime.confirmed_active = false;
    runtime.clear_streak = 0U;
    runtime.cleared_at_ms = now_ms;
    if (definition.clear_policy == ClearPolicy::AFTER_STABLE_READS) {
      // Automatic incidents keep a non-controlling history latch until the
      // user acknowledges them. Acknowledgement while active only prevents
      // that latch from remaining after the physical condition clears.
      runtime.latched = definition.category != IncidentCategory::STATUS && !runtime.acknowledged;
    }
  }

  void update_fault_recovery(uint32_t now_ms, bool complete_snapshot) {
    const bool hard_fault_now = hard_fault_effective();
    if (hard_fault_now) {
      hard_fault_seen_ = true;
      fault_recovery_active_ = false;
      fault_recovery_rounds_ = 0U;
      return;
    }
    if (!hard_fault_seen_) {
      return;
    }
    if (!fault_recovery_active_) {
      fault_recovery_active_ = true;
      fault_recovery_started_ms_ = now_ms;
      fault_recovery_rounds_ = complete_snapshot ? 1U : 0U;
    } else if (complete_snapshot) {
      fault_recovery_rounds_ = saturating_increment(fault_recovery_rounds_);
    }

    if (complete_snapshot && fault_recovery_rounds_ >= std::max<uint8_t>(1U, tuning_.fault_recovery_rounds) &&
        elapsed_at_least(now_ms, fault_recovery_started_ms_, tuning_.fault_recovery_ms)) {
      fault_recovery_active_ = false;
      fault_recovery_rounds_ = 0U;
      hard_fault_seen_ = false;
    }
  }

  static int primary_priority(const IncidentDefinition& definition) {
    if (has_effect(definition.effects, IncidentEffect::STOP_COMPRESSOR)) {
      return 5;
    }
    if (has_effect(definition.effects, IncidentEffect::BLOCK_START)) {
      // Preheat is a specific benign start block. Any other classified or
      // unclassified start block must win the presentation/availability
      // reason when both are active.
      return definition.id == incident_id(2119U, 6U) ? 3 : 4;
    }
    if (has_effect(definition.effects, IncidentEffect::LIMIT_CAPACITY)) {
      return 2;
    }
    return 1;
  }

  void refresh_derived() {
    outputs_ = {};
    outputs_.link_state = link_state_;
    outputs_.run_state = run_state_;
    outputs_.running_confirmed = compressor_running_confirmed_;
    outputs_.stop_confirmed = run_state_ == RunState::STOPPED;
    outputs_.stop_confirmation_pending = stop_confirmation_pending_;
    outputs_.stop_unconfirmed = run_state_ == RunState::STOP_UNCONFIRMED;
    outputs_.start_mode_ack_timed_out = start_mode_ack_timed_out_;
    outputs_.start_timed_out = start_timed_out_;

    bool hard_fault = false;
    bool block_start = false;
    bool limited = false;
    bool fallback_fault = false;
    int selected_priority = 0;

    for (size_t slot = 0U; slot < incidents_.size(); ++slot) {
      const IncidentDefinition definition = definition_for(register_for_slot(slot), bit_for_slot(slot));
      if (!incident_effective(slot, definition)) {
        continue;
      }

      outputs_.active_effects |= definition.effects;
      outputs_.active_incident_count = saturating_increment(outputs_.active_incident_count);
      outputs_.fault_active = outputs_.fault_active || definition.category == IncidentCategory::FAULT;
      outputs_.protection_active = outputs_.protection_active || definition.category == IncidentCategory::PROTECTION;
      hard_fault = hard_fault || has_effect(definition.effects, IncidentEffect::STOP_COMPRESSOR);
      block_start = block_start || has_effect(definition.effects, IncidentEffect::BLOCK_START);
      limited = limited || has_effect(definition.effects, IncidentEffect::LIMIT_CAPACITY);
      fallback_fault = fallback_fault || (definition.fallback_policy == FallbackPolicy::AFTER_SYSTEM_GUARDS &&
                                          has_effect(definition.effects, IncidentEffect::ALLOW_CM4));

      const int priority = primary_priority(definition);
      if (priority > selected_priority ||
          (priority == selected_priority &&
           (outputs_.primary_incident_id == kNoIncident || definition.id < outputs_.primary_incident_id))) {
        selected_priority = priority;
        outputs_.primary_incident_id = definition.id;
      }
    }

    if (hard_fault || start_timed_out_) {
      outputs_.protection_state = ProtectionState::FAULT_ACTIVE;
    } else if (fault_recovery_active_) {
      outputs_.protection_state = ProtectionState::FAULT_RECOVERY;
    } else if (block_start) {
      outputs_.protection_state = ProtectionState::START_BLOCKED;
    } else if (limited) {
      outputs_.protection_state = ProtectionState::LIMITED;
    } else {
      outputs_.protection_state = ProtectionState::CLEAR;
    }

    const bool link_recovery_after_loss = link_state_ == LinkState::RECOVERING && recovering_from_loss_;
    outputs_.must_stop = hard_fault || start_timed_out_ || outputs_.stop_confirmation_pending ||
                         outputs_.stop_unconfirmed || link_state_ == LinkState::LOST || link_recovery_after_loss;
    outputs_.available_for_start = link_state_ == LinkState::HEALTHY &&
                                   (outputs_.protection_state == ProtectionState::CLEAR ||
                                    outputs_.protection_state == ProtectionState::LIMITED) &&
                                   !outputs_.stop_confirmation_pending && run_state_ != RunState::STOP_UNCONFIRMED &&
                                   !start_timed_out_;
    outputs_.fallback_cause_present = fallback_fault || fault_recovery_active_ || start_timed_out_ ||
                                      link_state_ == LinkState::LOST || link_recovery_after_loss;
    outputs_.fallback_eligible = outputs_.fallback_cause_present && !outputs_.available_for_start &&
                                 outputs_.stop_confirmed && !outputs_.stop_unconfirmed;

    if (start_timed_out_) {
      outputs_.fault_active = true;
      outputs_.active_incident_count = saturating_increment(outputs_.active_incident_count);
      if (outputs_.primary_incident_id == kNoIncident) {
        outputs_.primary_incident_id = kStartFailedIncidentId;
      }
    }
    if ((link_state_ == LinkState::LOST || link_recovery_after_loss) && outputs_.primary_incident_id == kNoIncident) {
      outputs_.primary_incident_id = kLinkLossIncidentId;
    }
    if (outputs_.stop_unconfirmed && outputs_.primary_incident_id == kNoIncident) {
      outputs_.primary_incident_id = kStopUnconfirmedIncidentId;
    }
  }

  EngineTuning tuning_;
  std::array<IncidentRuntime, kRawIncidentSlotCount> incidents_{};
  IncidentRuntime empty_incident_{};
  DerivedOutputs outputs_{};

  LinkState link_state_ = LinkState::BOOTSTRAP;
  bool link_suspect_started_ = false;
  uint32_t link_suspect_since_ms_ = 0U;
  uint8_t missed_link_rounds_ = 0U;
  uint32_t link_recovery_started_ms_ = 0U;
  uint8_t link_recovery_rounds_ = 0U;
  bool recovering_from_loss_ = false;
  bool ever_healthy_ = false;

  bool hard_fault_seen_ = false;
  bool fault_recovery_active_ = false;
  uint32_t fault_recovery_started_ms_ = 0U;
  uint8_t fault_recovery_rounds_ = 0U;

  RunState run_state_ = RunState::UNKNOWN;
  bool compressor_running_confirmed_ = false;
  bool stop_requested_ = false;
  bool stop_request_initialized_ = false;
  bool stop_confirmation_pending_ = false;
  uint32_t stop_requested_at_ms_ = 0U;
  uint8_t stopped_read_streak_ = 0U;

  bool start_watchdog_initialized_ = false;
  uint32_t start_watchdog_last_ms_ = 0U;
  uint32_t start_watchdog_elapsed_ms_ = 0U;
  bool start_mode_seen_ = false;
  bool start_mode_ack_timed_out_ = false;
  bool start_timed_out_ = false;
  bool wrong_mode_compressor_active_ = false;
};

}  // namespace oq_incidents
