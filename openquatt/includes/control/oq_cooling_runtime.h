#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "oq_compressor_frequency_runtime.h"
#include "oq_cooling_demand_logic.h"
#include "oq_cooling_dispatch_logic.h"
#include "oq_thermal_request_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_cooling_runtime {

struct DemandConfig {
  uint32_t oil_return_hold_ms;
  uint32_t limiter_stop_confirm_ms;
};

struct DispatchConfig {
  uint32_t cadence_ms;
  uint32_t hp_min_off_ms;
  int demand_max_f;
  int minimum_off_floor_s;
};

class Runtime {
 public:
  float minimum_off_remaining_s(int minimum_off_floor_s) const {
    if (!this->restart_by_minimum_off_time_()) return 0.0f;
    const uint32_t configured_ms = this->minimum_off_ms_(minimum_off_floor_s);
    uint32_t remaining_ms = oq_cooling::global_minimum_off_time_remaining_ms(
        true, static_cast<uint32_t>(millis()), id(oq_cooling_confirmed_stop_seen),
        id(oq_cooling_last_confirmed_stop_ms), id(oq_cooling_boot_min_off_elapsed), configured_ms);
    if (this->stop_confirmation_pending_()) remaining_ms = std::max(remaining_ms, configured_ms);
    return static_cast<float>((remaining_ms + 999UL) / 1000UL);
  }

  void demand_tick(const DemandConfig& config) {
    oq_cooling::DemandTuning tuning;
    tuning.oil_return.hold_ms = config.oil_return_hold_ms;
    tuning.limiter.limiter_stop_confirm_ms = config.limiter_stop_confirm_ms;
    bool oil_return_active = id(hp1_prot_oil_return).state;
#if OQ_TOPOLOGY_DUO
    oil_return_active = oil_return_active || id(hp2_prot_oil_return).state;
#endif
    const auto& previous = oq_cooling::demand_runtime().state();
    const bool cycle_was_active = previous.water_cycle.active || previous.limited_demand > 0 ||
                                  previous.base_demand > 0 || id(oq_cooling_request_hp1_level) > 0 ||
                                  id(oq_cooling_request_hp2_level) > 0;
    const bool enabled = id(cooling_enable_selected).has_state() && id(cooling_enable_selected).state;
    const bool request_known = id(cooling_request_active).has_state();
    const bool request_active = request_known && id(cooling_request_active).state;
    const bool request_cleared = enabled && request_known && !request_active;
    const bool request_still_active = cycle_was_active && enabled && request_active;
    const bool core_permitted = id(cooling_permitted_core).has_state() && id(cooling_permitted_core).state;
    const bool core_permission_lost = request_still_active && !core_permitted;
    const bool flow_permission_lost =
        request_still_active && core_permitted && (!id(cooling_permitted).has_state() || !id(cooling_permitted).state);
    const bool cooling_hp_applied = id(hp1_last_applied_level) > 0
#if OQ_TOPOLOGY_DUO
                                    || id(hp2_last_applied_level) > 0
#endif
        ;
    const bool minimum_off_wait_active =
        cooling_hp_applied || id(oq_cooling_confirmed_stop_seen) || this->stop_confirmation_pending_();
    const bool user_responsibility = id(cooling_without_dew_point_user_responsibility_enabled).has_state() &&
                                     id(cooling_without_dew_point_user_responsibility_enabled).state;
    const bool dew_mode = !user_responsibility && id(cooling_dew_point_available).has_state() &&
                          id(cooling_dew_point_available).state && id(cooling_dew_point_selected).has_state() &&
                          std::isfinite(id(cooling_dew_point_selected).state);
    const bool fallback_mode = !user_responsibility && !dew_mode && id(cooling_fallback_active).has_state() &&
                               id(cooling_fallback_active).state;
    const bool room_valid = id(room_temp_selected).has_state() && std::isfinite(id(room_temp_selected).state) &&
                            id(room_setpoint_selected).has_state() && std::isfinite(id(room_setpoint_selected).state);
    const float room_error_c = room_valid ? id(room_temp_selected).state - id(room_setpoint_selected).state : NAN;
    const bool supply_valid = id(oq_system_supply_temp).has_state() && std::isfinite(id(oq_system_supply_temp).state);
    const bool target_valid = id(cooling_supply_target).has_state() && std::isfinite(id(cooling_supply_target).state);

    oq_cooling::DemandInput input;
    input.now_ms = static_cast<uint32_t>(millis());
    input.control_active =
        enabled && request_active && id(cooling_permitted).has_state() && id(cooling_permitted).state;
    input.cycle_was_active = cycle_was_active;
    input.request_cleared = request_cleared;
    input.sensor_valid = supply_valid && target_valid;
    input.flow_permission_lost = flow_permission_lost;
    input.core_permission_lost = core_permission_lost;
    input.oil_return_active = oil_return_active;
    input.cooling_hp_applied = cooling_hp_applied;
    input.restart_by_minimum_off_time = this->restart_by_minimum_off_time_();
    input.minimum_off_stop_pending = id(oq_cooling_min_off_stop_pending);
    input.minimum_off_wait_active = minimum_off_wait_active;
    input.guard_mode = user_responsibility ? oq_cooling::GUARD_USER_RESPONSIBILITY
                       : dew_mode          ? oq_cooling::GUARD_DEW
                       : fallback_mode     ? oq_cooling::GUARD_FALLBACK
                                           : oq_cooling::GUARD_NONE;
    input.dew_mode = dew_mode;
    input.fallback_mode = fallback_mode;
    input.room_error_valid = room_valid && std::isfinite(room_error_c);
    input.supply_c = supply_valid ? id(oq_system_supply_temp).state : NAN;
    input.target_c = target_valid ? id(cooling_supply_target).state : NAN;
    input.dew_point_c = dew_mode ? id(cooling_dew_point_selected).state : NAN;
    input.safety_margin_c =
        id(cooling_safety_margin_selected).has_state() ? id(cooling_safety_margin_selected).state : NAN;
    input.room_error_c = room_error_c;
    input.restart_delta_c = id(cooling_restart_delta).has_state() ? id(cooling_restart_delta).state : NAN;
    input.demand_max = id(cooling_demand_max_f).has_state() ? id(cooling_demand_max_f).state : NAN;
    input.kp = id(cooling_pid_kp).has_state() ? id(cooling_pid_kp).state : NAN;
    input.ki = id(cooling_pid_ki).has_state() ? id(cooling_pid_ki).state : NAN;
    input.kd = id(cooling_pid_kd).has_state() ? id(cooling_pid_kd).state : NAN;

    const auto decision = oq_cooling::demand_runtime().tick(input, tuning);
    const auto& cooling = oq_cooling::demand_runtime().state();
    id(oq_cooling_demand_raw) = cooling.limited_demand;
    id(oq_cooling_stop_reason_code) = cooling.water_cycle.stop_reason_code;
    if (decision.arm_minimum_off_stop) id(oq_cooling_min_off_stop_pending) = true;
    const bool oil_return_mask = cooling.limiter_reason_code == oq_cooling::REASON_OIL_RETURN_HOLD;
    if (decision.control_active && oil_return_mask != this->last_oil_return_mask_active_) {
      ESP_LOGI("quatt.cool", "Cooling oil-return recovery %s", oil_return_mask ? "active" : "cleared");
      this->last_oil_return_mask_active_ = oil_return_mask;
    }
    if (decision.guard_changed) ESP_LOGI("quatt.cool", "Cooling guard mode changed: reset restart latch");
    this->publish_limiter_event_(decision.event_limiter_active, cooling.limiter_reason_code, cooling.limited_demand,
                                 cooling.base_demand, cooling.dew_gap_c, decision.event_hard_pause);
    if (decision.control_active && cooling.limiter_reason_code != this->last_limiter_reason_) {
      ESP_LOGI("quatt.cool", "Cooling limiter: %s f=%d raw=%d lim=%d gap=%.2f ema=%.2f rate=%.2f dew=%.2f",
               oq_cooling::reason_name(cooling.limiter_reason_code), cooling.limited_demand, cooling.base_demand,
               cooling.allowed_max, input.supply_c - input.target_c, cooling.filter.filtered_gap_c,
               cooling.filter.rate_c_per_min, cooling.dew_gap_c);
      this->last_limiter_reason_ = cooling.limiter_reason_code;
    }
  }

  void dispatch_tick(const DispatchConfig& config) {
    const uint32_t now_ms = static_cast<uint32_t>(millis());
    if (id(oq_control_mode_code) != 5) {
      oq_cooling::DispatchInput reset;
      reset.now_ms = now_ms;
      oq_cooling::dispatch_tick(reset);
      id(oq_cooling_request_hp1_level) = 0;
      id(oq_cooling_request_hp2_level) = 0;
      id(oq_cooling_request_owner_hp) = 0;
      id(oq_cooling_owner_hp) = 0;
      id(oq_cooling_request_reason_code) = 0;
      return;
    }
    const uint32_t global_remaining_ms = oq_cooling::global_minimum_off_time_remaining_ms(
        this->restart_by_minimum_off_time_(), now_ms, id(oq_cooling_confirmed_stop_seen),
        id(oq_cooling_last_confirmed_stop_ms), id(oq_cooling_boot_min_off_elapsed),
        this->minimum_off_ms_(config.minimum_off_floor_s));
    const bool stop_confirmation_pending = this->stop_confirmation_pending_();
#if OQ_TOPOLOGY_DUO
    const bool lead_is_hp1 = id(hp1_minutes) <= id(hp2_minutes);
#else
    const bool lead_is_hp1 = true;
#endif
    id(oq_last_lead_hp) = lead_is_hp1 ? 1 : 2;
    const auto frequency = oq_frequency_runtime::capture();
    const auto any_level_allowed = [&](bool hp1) {
      for (int level = 1; level <= 10; ++level)
        if (frequency.frequency_allowed(hp1, 1, level)) return true;
      return false;
    };
    const auto hp1_candidate =
        oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(1), id(hp1_last_applied_level));
#if OQ_TOPOLOGY_DUO
    const auto hp2_candidate =
        oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(2), id(hp2_last_applied_level));
#else
    const oq_hp_candidate::HpCandidateState hp2_candidate;
#endif
    oq_cooling::DispatchInput dispatch;
    dispatch.now_ms = now_ms;
    dispatch.cadence_ms = config.cadence_ms;
    dispatch.hp_min_off_ms = config.hp_min_off_ms;
    dispatch.global_min_off_remaining_ms = global_remaining_ms;
    dispatch.raw_demand = id(oq_cooling_demand_raw);
    dispatch.demand_max = config.demand_max_f;
    dispatch.power_cap = id(oq_power_cap_f);
    dispatch.stored_owner = id(oq_cooling_owner_hp);
    dispatch.cooling_mode = true;
    dispatch.lead_is_hp1 = lead_is_hp1;
    dispatch.stop_confirmation_pending = stop_confirmation_pending;
    dispatch.hp1 = {hp1_candidate, id(hp1_last_start_ms), id(hp1_last_stop_ms), any_level_allowed(true)};
#if OQ_TOPOLOGY_DUO
    dispatch.duo = true;
    dispatch.hp2 = {hp2_candidate, id(hp2_last_start_ms), id(hp2_last_stop_ms), any_level_allowed(false)};
#endif
    const auto routing = oq_cooling::dispatch_tick(dispatch);
    if (!routing.evaluated) return;
    id(oq_demand_filtered_prev) = id(oq_demand_filtered);
    id(oq_demand_filtered) = routing.raw_demand;
    id(oq_heating_demand_filtered) = 0;
    this->log_start_block_(routing.start_blocked, now_ms, global_remaining_ms, stop_confirmation_pending,
                           config.hp_min_off_ms);
    oq_request::reset_dual_runtime_state(id(oq_dual_hp_enabled), id(oq_dual_hp_enable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_disable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_emergency_hold_elapsed_accum_min), id(oq_cooling_owner_hp),
                                         id(oq_duo_request_hold_until_ms),
#if OQ_TOPOLOGY_DUO
                                         routing.owner_before_hold);
#else
                                         1);
#endif
#if OQ_TOPOLOGY_DUO
    id(oq_cooling_owner_hp) = routing.owner;
#endif
    id(oq_P_hp_cap_w) = 0.0f;
    id(oq_P_deficit_w) = 0.0f;
    id(oq_cooling_request_hp1_level) = routing.hp1_request;
    id(oq_cooling_request_hp2_level) = routing.hp2_request;
    id(oq_cooling_request_owner_hp) = routing.owner;
    id(oq_cooling_request_reason_code) = routing.owner;
    id(oq_strategy_phase_code) = routing.demand > 0 ? 1 : 0;
    id(oq_strategy_requested_power_w) = NAN;
    id(oq_strategy_supply_target_temp) = id(cooling_supply_target).state;
    id(oq_strategy_heat_request_active) = routing.demand > 0;
    id(oq_strategy_output_valid) = std::isfinite(id(cooling_supply_target).state);
    id(oq_strategy_output_source_code) = 1;
    id(oq_strategy_output_updated_ms) = now_ms;
    id(oq_strategy_phase_text).publish_state(routing.demand > 0 ? "cool" : "idle");
    const auto& cooling = oq_cooling::demand_runtime().state();
    ESP_LOGD("quatt.strategy",
             "cool f=%d raw=%d lim=%d owner=%d gap=%.2f ema=%.2f rate=%.2f dew=%.2f reason=%s target=%.2f",
             routing.demand, cooling.base_demand, cooling.allowed_max, routing.owner,
             id(oq_system_supply_temp).state - id(cooling_supply_target).state, cooling.filter.filtered_gap_c,
             cooling.filter.rate_c_per_min, cooling.dew_gap_c, oq_cooling::reason_name(cooling.limiter_reason_code),
             id(cooling_supply_target).state);
  }

 private:
  static bool restart_by_minimum_off_time_() {
    return id(cooling_restart_mode).has_state() && id(cooling_restart_mode).current_option() == "Minimum off time";
  }

  static bool stop_confirmation_pending_() {
    return id(oq_cooling_stop_confirmation_pending_hp1)
#if OQ_TOPOLOGY_DUO
           || id(oq_cooling_stop_confirmation_pending_hp2)
#endif
        ;
  }

  static uint32_t minimum_off_ms_(int minimum_off_floor_s) {
    const float configured = id(cooling_minimum_off_time).state;
    int seconds = std::isfinite(configured) ? static_cast<int>(std::lround(configured)) : 600;
    seconds = std::max(minimum_off_floor_s, std::min(3600, seconds));
    return static_cast<uint32_t>(seconds) * 1000UL;
  }

  void log_start_block_(bool blocked, uint32_t now_ms, uint32_t global_remaining_ms, bool confirmation_pending,
                        uint32_t hp_min_off_ms) {
    if (blocked == this->last_start_blocked_) return;
    if (blocked) {
      const uint32_t hp1_remaining_s = oq_cooling::hp_minimum_off_remaining_s(
          now_ms, id(hp1_last_stop_ms), id(hp1_last_applied_level), hp_min_off_ms);
#if OQ_TOPOLOGY_DUO
      const uint32_t hp2_remaining_s = oq_cooling::hp_minimum_off_remaining_s(
          now_ms, id(hp2_last_stop_ms), id(hp2_last_applied_level), hp_min_off_ms);
#else
      const uint32_t hp2_remaining_s = 0;
#endif
      ESP_LOGI("quatt.cool", "Cooling request blocked: global=%u s confirm=%d hp1=%u s hp2=%u s/unavailable",
               static_cast<unsigned>((global_remaining_ms + 999UL) / 1000UL), confirmation_pending,
               static_cast<unsigned>(hp1_remaining_s), static_cast<unsigned>(hp2_remaining_s));
    } else {
      ESP_LOGI("quatt.cool", "Cooling request block cleared");
    }
    this->last_start_blocked_ = blocked;
  }

  static uint8_t decision_reason_(int limiter_reason) {
    switch (limiter_reason) {
      case oq_cooling::REASON_PROJECTED_FLOOR:
        return openquatt_decision_log::REASON_PROJECTED_FLOOR;
      case oq_cooling::REASON_SIMMER:
        return openquatt_decision_log::REASON_SIMMER;
      case oq_cooling::REASON_FALLING_GAP:
        return openquatt_decision_log::REASON_FALLING_GAP;
      case oq_cooling::REASON_BUFFER_STOP:
        return openquatt_decision_log::REASON_BUFFER_STOP;
      case oq_cooling::REASON_DEW_STOP:
        return openquatt_decision_log::REASON_DEW_STOP;
      case oq_cooling::REASON_FALLBACK_FLOOR:
      case oq_cooling::REASON_FALLBACK_CAP1:
        return openquatt_decision_log::REASON_SENSOR_FALLBACK;
      case oq_cooling::REASON_RESTART_WAIT:
        return openquatt_decision_log::REASON_RESTART_WAIT;
      case oq_cooling::REASON_ROOM_CAP:
        return openquatt_decision_log::REASON_ROOM_CAP;
      case oq_cooling::REASON_LEVEL1_HOLD:
        return openquatt_decision_log::REASON_LEVEL1_HOLD;
      case oq_cooling::REASON_OIL_RETURN_HOLD:
        return openquatt_decision_log::REASON_OIL_RETURN_HOLD;
      case oq_cooling::REASON_OIL_RETURN_RECOVERY:
        return openquatt_decision_log::REASON_OIL_RETURN_RECOVERY;
      case oq_cooling::REASON_CAPACITY_CAP:
        return openquatt_decision_log::REASON_CAPACITY_CAP;
      default:
        return openquatt_decision_log::REASON_COOLING_LIMITER;
    }
  }

  void publish_limiter_event_(bool limiter_active, int limiter_reason, int limited_demand, int raw_demand,
                              float dew_gap_c, bool hard_pause) {
    const bool paused = limiter_active && limited_demand <= 0 && (hard_pause || raw_demand > 0);
    if (paused == this->last_pause_active_) return;
    if (paused) {
      id(oq_decision_log)
          .emit(openquatt_decision_log::EVENT_COOLING_LIMITED, openquatt_decision_log::SUBJECT_COOLING,
                this->decision_reason_(limiter_reason), openquatt_decision_log::SEVERITY_LIMITED,
                static_cast<uint8_t>(id(oq_control_mode_code)), openquatt_decision_log::STATE_ACTIVE,
                openquatt_decision_log::STATE_LIMITED, static_cast<int16_t>(limited_demand),
                static_cast<int16_t>(raw_demand),
                !std::isfinite(dew_gap_c) ? 0 : static_cast<int16_t>(std::lround(dew_gap_c * 100.0f)));
    } else {
      id(oq_decision_log)
          .emit(openquatt_decision_log::EVENT_COOLING_RELEASED, openquatt_decision_log::SUBJECT_COOLING,
                openquatt_decision_log::REASON_KEEP_CURRENT, openquatt_decision_log::SEVERITY_NORMAL,
                static_cast<uint8_t>(id(oq_control_mode_code)), openquatt_decision_log::STATE_LIMITED,
                openquatt_decision_log::STATE_ACTIVE);
    }
    this->last_pause_active_ = paused;
  }

  bool last_pause_active_{false};
  bool last_oil_return_mask_active_{false};
  bool last_start_blocked_{false};
  int last_limiter_reason_{-1};
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_cooling_runtime
#endif
