#pragma once

#include <math.h>
#include <stdint.h>

#include <algorithm>
#include <string>

#include "oq_compressor_frequency_runtime.h"
#include "oq_cooling_limiter_logic.h"
#include "oq_incident_actuator_logic.h"
#include "oq_thermal_actuator_logic.h"
#include "oq_thermal_request_logic.h"
#include "../odu/oq_odu_compressor_levels.h"
#include "../service/oq_service_status.h"
#include "../service/tasks/oq_manual_hp_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_thermal_actuator_runtime {

#if OQ_TOPOLOGY_DUO
#define OQ_ACTUATOR_SECONDARY_ID(suffix) id(hp2_##suffix)
#else
#define OQ_ACTUATOR_SECONDARY_ID(suffix) id(hp1_##suffix)
#endif

struct CoolingWindow {
  uint32_t remaining_ms{0};
  bool confirmation_pending{false};
};

struct TickConfig {
  uint32_t now_ms;
  uint32_t dt_ms;
  int minimum_off_s;
  int cooling_minimum_off_min_s;
  float minimum_flow_lph;
};

class Runtime {
  struct Cycle {
    TickConfig config;
    int request_mode_code;
    bool request_thermal_active;
    bool manual_service_active;
    bool restart_by_minimum_off_time;
    const char* request_mode_option;
    oq_frequency_runtime::Context frequency;
    CoolingWindow cooling;
    bool cooling_stop_planned{false};
    bool cooling_stop_armed{false};
    std::string service_guards[2]{"Vrijgegeven", "Vrijgegeven"};
  };

 public:
  void tick(const TickConfig& config) {
    const float cooling_off_state = id(cooling_minimum_off_time).state;
    int cooling_off_s = !isfinite(cooling_off_state) ? 600 : static_cast<int>(lroundf(cooling_off_state));
    const int cooling_off_floor_s = oq_cooling::cooling_minimum_off_floor_s(config.cooling_minimum_off_min_s);
    cooling_off_s = std::max(cooling_off_floor_s, std::min(3600, cooling_off_s));
    const bool restart_by_minimum_off_time =
        id(cooling_restart_mode).has_state() && id(cooling_restart_mode).current_option() == "Minimum off time";
    const int request_mode_code = id(oq_request_mode_code);
    const bool request_thermal_active = request_mode_code == 1 || request_mode_code == 2;
    const bool manual_service_active = oq_manual_hp::owns_control();
    Cycle cycle{
        config,
        request_mode_code,
        request_thermal_active,
        manual_service_active,
        restart_by_minimum_off_time,
        oq_request::request_mode_option(request_mode_code),
        oq_frequency_runtime::capture(),
        this->update_cooling_window(config.now_ms, static_cast<uint32_t>(cooling_off_s) * 1000UL,
                                    restart_by_minimum_off_time),
    };
    this->publish_defrost_events(config.now_ms);
    const bool hp1_was_cooling = this->applied_cooling_[0] || this->applied_mode_is_cooling_(true, request_mode_code);
#if OQ_TOPOLOGY_DUO
    const bool hp2_was_cooling = this->applied_cooling_[1] || this->applied_mode_is_cooling_(false, request_mode_code);
#else
    constexpr bool hp2_was_cooling = false;
#endif
    cycle.cooling_stop_planned = oq_cooling::cooling_stop_is_planned(hp1_was_cooling, id(hp1_last_applied_level),
                                                                     id(oq_cooling_request_hp1_level));
#if OQ_TOPOLOGY_DUO
    cycle.cooling_stop_planned =
        cycle.cooling_stop_planned || oq_cooling::cooling_stop_is_planned(hp2_was_cooling, id(hp2_last_applied_level),
                                                                          id(oq_cooling_request_hp2_level));
#endif
    int hp1_applied = 0;
    int hp2_applied = 0;
#if OQ_TOPOLOGY_DUO
    const bool hp2_first = oq_cooling::apply_hp2_before_hp1_for_cooling_handover(hp1_was_cooling, hp2_was_cooling);
    if (hp2_first) {
      hp2_applied = this->apply_level_(false, id(oq_actuator_hp2_req), hp2_was_cooling, cycle);
      hp1_applied = this->apply_level_(true, id(oq_actuator_hp1_req), hp1_was_cooling, cycle);
    } else {
      hp1_applied = this->apply_level_(true, id(oq_actuator_hp1_req), hp1_was_cooling, cycle);
      hp2_applied = this->apply_level_(false, id(oq_actuator_hp2_req), hp2_was_cooling, cycle);
    }
    this->publish_topology_change_(hp1_applied, hp2_applied, config.now_ms);
#else
    hp1_applied = this->apply_level_(true, id(oq_actuator_hp1_req), hp1_was_cooling, cycle);
#endif
    this->applied_cooling_[0] =
        oq_cooling::next_applied_cooling(hp1_was_cooling, hp1_applied,
                                         this->requested_mode_code(true, hp1_applied > 0, request_mode_code,
                                                                   request_thermal_active, manual_service_active));
#if OQ_TOPOLOGY_DUO
    this->applied_cooling_[1] =
        oq_cooling::next_applied_cooling(hp2_was_cooling, hp2_applied,
                                         this->requested_mode_code(false, hp2_applied > 0, request_mode_code,
                                                                   request_thermal_active, manual_service_active));
#endif
    if (manual_service_active) {
      this->publish_manual_guard(static_cast<int>(roundf(id(oq_manual_hp1_level).state)),
                                 static_cast<int>(roundf(id(oq_manual_hp2_level).state)), hp1_applied, hp2_applied,
                                 cycle.service_guards[0], cycle.service_guards[1], config.now_ms,
                                 static_cast<uint32_t>(std::max(0, config.minimum_off_s)) * 1000UL,
                                 config.minimum_flow_lph);
    }
    this->record_transition(true, hp1_applied, config.now_ms, config.dt_ms);
#if OQ_TOPOLOGY_DUO
    this->record_transition(false, hp2_applied, config.now_ms, config.dt_ms);
#endif
  }

  CoolingWindow update_cooling_window(uint32_t now_ms, uint32_t minimum_off_ms, bool restart_by_minimum_off_time) {
    if (!id(oq_cooling_boot_min_off_elapsed) && now_ms >= minimum_off_ms) {
      id(oq_cooling_boot_min_off_elapsed) = true;
    }
    this->confirm_cooling_stop_(true, now_ms);
#if OQ_TOPOLOGY_DUO
    this->confirm_cooling_stop_(false, now_ms);
#endif

    const bool confirmation_pending = id(oq_cooling_stop_confirmation_pending_hp1)
#if OQ_TOPOLOGY_DUO
                                      || id(oq_cooling_stop_confirmation_pending_hp2)
#endif
        ;
    if (id(oq_cooling_confirmed_stop_seen) &&
        static_cast<uint32_t>(now_ms - id(oq_cooling_last_confirmed_stop_ms)) >= minimum_off_ms) {
      id(oq_cooling_confirmed_stop_seen) = false;
      if (!confirmation_pending) id(oq_cooling_min_off_stop_pending) = false;
    }

    return {
        oq_cooling::global_minimum_off_time_remaining_ms(
            restart_by_minimum_off_time, now_ms, id(oq_cooling_confirmed_stop_seen),
            id(oq_cooling_last_confirmed_stop_ms), id(oq_cooling_boot_min_off_elapsed), minimum_off_ms),
        confirmation_pending,
    };
  }

  void publish_optimizer_reason(const char* reason) {
#if OQ_TOPOLOGY_DUO
    const std::string value(reason);
    if (value == this->last_optimizer_reason_) return;
    id(oq_duo_optimizer_reason).publish_state(value.c_str());
    this->last_optimizer_reason_ = value;
#else
    (void)reason;
#endif
  }

  int selected_level(bool is_hp1) const {
    auto& level = is_hp1 ? id(hp1_compressor_level) : OQ_ACTUATOR_SECONDARY_ID(compressor_level);
    if (!level.has_state()) return -1;
    const auto index = level.active_index();
    return index.has_value() ? static_cast<int>(index.value()) : -1;
  }

  bool mode_is(bool is_hp1, int mode_code) const {
    const float mode = is_hp1 ? id(hp1_working_mode).state : OQ_ACTUATOR_SECONDARY_ID(working_mode).state;
    return oq_request::thermal_mode_matches(mode, mode_code);
  }

  bool mode_target_is(bool is_hp1, int mode_code) const {
    auto& mode = is_hp1 ? id(hp1_set_working_mode) : OQ_ACTUATOR_SECONDARY_ID(set_working_mode);
    return oq_request::target_option_matches_mode(mode.has_state(), mode.current_option(), mode_code);
  }

  bool valve_defrost_active(bool is_hp1) const {
    return is_hp1 ? id(hp1_4_way_valve).state : OQ_ACTUATOR_SECONDARY_ID(4_way_valve).state;
  }

  void publish_defrost_events(uint32_t now_ms) {
    this->publish_defrost_seen_(true, this->real_defrost_seen_(true), now_ms);
#if OQ_TOPOLOGY_DUO
    this->publish_defrost_seen_(false, this->real_defrost_seen_(false), now_ms);
#endif
  }

  oq_odu::RetainedLevel retained_level(bool is_hp1, const oq_frequency_runtime::Context& frequency_runtime) {
    oq_odu::RetainedLevel& retained = this->retained_levels_[is_hp1 ? 0 : 1];
    const int previous_applied = is_hp1 ? id(hp1_last_applied_level) : OQ_ACTUATOR_SECONDARY_ID(last_applied_level);
    const int previous_physical =
        is_hp1 ? id(hp1_last_commanded_physical_level) : OQ_ACTUATOR_SECONDARY_ID(last_commanded_physical_level);
    retained = oq_odu::update_retained_level_snapshot(
        retained, this->valve_defrost_active(is_hp1), this->mode_is(is_hp1, 1) || this->mode_target_is(is_hp1, 1),
        this->selected_level(is_hp1), previous_applied, previous_physical, frequency_runtime.configured_v2,
        frequency_runtime.snapshot(is_hp1));
    return retained;
  }

  void clear_retained_level(bool is_hp1) { this->retained_levels_[is_hp1 ? 0 : 1] = {}; }

  void publish_defrost_hold_start(bool is_hp1, bool active, const oq_odu::RetainedLevel& retained,
                                  int requested_level) {
    bool& previous = this->last_defrost_holds_[is_hp1 ? 0 : 1];
    if (active && !previous) {
      id(oq_decision_log)
          .emit(openquatt_decision_log::EVENT_DECISION_HOLD,
                is_hp1 ? openquatt_decision_log::SUBJECT_HP1 : openquatt_decision_log::SUBJECT_HP2,
                openquatt_decision_log::REASON_DEFROST_HOLD, openquatt_decision_log::SEVERITY_LIMITED,
                static_cast<uint8_t>(id(oq_control_mode_code)), openquatt_decision_log::STATE_ACTIVE,
                openquatt_decision_log::STATE_ACTIVE, static_cast<int16_t>(retained.control_level),
                static_cast<int16_t>(requested_level));
    }
    previous = active;
  }

  bool apply_active_mode_hold(bool is_hp1, const oq_odu::RetainedLevel& retained, const char* fallback_mode_option) {
    if (retained.control_level <= 0 || retained.physical_level <= 0) return false;
    const char* option = oq_request::retained_mode_name(this->mode_is(is_hp1, 1) || this->mode_target_is(is_hp1, 1),
                                                        this->mode_is(is_hp1, 2) || this->mode_target_is(is_hp1, 2),
                                                        fallback_mode_option);
    this->write_mode_option_(is_hp1, option, false);
    this->write_level(is_hp1, retained.physical_level, false);
    return true;
  }

  int requested_mode_code(bool is_hp1, bool active, int request_mode_code, bool request_thermal_active,
                          bool manual_service_active) const {
    int mode_code = active && request_thermal_active ? request_mode_code : 0;
    if (manual_service_active && id(oq_manual_hp_mode_allowed)) {
      const int manual_mode = is_hp1 ? id(oq_manual_hp1_mode_code) : id(oq_manual_hp2_mode_code);
      if (manual_mode > 0) mode_code = manual_mode;
    }
    return mode_code;
  }

  void set_mode(bool is_hp1, bool active, bool force_write, int request_mode_code, bool request_thermal_active,
                bool manual_service_active) {
    const int mode_code =
        this->requested_mode_code(is_hp1, active, request_mode_code, request_thermal_active, manual_service_active);
    this->write_mode_option_(is_hp1, oq_request::request_mode_option(mode_code), force_write);
  }

  void write_level(bool is_hp1, int physical_level, bool force_write) {
    static const char* const level_options[21] = {"0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9", "10",
                                                  "11", "12", "13", "14", "15", "16", "17", "18", "19", "20"};
    if (physical_level < 0 || physical_level > 20) physical_level = 0;
    if (is_hp1) {
      const auto index = id(hp1_compressor_level).active_index();
      const int current = index.has_value() ? static_cast<int>(index.value()) : -1;
      if (current != physical_level || (physical_level == 0 && force_write)) {
        auto call = id(hp1_compressor_level).make_call();
        call.set_option(level_options[physical_level]);
        call.perform();
      }
      id(hp1_last_commanded_physical_level) = physical_level;
      return;
    }
#if OQ_TOPOLOGY_DUO
    const auto index = id(hp2_compressor_level).active_index();
    const int current = index.has_value() ? static_cast<int>(index.value()) : -1;
    if (current != physical_level || (physical_level == 0 && force_write)) {
      auto call = id(hp2_compressor_level).make_call();
      call.set_option(level_options[physical_level]);
      call.perform();
    }
    id(hp2_last_commanded_physical_level) = physical_level;
#else
    (void)physical_level;
    (void)force_write;
#endif
  }

  void log_block_change(bool is_hp1, const std::string& reason) {
    std::string& previous = this->last_block_reasons_[is_hp1 ? 0 : 1];
    if (reason == previous) return;
    const char* hp = is_hp1 ? "HP1" : "HP2";
    if (reason.empty()) {
      ESP_LOGI("quatt.act", "%s compressor block cleared", hp);
    } else {
      ESP_LOGI("quatt.act", "%s compressor block: %s", hp, reason.c_str());
    }
    previous = reason;
  }

  void publish_candidate_blocked(bool is_hp1, uint8_t reason, int requested_level, uint16_t auxiliary_seconds) {
    uint8_t& previous = this->last_candidate_reasons_[is_hp1 ? 0 : 1];
    if (reason == openquatt_decision_log::REASON_UNKNOWN) {
      previous = reason;
      return;
    }
    if (reason == previous) return;
    id(oq_decision_log)
        .emit(openquatt_decision_log::EVENT_CANDIDATE_BLOCKED,
              is_hp1 ? openquatt_decision_log::SUBJECT_HP1 : openquatt_decision_log::SUBJECT_HP2, reason,
              openquatt_decision_log::SEVERITY_LIMITED, static_cast<uint8_t>(id(oq_control_mode_code)),
              openquatt_decision_log::STATE_STANDBY, openquatt_decision_log::STATE_BLOCKED,
              static_cast<int16_t>(requested_level), static_cast<int16_t>(auxiliary_seconds));
    previous = reason;
  }

  uint32_t& last_safe_stop_write_ms(bool is_hp1) { return this->last_safe_stop_write_ms_[is_hp1 ? 0 : 1]; }

  uint8_t transition_reason(bool starting) const {
    if (id(oq_control_mode_code) == 5) {
      return starting ? static_cast<uint8_t>(openquatt_decision_log::REASON_RUNTIME_LEAD) : this->source_stop_reason_();
    }
    return starting ? this->heating_transition_reason_() : this->source_stop_reason_();
  }

  void publish_manual_guard(int hp1_manual_level, int hp2_manual_level, int hp1_applied, int hp2_applied,
                            std::string hp1_guard, std::string hp2_guard, uint32_t now_ms, uint32_t minimum_off_ms,
                            float minimum_flow_lph) {
    const uint32_t startup_remaining_s =
        id(oq_boot_startup_inhibit_active) && id(oq_boot_startup_inhibit_until_ms) > now_ms
            ? (id(oq_boot_startup_inhibit_until_ms) - now_ms + 999UL) / 1000UL
            : 0;
    const bool flow_valid = id(flow_rate_selected).has_state() && !isnan(id(flow_rate_selected).state) &&
                            id(flow_rate_selected).state >= minimum_flow_lph;
    const bool mode_conflict = id(oq_manual_hp1_mode_code) > 0 && id(oq_manual_hp2_mode_code) > 0 &&
                               id(oq_manual_hp1_mode_code) != id(oq_manual_hp2_mode_code);
    const auto guard = [&](int level, int mode_code, uint32_t last_stop_ms, const std::string& current) {
      return oq_thermal_actuator::manual_guard(
          {level, mode_code, now_ms, last_stop_ms, minimum_off_ms, startup_remaining_s, id(oq_manual_hp_stop_requested),
           id(oq_water_temp_hard_trip_active), id(oq_lowflow_fault_active), flow_valid, mode_conflict},
          current);
    };
    hp1_guard = guard(hp1_manual_level, id(oq_manual_hp1_mode_code), id(hp1_last_stop_ms), hp1_guard);
    this->finish_manual_guard_(hp1_manual_level, hp1_applied, hp1_guard);
#if OQ_TOPOLOGY_DUO
    hp2_guard = guard(hp2_manual_level, id(oq_manual_hp2_mode_code), id(hp2_last_stop_ms), hp2_guard);
    this->finish_manual_guard_(hp2_manual_level, hp2_applied, hp2_guard);
#else
    (void)hp2_manual_level;
    (void)hp2_applied;
    (void)hp2_guard;
#endif

    std::string status = "Vrijgegeven";
    if (hp1_guard != "Vrijgegeven") status = "HP1: " + hp1_guard;
#if OQ_TOPOLOGY_DUO
    if (hp2_guard != "Vrijgegeven") {
      if (status != "Vrijgegeven") {
        status += " | ";
      } else {
        status.clear();
      }
      status += "HP2: " + hp2_guard;
    }
#endif
    if (status == this->last_manual_guard_status_) return;
    oq_service_status::set_manual_hp_guard(status);
    this->last_manual_guard_status_ = status;
  }

  void record_transition(bool is_hp1, int new_level, uint32_t now_ms, uint32_t dt_ms) {
    int& previous = is_hp1 ? id(hp1_last_applied_level) : OQ_ACTUATOR_SECONDARY_ID(last_applied_level);
    if (previous != new_level) {
      (is_hp1 ? id(hp1_last_level_change_ms) : id(hp2_last_level_change_ms)) = now_ms;
    }

    if (previous == 0 && new_level > 0) {
      this->set_last_start_ms_(is_hp1, now_ms);
      this->emit_transition_(is_hp1, true, new_level);
    } else if (previous > 0 && new_level == 0) {
      this->set_last_stop_ms_(is_hp1, now_ms);
      this->emit_transition_(is_hp1, false, previous);
    }

    if (new_level > 0 && dt_ms > 0 && this->measured_thermal_mode_(is_hp1)) {
      uint32_t& accumulated_ms = is_hp1 ? id(hp1_runtime_accum_ms) : id(hp2_runtime_accum_ms);
      int& runtime_minutes = is_hp1 ? id(hp1_minutes) : OQ_ACTUATOR_SECONDARY_ID(minutes);
      oq_thermal_actuator::accumulate_runtime(dt_ms, accumulated_ms, runtime_minutes);
    }
    previous = new_level;
  }

 private:
  int apply_level_(bool is_hp1, int requested, bool was_cooling, Cycle& cycle) {
    constexpr int MAX_LEVEL = 10;
    const int maximum = cycle.manual_service_active
                            ? oq_odu::physical_level_limit(cycle.frequency.configured_v2,
                                                           cycle.frequency.snapshot(is_hp1), cycle.request_mode_code)
                            : MAX_LEVEL;
    int level = std::max(0, std::min(maximum, requested));
    requested = level;
    const int previous = this->previous_applied_(is_hp1);
    const uint8_t hp_index = is_hp1 ? 1U : 2U;
    const auto incident = id(oq_incident_manager).get_outputs(hp_index);
    uint32_t& last_safe_write_ms = this->last_safe_stop_write_ms(is_hp1);
    const bool stop_pending = incident.run_state == oq_incidents::RunState::STOPPING ||
                              incident.run_state == oq_incidents::RunState::STOP_UNCONFIRMED;
    const bool force_safe_write =
        oq_incident_actuator::safe_stop_write_retry_due(stop_pending, cycle.config.now_ms, last_safe_write_ms, 10000U);

    // Contract order: incident stop -> defrost hold -> cooling rest -> per-HP rest
    // -> valid mode -> frequency policy -> start/stop registration -> physical write.
    const auto incident_guard =
        oq_incident_actuator::decide({level, previous, incident.available_for_start, incident.must_stop});
    if (incident_guard.bypass_runtime_and_defrost_holds) this->clear_retained_level(is_hp1);
    const auto retained = incident_guard.bypass_runtime_and_defrost_holds
                              ? oq_odu::RetainedLevel{}
                              : this->retained_level(is_hp1, cycle.frequency);
    const int expected_mode = this->requested_mode_code(is_hp1, true, cycle.request_mode_code,
                                                        cycle.request_thermal_active, cycle.manual_service_active);
    const bool cooling_start_blocked = oq_cooling::cooling_stop_confirmation_blocks_start(
                                           cycle.cooling.confirmation_pending, cycle.cooling_stop_armed, previous) ||
                                       (!cycle.manual_service_active && cycle.request_mode_code == 1 &&
                                        oq_cooling::global_minimum_off_time_blocks_start(
                                            cycle.cooling.remaining_ms, false,
                                            cycle.restart_by_minimum_off_time && cycle.cooling_stop_planned, previous));
    const uint32_t hp_rest_remaining_ms = oq_thermal_actuator::minimum_off_remaining_ms(
        cycle.config.now_ms, this->last_stop_ms_(is_hp1),
        static_cast<uint32_t>(std::max(0, cycle.config.minimum_off_s)) * 1000UL);
    const bool defrost_hold = oq_odu::retained_level_should_override_request(retained, incident_guard.guarded_level,
                                                                             cycle.manual_service_active);
    const auto preflight = oq_thermal_actuator::decide_preflight(
        incident_guard.guarded_level, previous, defrost_hold, expected_mode, hp_rest_remaining_ms,
        incident_guard.bypass_runtime_and_defrost_holds, cooling_start_blocked);
    level = preflight == oq_thermal_actuator::PreflightBlock::NONE ? incident_guard.guarded_level : 0;
    std::string block_reason;
    uint8_t candidate_reason = openquatt_decision_log::REASON_UNKNOWN;
    uint16_t candidate_aux_s = 0;
    bool register_incident_stop = false;
    if (incident_guard.action == oq_incident_actuator::Action::FORCE_STOP) {
      block_reason = "incident manager requires compressor stop";
      candidate_reason = openquatt_decision_log::REASON_HP_FAULT;
      this->service_guard_(cycle, is_hp1) = "incidentbewaking vereist compressorstop";
      register_incident_stop = incident.run_state != oq_incidents::RunState::STOPPED &&
                               incident.run_state != oq_incidents::RunState::STOPPING &&
                               incident.run_state != oq_incidents::RunState::STOP_UNCONFIRMED;
    } else if (incident_guard.action == oq_incident_actuator::Action::BLOCK_NEW_START) {
      block_reason = "incident manager blocks new compressor start";
      candidate_reason = openquatt_decision_log::REASON_CANDIDATE_UNAVAILABLE;
      this->service_guard_(cycle, is_hp1) = "incidentbewaking blokkeert een nieuwe start";
    }

    this->publish_defrost_hold_start(is_hp1, defrost_hold, retained, requested);
    if (defrost_hold) {
      char reason[96];
      snprintf(reason, sizeof(reason), "defrost hold at model level %d / physical F%d", retained.control_level,
               retained.physical_level);
      this->service_guard_(cycle, is_hp1) = "ontdooihold op modelstand " + std::to_string(retained.control_level);
      this->log_block_change(is_hp1, reason);
      this->apply_active_mode_hold(is_hp1, retained, cycle.request_mode_option);
      this->publish_optimizer_reason("defrost_protect_hold");
      return retained.control_level;
    }

    if (preflight == oq_thermal_actuator::PreflightBlock::COOLING_REST) {
      candidate_reason = openquatt_decision_log::REASON_CANDIDATE_IN_REST;
      if (cycle.cooling.remaining_ms > 0) {
        const uint32_t remaining_s = (cycle.cooling.remaining_ms + 999UL) / 1000UL;
        block_reason = "cooling minimum off-time remaining " + std::to_string(remaining_s) + " s";
        candidate_aux_s = this->cap_seconds_(remaining_s);
        this->service_guard_(cycle, is_hp1) = "minimale koel-uit-tijd: nog " + std::to_string(remaining_s) + " s";
      } else {
        block_reason = "waiting for confirmed cooling stop";
        this->service_guard_(cycle, is_hp1) = "wachten op bevestigde koelstop";
      }
      this->log_block_change(is_hp1, block_reason);
    }

    if (preflight == oq_thermal_actuator::PreflightBlock::HP_REST) {
      const uint32_t remaining_s = (hp_rest_remaining_ms + 999UL) / 1000UL;
      char reason[144];
      snprintf(reason, sizeof(reason), "minimum off-time remaining %u s after stop (requested %d)",
               static_cast<unsigned int>(remaining_s), requested);
      block_reason = reason;
      candidate_reason = openquatt_decision_log::REASON_CANDIDATE_IN_REST;
      candidate_aux_s = this->cap_seconds_(remaining_s);
      this->service_guard_(cycle, is_hp1) = "minimale uit-tijd: nog " + std::to_string(remaining_s) + " s";
      this->log_block_change(is_hp1, reason);
    }

    int applied = level;
    if (preflight == oq_thermal_actuator::PreflightBlock::MODE) {
      block_reason = "no valid thermal mode for compressor request";
      candidate_reason = openquatt_decision_log::REASON_CANDIDATE_UNAVAILABLE;
      this->service_guard_(cycle, is_hp1) = "geen geldige werkmodus voor compressorstart";
      this->log_block_change(is_hp1, block_reason);
    }

    int cap_hz = -1;
    const bool use_frequency_policy = !cycle.manual_service_active;
    if (applied > 0 && use_frequency_policy) {
      cap_hz = cycle.frequency.cap_hz;
      applied = cycle.frequency.pick_allowed_level(is_hp1, expected_mode, applied, 1, MAX_LEVEL);
    }

    oq_odu::LevelCommand command{};
    if (applied > 0) {
      command = cycle.manual_service_active
                    ? oq_odu::resolve_manual_level(cycle.frequency.configured_v2, cycle.frequency.snapshot(is_hp1),
                                                   expected_mode, applied)
                    : oq_odu::resolve_automatic_level(cycle.frequency.configured_v2, cycle.frequency.snapshot(is_hp1),
                                                      expected_mode, applied);
      if (!oq_thermal_actuator::valid_level_command(command.control_level, command.physical_level)) {
        applied = 0;
        command = {};
        block_reason = "runtime frequency table cannot map compressor request";
        candidate_reason = openquatt_decision_log::REASON_CANDIDATE_UNAVAILABLE;
        this->service_guard_(cycle, is_hp1) = "frequentietabel bevat geen bruikbare compressorstand";
        this->log_block_change(is_hp1, block_reason);
      }
    }

    if (applied == 0 && retained.control_level > 0) {
      this->apply_active_mode_hold(is_hp1, retained, cycle.request_mode_option);
      this->publish_optimizer_reason("defrost_protect_hold");
      return retained.control_level;
    }
    if (oq_cooling::cooling_stop_requires_confirmation(was_cooling, previous, incident.running_confirmed, applied)) {
      this->cooling_confirmation_pending_(is_hp1) = true;
      cycle.cooling_stop_armed = true;
    }

    bool safe_mode_written = false;
    if (applied > 0) {
      const bool authorized = oq_incident_actuator::apply_start_gate_before_active_write(
          previous == 0,
          [&]() {
            return id(oq_incident_manager)
                .request_start(hp_index, static_cast<uint8_t>(expected_mode), cycle.config.now_ms);
          },
          [&]() {
            this->set_mode(is_hp1, true, false, cycle.request_mode_code, cycle.request_thermal_active,
                           cycle.manual_service_active);
          },
          [&]() {
            this->set_mode(is_hp1, false, false, cycle.request_mode_code, cycle.request_thermal_active,
                           cycle.manual_service_active);
            safe_mode_written = true;
          });
      if (!authorized) {
        applied = 0;
        command = {};
        block_reason = "waiting for incident-manager start release";
        candidate_reason = openquatt_decision_log::REASON_CANDIDATE_UNAVAILABLE;
        this->service_guard_(cycle, is_hp1) = "wachten op technische startvrijgave";
        this->log_block_change(is_hp1, block_reason);
      }
    }
    if (applied == 0 && !safe_mode_written) {
      const bool register_initial_stop = previous > 0 || incident.running_confirmed || register_incident_stop;
      const bool notify_stop = oq_incident_actuator::requires_stop_notification(
          register_initial_stop, incident.must_stop, incident.stop_confirmed, incident.stop_confirmation_pending);
      oq_incident_actuator::apply_stop_notification_before_safe_write(
          notify_stop, [&]() { id(oq_incident_manager).request_stop(hp_index, cycle.config.now_ms); },
          [&]() {
            this->set_mode(is_hp1, false, force_safe_write, cycle.request_mode_code, cycle.request_thermal_active,
                           cycle.manual_service_active);
          });
      if (register_initial_stop || force_safe_write) last_safe_write_ms = cycle.config.now_ms;
    }

    if (applied == 0 && level > 0) {
      if (block_reason.empty() && use_frequency_policy) {
        char reason[144];
        snprintf(reason, sizeof(reason), "frequency policy blocked request %d (cap=%d Hz)", requested, cap_hz);
        block_reason = reason;
        candidate_reason = openquatt_decision_log::REASON_CANDIDATE_UNAVAILABLE;
        this->log_block_change(is_hp1, reason);
      } else if (block_reason.empty() && retained.control_level == 0) {
        block_reason = "actuator gating held request at 0";
        candidate_reason = openquatt_decision_log::REASON_NO_CANDIDATE;
        this->service_guard_(cycle, is_hp1) = "technische bewaking houdt verzoek tegen";
        this->log_block_change(is_hp1, block_reason);
      }
    } else if (block_reason.empty()) {
      this->log_block_change(is_hp1, "");
    }

    this->publish_candidate_blocked(is_hp1,
                                    requested > 0 && previous == 0 && applied == 0
                                        ? candidate_reason
                                        : static_cast<uint8_t>(openquatt_decision_log::REASON_UNKNOWN),
                                    requested, candidate_aux_s);
    this->write_level(is_hp1, command.physical_level, force_safe_write);
    return command.control_level;
  }

  int previous_applied_(bool is_hp1) const {
    return is_hp1 ? id(hp1_last_applied_level) : OQ_ACTUATOR_SECONDARY_ID(last_applied_level);
  }

  uint32_t last_stop_ms_(bool is_hp1) const {
    return is_hp1 ? id(hp1_last_stop_ms) : OQ_ACTUATOR_SECONDARY_ID(last_stop_ms);
  }

  bool& cooling_confirmation_pending_(bool is_hp1) {
    return is_hp1 ? id(oq_cooling_stop_confirmation_pending_hp1) : id(oq_cooling_stop_confirmation_pending_hp2);
  }

  static std::string& service_guard_(Cycle& cycle, bool is_hp1) { return cycle.service_guards[is_hp1 ? 0 : 1]; }

  static uint16_t cap_seconds_(uint32_t seconds) {
    return seconds > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(seconds);
  }

  bool applied_mode_is_cooling_(bool is_hp1, int request_mode_code) const {
    const auto incident = id(oq_incident_manager).get_outputs(is_hp1 ? 1U : 2U);
    const bool running = this->previous_applied_(is_hp1) > 0 || incident.running_confirmed;
    return running && (request_mode_code == 1 || id(oq_control_mode_code) == 5 || this->mode_is(is_hp1, 1) ||
                       this->mode_target_is(is_hp1, 1));
  }

  void publish_topology_change_(int hp1_applied, int hp2_applied, uint32_t now_ms) {
#if OQ_TOPOLOGY_DUO
    const int previous_count = (id(hp1_last_applied_level) > 0 ? 1 : 0) + (id(hp2_last_applied_level) > 0 ? 1 : 0);
    const int new_count = (hp1_applied > 0 ? 1 : 0) + (hp2_applied > 0 ? 1 : 0);
    if (previous_count != new_count) {
      const uint8_t subject = new_count >= 2    ? openquatt_decision_log::SUBJECT_BOTH
                              : hp1_applied > 0 ? openquatt_decision_log::SUBJECT_HP1
                              : hp2_applied > 0 ? openquatt_decision_log::SUBJECT_HP2
                                                : openquatt_decision_log::SUBJECT_SYSTEM;
      id(oq_decision_log)
          .emit(openquatt_decision_log::EVENT_TOPOLOGY_CHANGE, subject,
                this->transition_reason(new_count > previous_count), openquatt_decision_log::SEVERITY_NORMAL,
                static_cast<uint8_t>(id(oq_control_mode_code)),
                oq_thermal_actuator::topology_state(previous_count, openquatt_decision_log::STATE_IDLE,
                                                    openquatt_decision_log::STATE_SINGLE,
                                                    openquatt_decision_log::STATE_DUO),
                oq_thermal_actuator::topology_state(new_count, openquatt_decision_log::STATE_IDLE,
                                                    openquatt_decision_log::STATE_SINGLE,
                                                    openquatt_decision_log::STATE_DUO),
                static_cast<int16_t>(hp1_applied), static_cast<int16_t>(hp2_applied));
    }
    if (previous_count > 0 && new_count > 0 && previous_count != new_count) {
      id(oq_duo_request_hold_until_ms) = now_ms + 180000UL;
    }
#else
    (void)hp1_applied;
    (void)hp2_applied;
    (void)now_ms;
#endif
  }

  void confirm_cooling_stop_(bool is_hp1, uint32_t now_ms) {
    bool& pending = this->cooling_confirmation_pending_(is_hp1);
    if (!pending) return;
    const auto outputs = id(oq_incident_manager).get_outputs(is_hp1 ? 1U : 2U);
    if (!oq_cooling::record_confirmed_cooling_stop(pending, outputs.stop_confirmed, now_ms,
                                                   id(oq_cooling_last_confirmed_stop_ms),
                                                   id(oq_cooling_confirmed_stop_seen))) {
      return;
    }
    pending = false;
  }

  bool defrost_bit_active_(bool is_hp1) const {
    return is_hp1 ? id(hp1_defrost).state : OQ_ACTUATOR_SECONDARY_ID(defrost).state;
  }

  bool real_defrost_seen_(bool is_hp1) const {
    const bool cooling_active =
        this->mode_is(is_hp1, 1) || this->mode_target_is(is_hp1, 1) || id(oq_control_mode_code) == 5;
    return this->defrost_bit_active_(is_hp1) && !cooling_active;
  }

  void publish_defrost_seen_(bool is_hp1, bool active, uint32_t now_ms) {
    const size_t index = is_hp1 ? 0 : 1;
    if (active == this->last_defrost_seen_[index]) return;
    uint16_t duration_s = 0;
    if (active) {
      this->defrost_started_ms_[index] = now_ms;
    } else if (this->defrost_started_ms_[index] != 0) {
      const uint32_t elapsed_s = (now_ms - this->defrost_started_ms_[index]) / 1000UL;
      duration_s = elapsed_s > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(elapsed_s);
    }
    id(oq_decision_log)
        .emit(active ? openquatt_decision_log::EVENT_DEFROST_SEEN_START
                     : openquatt_decision_log::EVENT_DEFROST_SEEN_CLEAR,
              is_hp1 ? openquatt_decision_log::SUBJECT_HP1 : openquatt_decision_log::SUBJECT_HP2,
              openquatt_decision_log::REASON_DEFROST_HOLD, openquatt_decision_log::SEVERITY_NORMAL,
              static_cast<uint8_t>(id(oq_control_mode_code)),
              active ? openquatt_decision_log::STATE_IDLE : openquatt_decision_log::STATE_ACTIVE,
              active ? openquatt_decision_log::STATE_ACTIVE : openquatt_decision_log::STATE_IDLE, 0, 0, 0, duration_s);
    if (!active) this->defrost_started_ms_[index] = 0;
    this->last_defrost_seen_[index] = active;
  }

  void write_mode_option_(bool is_hp1, const char* option, bool force_write) {
    if (is_hp1) {
      if (force_write || id(hp1_set_working_mode).current_option() != option) {
        auto call = id(hp1_set_working_mode).make_call();
        call.set_option(option);
        call.perform();
      }
      return;
    }
#if OQ_TOPOLOGY_DUO
    if (force_write || id(hp2_set_working_mode).current_option() != option) {
      auto call = id(hp2_set_working_mode).make_call();
      call.set_option(option);
      call.perform();
    }
#else
    (void)option;
    (void)force_write;
#endif
  }

  uint8_t heating_transition_reason_() const {
    if (id(oq_control_mode_code) == 98) return openquatt_decision_log::REASON_FROST_PROTECTION;
    if (!id(oq_strategy_heat_request_active)) return openquatt_decision_log::REASON_HEATING_REQUEST_CLEARED;
    if (id(oq_heat_mode_code) == 1) {
      if (id(oq_curve_fast_intent_code) == 2) return openquatt_decision_log::REASON_SETPOINT_RAISE;
      if (id(oq_curve_fast_intent_code) == 1) return openquatt_decision_log::REASON_ROOM_DEMAND;
#if OQ_TOPOLOGY_DUO
      return id(oq_curve_capacity_mode_code) == 2 ? openquatt_decision_log::REASON_BETTER_HEAT
                                                  : openquatt_decision_log::REASON_RUNTIME_LEAD;
#else
      return openquatt_decision_log::REASON_RUNTIME_LEAD;
#endif
    }
    if (id(oq_ph_fast_intent_code) == 2) return openquatt_decision_log::REASON_SETPOINT_RAISE;
    if (id(oq_ph_fast_intent_code) == 1) return openquatt_decision_log::REASON_ROOM_DEMAND;
    return static_cast<uint8_t>(id(oq_ph_request_reason_code));
  }

  uint8_t source_stop_reason_() const {
    bool cooling_context = id(oq_control_mode_code) == 5 || id(oq_request_mode_code) == 1 || this->mode_is(true, 1) ||
                           this->mode_target_is(true, 1);
#if OQ_TOPOLOGY_DUO
    cooling_context = cooling_context || this->mode_is(false, 1) || this->mode_target_is(false, 1);
#endif
    if (!cooling_context) return this->heating_transition_reason_();
    const int stop_reason = id(oq_cooling_stop_reason_code);
    if (stop_reason == 2) return openquatt_decision_log::REASON_DEW_STOP;
    if (stop_reason == 3 || stop_reason == 7) return openquatt_decision_log::REASON_SENSOR_FALLBACK;
    if (stop_reason == 6) return openquatt_decision_log::REASON_FLOW_TOO_LOW;
    if (stop_reason == 1 || stop_reason == 4) return openquatt_decision_log::REASON_COOLING_LIMITER;
    if (stop_reason == 5 || (id(cooling_request_active).has_state() && !id(cooling_request_active).state)) {
      return openquatt_decision_log::REASON_COOLING_REQUEST_CLEARED;
    }
    return openquatt_decision_log::REASON_KEEP_CURRENT;
  }

  static void finish_manual_guard_(int requested_level, int applied_level, std::string& guard) {
    if (requested_level <= 0 && applied_level > 0) {
      guard = "minimale draaitijd: tijdelijk stand " + std::to_string(applied_level);
    } else if (requested_level > 0 && applied_level == 0 && guard == "Vrijgegeven") {
      guard = "technische bewaking houdt verzoek tegen";
    }
  }

  bool measured_thermal_mode_(bool is_hp1) const {
    const float raw = is_hp1 ? id(hp1_working_mode).state : OQ_ACTUATOR_SECONDARY_ID(working_mode).state;
    if (isnan(raw)) return false;
    const int mode = static_cast<int>(roundf(raw));
    return mode == 1 || mode == 2;
  }

  void set_last_start_ms_(bool is_hp1, uint32_t now_ms) {
    (is_hp1 ? id(hp1_last_start_ms) : OQ_ACTUATOR_SECONDARY_ID(last_start_ms)) = now_ms;
  }

  void set_last_stop_ms_(bool is_hp1, uint32_t now_ms) {
    (is_hp1 ? id(hp1_last_stop_ms) : OQ_ACTUATOR_SECONDARY_ID(last_stop_ms)) = now_ms;
  }

  void emit_transition_(bool is_hp1, bool starting, int level) const {
    id(oq_decision_log)
        .emit(starting ? openquatt_decision_log::EVENT_SOURCE_START : openquatt_decision_log::EVENT_SOURCE_STOP,
              is_hp1 ? openquatt_decision_log::SUBJECT_HP1 : openquatt_decision_log::SUBJECT_HP2,
              this->transition_reason(starting), openquatt_decision_log::SEVERITY_NORMAL,
              static_cast<uint8_t>(id(oq_control_mode_code)),
              starting ? openquatt_decision_log::STATE_IDLE : openquatt_decision_log::STATE_ACTIVE,
              starting ? openquatt_decision_log::STATE_ACTIVE : openquatt_decision_log::STATE_IDLE,
              static_cast<int16_t>(level), 0, 0);
  }

  std::string last_optimizer_reason_;
  std::string last_block_reasons_[2];
  std::string last_manual_guard_status_;
  bool last_defrost_seen_[2]{false, false};
  bool last_defrost_holds_[2]{false, false};
  bool applied_cooling_[2]{false, false};
  uint32_t defrost_started_ms_[2]{0, 0};
  uint32_t last_safe_stop_write_ms_[2]{0, 0};
  oq_odu::RetainedLevel retained_levels_[2]{};
  uint8_t last_candidate_reasons_[2]{openquatt_decision_log::REASON_UNKNOWN, openquatt_decision_log::REASON_UNKNOWN};
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

#undef OQ_ACTUATOR_SECONDARY_ID

}  // namespace oq_thermal_actuator_runtime
#endif
