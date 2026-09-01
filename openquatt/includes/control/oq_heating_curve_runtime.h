#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "../performance/hp_perf_frequency.h"
#include "oq_compressor_frequency_runtime.h"
#include "oq_heating_curve_logic.h"
#include "oq_hp_candidate_logic.h"
#include "oq_thermal_request_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_heating_curve_runtime {

struct DispatchConfig {
  uint32_t loop_ms;
  int demand_max_f;
};

class Runtime {
 public:
  void reset_profile() {
    this->reset_control_();
    id(oq_curve_oil_return_hold_until_ms) = 0;
    this->reset_outside_ema_();
    this->reset_request_(0);
  }

  void write_pid_output(float pid_output, int demand_max_f, bool ot_room_temperature_fresh,
                        bool ot_room_setpoint_fresh) {
    if (id(oq_heat_mode_code) != 1) {
      this->reset_control_();
      return;
    }
    const float target_c = id(oq_supply_target_temp).state;
    const float supply_c = id(oq_system_supply_temp).state;
    const auto tuning = this->tuning_();
    const float room_c = id(room_temp_selected).state;
    const float room_setpoint_c = id(room_setpoint_selected).state;
    const bool room_data_fresh = std::isfinite(room_c) && std::isfinite(room_setpoint_c) &&
                                 this->room_temperature_fresh_(ot_room_temperature_fresh) &&
                                 this->room_setpoint_fresh_(ot_room_setpoint_fresh);
    const uint32_t now_ms = static_cast<uint32_t>(millis());

    if (room_data_fresh) {
      const float last_sp = id(oq_curve_last_room_setpoint_c);
      if (std::isfinite(last_sp) && !id(oq_curve_heat_request_active) &&
          room_setpoint_c > last_sp + kSetpointRaiseHysteresisC)
        id(oq_curve_setpoint_raise_latch) = true;
      id(oq_curve_last_room_setpoint_c) = room_setpoint_c;
    }

    bool oil_return_active = id(hp1_prot_oil_return).state;
#if OQ_TOPOLOGY_DUO
    oil_return_active = oil_return_active || id(hp2_prot_oil_return).state;
#endif
    const auto oil_return = oq_curve::update_oil_return_hold(now_ms, oil_return_active,
                                                             id(oq_curve_oil_return_hold_until_ms), kOilReturnHoldMs);
    int applied_total = std::max(0, static_cast<int>(id(hp1_last_applied_level)));
#if OQ_TOPOLOGY_DUO
    applied_total += std::max(0, static_cast<int>(id(hp2_last_applied_level)));
#endif
    const oq_curve::DemandState previous{id(oq_curve_heat_request_active),
                                         id(oq_curve_stop_arm_ms),
                                         id(oq_curve_off_since_ms),
                                         id(oq_curve_restart_inhibit_active),
                                         id(oq_curve_restart_blocked_by_room),
                                         id(oq_curve_regime_code)};
    const auto demand = oq_curve::decide_demand({now_ms, pid_output, target_c, supply_c, room_c, room_setpoint_c,
                                                 room_data_fresh, oil_return.mask_active,
                                                 id(oq_curve_setpoint_raise_latch), applied_total, demand_max_f},
                                                tuning, previous);
    if (demand.next.heat_request_active) id(oq_curve_setpoint_raise_latch) = false;
    if (demand.valid && demand.next.heat_request_active && !previous.heat_request_active)
      id(oq_curve_last_restart_reason) = static_cast<uint8_t>(demand.restart_reason);
    if (demand.valid && !demand.next.heat_request_active && previous.heat_request_active)
      id(oq_curve_last_restart_reason) = 0;
    id(oq_curve_oil_return_hold_until_ms) = oil_return.hold_until_ms;
    if (demand.valid)
      this->log_demand_transition_(now_ms, room_c, room_setpoint_c, oil_return.mask_active, tuning, previous, demand);
    id(oq_curve_demand_continuous) = demand.demand_continuous;
    id(oq_curve_demand_pre_guardrail) = demand.demand_pre_guardrail;
    id(oq_curve_heat_request_active) = demand.next.heat_request_active;
    id(oq_curve_stop_arm_ms) = demand.next.stop_arm_ms;
    id(oq_curve_off_since_ms) = demand.next.off_since_ms;
    id(oq_curve_restart_inhibit_active) = demand.next.restart_inhibit_active;
    id(oq_curve_restart_blocked_by_room) = demand.next.restart_blocked_by_room;
    id(oq_curve_regime_code) = demand.next.regime_code;
    id(oq_demand_curve) = demand.demand;
  }

  float filtered_outside_temperature() {
    const auto decision = oq_curve::update_outside_ema(
        static_cast<uint32_t>(millis()), id(outside_temp_selected).state, this->tuning_().outside_tau_s,
        {id(oq_curve_outside_ema_c), id(oq_curve_outside_ema_initialized), id(oq_curve_outside_ema_last_ms)});
    id(oq_curve_outside_ema_c) = decision.next.value_c;
    id(oq_curve_outside_ema_initialized) = decision.next.initialized;
    id(oq_curve_outside_ema_last_ms) = decision.next.last_ms;
    return decision.value_c;
  }

  float supply_target() const {
    const std::array<oq_curve::CurvePoint, 6> points{{
        {-20.0f, id(curve_tsupply_m20).state},
        {-10.0f, id(curve_tsupply_m10).state},
        {0.0f, id(curve_tsupply_0).state},
        {5.0f, id(curve_tsupply_5).state},
        {10.0f, id(curve_tsupply_10).state},
        {15.0f, id(curve_tsupply_15).state},
    }};
    return oq_curve::supply_target(id(oq_curve_outside_temp_filtered).state, id(curve_fallback_supply_temp).state,
                                   points, id(room_temp_selected).state, id(room_setpoint_selected).state,
                                   this->tuning_(), id(max_water_temp_limit_c).state);
  }

  void strategy_tick(int demand_max_f) {
    const bool active = id(oq_control_mode_code) != 5 && id(oq_heat_mode_code) == 1;
    const uint32_t now_ms = static_cast<uint32_t>(millis());
    if (!active) {
      if (id(oq_heating_curve_pid).mode != CLIMATE_MODE_OFF) {
        auto call = id(oq_heating_curve_pid).make_call();
        call.set_mode(CLIMATE_MODE_OFF);
        call.perform();
      }
      this->reset_control_();
      return;
    }

    const float target_c = id(oq_supply_target_temp).state;
    const float supply_c = id(oq_system_supply_temp).state;
    if (!std::isfinite(target_c) || !std::isfinite(supply_c)) {
      this->reset_control_();
    } else {
      const float current_target_c = id(oq_heating_curve_pid).target_temperature;
      if (id(oq_heating_curve_pid).mode != CLIMATE_MODE_HEAT || !std::isfinite(current_target_c) ||
          std::fabs(current_target_c - target_c) >= 0.10f) {
        auto call = id(oq_heating_curve_pid).make_call();
        if (id(oq_heating_curve_pid).mode != CLIMATE_MODE_HEAT) call.set_mode(CLIMATE_MODE_HEAT);
        call.set_target_temperature(target_c);
        call.perform();
      }
    }

    int demand = std::max(0, std::min(demand_max_f, static_cast<int>(id(oq_demand_curve))));
    if (id(oq_water_temp_hard_trip_active)) demand = 0;
    id(oq_demand_raw) = demand;
    id(oq_strategy_phase_code) = id(oq_curve_regime_code);
    id(oq_strategy_requested_power_w) = NAN;
    id(oq_strategy_supply_target_temp) = target_c;
    id(oq_strategy_heat_request_active) = id(oq_curve_heat_request_active);
    id(oq_strategy_hp_expected_power_w) = NAN;
    id(oq_strategy_hp_max_power_w) = NAN;
    id(oq_strategy_hp_saturated) = id(oq_curve_heat_request_active) && demand >= demand_max_f;
    id(oq_strategy_output_valid) = std::isfinite(target_c) && std::isfinite(supply_c);
    id(oq_strategy_output_source_code) = 2;
    id(oq_strategy_output_updated_ms) = now_ms;

    const char* regime = oq_curve::regime_name(id(oq_curve_regime_code));
    id(oq_strategy_phase_text).publish_state(regime);
    ESP_LOGD("quatt.strategy", "curve phase=%s reg=%s d=%.2f/%d pre=%d sp=%.2f pv=%.2f",
             !id(oq_curve_heat_request_active) ? "off" : (id(oq_curve_regime_code) == 1 ? "heat" : "coast"), regime,
             id(oq_curve_demand_continuous), demand, id(oq_curve_demand_pre_guardrail), target_c, supply_c);
  }

  bool integral_reset_required() const {
    if (id(oq_heat_mode_code) != 1) return false;
    if (id(oq_water_temp_hard_trip_active)) return true;
    const int control_mode = id(oq_control_mode_code);
    if (control_mode != 2 && control_mode != 3) return true;
    if (!id(oq_curve_heat_request_active)) return true;
    if (!std::isfinite(id(oq_supply_target_temp).state) || !std::isfinite(id(oq_system_supply_temp).state)) return true;
    const bool hp1_delivering =
        id(hp1_last_applied_level) > 0 && this->working_mode_heating_(id(hp1_working_mode).state);
#if OQ_TOPOLOGY_DUO
    const bool hp2_delivering =
        id(hp2_last_applied_level) > 0 && this->working_mode_heating_(id(hp2_working_mode).state);
#else
    const bool hp2_delivering = false;
#endif
    return !hp1_delivering && !hp2_delivering;
  }

  void dispatch_tick(const DispatchConfig& config) {
    const bool active = id(oq_control_mode_code) != 5 && id(oq_heat_mode_code) == 1;
    if (!active) {
      this->reset_request_(0);
      return;
    }
    const uint32_t now_ms = static_cast<uint32_t>(millis());
    if (!oq_curve::cadence_due(now_ms, id(oq_curve_request_last_loop_ms), config.loop_ms)) return;
    id(oq_curve_request_last_loop_ms) = oq_curve::timestamp_ms(now_ms);

    constexpr int level_cap = 10;
    const int raw = std::max(0, std::min(config.demand_max_f, static_cast<int>(id(oq_demand_raw))));
    id(oq_demand_filtered_prev) = id(oq_demand_filtered);
    id(oq_demand_filtered) = raw;
    id(oq_heating_demand_filtered) = raw;
    const int capped = std::min(raw, std::max(0, std::min(config.demand_max_f, static_cast<int>(id(oq_power_cap_f)))));

#if OQ_TOPOLOGY_DUO
    const bool lead_is_hp1 = id(hp1_minutes) <= id(hp2_minutes);
#else
    const bool lead_is_hp1 = true;
#endif
    id(oq_last_lead_hp) = lead_is_hp1 ? 1 : 2;
    const auto frequency = oq_frequency_runtime::capture();
    const bool hp1_valve_defrost = id(hp1_4_way_valve).state;
#if OQ_TOPOLOGY_DUO
    const bool hp2_valve_defrost = id(hp2_4_way_valve).state;
#else
    const bool hp2_valve_defrost = false;
#endif
    id(oq_P_hp_cap_w) = 0.0f;
    id(oq_P_deficit_w) = 0.0f;
    const auto hp1_candidate =
        oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(1), id(hp1_last_applied_level));
    const bool hp1_available = oq_hp_candidate::may_serve_candidate(hp1_candidate);
#if OQ_TOPOLOGY_DUO
    const auto hp2_candidate =
        oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(2), id(hp2_last_applied_level));
    const bool hp2_available = oq_hp_candidate::may_serve_candidate(hp2_candidate);
#else
    const oq_hp_candidate::HpCandidateState hp2_candidate;
    const bool hp2_available = false;
#endif

    int owner = 0;
    int hp1_level = 0;
    int hp2_level = 0;
    const float demand_u = oq_curve::power_capped_demand_u(id(oq_curve_demand_continuous), capped, config.demand_max_f);
    const float dispatch_u = config.demand_max_f > 0 ? static_cast<float>(capped) / config.demand_max_f : 0.0f;
    const bool demand_active = demand_u > 0.0f;
#if OQ_TOPOLOGY_DUO
    const auto tuning = this->tuning_();
    const float target_c = id(oq_supply_target_temp).state;
    const float supply_c = id(oq_system_supply_temp).state;
    const float temperature_error_c = std::isfinite(target_c) && std::isfinite(supply_c) ? target_c - supply_c : NAN;
    const bool heat_phase = demand_active && id(oq_curve_regime_code) == 1;
    const int previous_capped = id(oq_demand_filtered_prev);
    const auto level_allowed = [&](bool hp1, int level) { return frequency.frequency_allowed(hp1, 2, level); };
    const auto maximum_level = [&](bool hp1) {
      if ((hp1 && !hp1_available) || (!hp1 && !hp2_available)) return 0;
      for (int level = level_cap; level >= 1; --level)
        if (level_allowed(hp1, level)) return level;
      return 0;
    };
    const auto level_power_w = [&](bool hp1, int level) {
      if (level <= 0) return 0.0f;
      if (!level_allowed(hp1, level) || !std::isfinite(id(outside_temp_selected).state) || !std::isfinite(target_c))
        return NAN;
      return oq_perf::interp_power_th_w_hz(oq_perf::model_frequency_hz(level), id(outside_temp_selected).state,
                                           target_c);
    };
    const int previous_hp1 = id(hp1_last_applied_level);
    const int previous_hp2 = id(hp2_last_applied_level);
    const auto candidate = [&](int candidate_hp1, int candidate_hp2, float target_power_w) {
      if ((candidate_hp1 > 0 && !hp1_available) || (candidate_hp2 > 0 && !hp2_available))
        return oq_curve::invalid_dispatch_candidate();
      const float hp1_power_w = level_power_w(true, candidate_hp1);
      const float hp2_power_w = level_power_w(false, candidate_hp2);
      if (!std::isfinite(hp1_power_w) || !std::isfinite(hp2_power_w)) return oq_curve::invalid_dispatch_candidate();
      const float power_w = hp1_power_w + hp2_power_w;
      return oq_curve::DispatchCandidate{true,
                                         candidate_hp1,
                                         candidate_hp2,
                                         power_w,
                                         std::fabs(power_w - target_power_w),
                                         (candidate_hp1 > 0 ? 1 : 0) + (candidate_hp2 > 0 ? 1 : 0),
                                         std::abs(candidate_hp1 - candidate_hp2)};
    };

    int single_owner = oq_curve::pick_single_owner(demand_active, id(oq_curve_single_owner_hp), previous_hp1 > 0,
                                                   previous_hp2 > 0, lead_is_hp1);
    if (single_owner == 1 && !hp1_available)
      single_owner = hp2_available ? 2 : 0;
    else if (single_owner == 2 && !hp2_available)
      single_owner = hp1_available ? 1 : 0;
    const int hp1_max = maximum_level(true);
    const int hp2_max = maximum_level(false);
    const int owner_max = single_owner == 2 ? hp2_max : hp1_max;
    const float owner_capacity_w = level_power_w(single_owner != 2, owner_max);
    float duo_capacity_w = 0.0f;
    for (int first = 1; first <= hp1_max; ++first)
      for (int second = 1; second <= hp2_max; ++second) {
        if (std::abs(first - second) > 1) continue;
        const auto item = candidate(first, second, 0.0f);
        if (item.valid) duo_capacity_w = std::max(duo_capacity_w, item.power_w);
      }

    const bool demand_rundown = capped < previous_capped;
    const float target_power_w =
        demand_active ? oq_curve::phase_target_power_w(heat_phase, demand_u, owner_capacity_w, duo_capacity_w) : 0.0f;
    oq_curve::DispatchCandidate best_single;
    if (demand_active)
      for (int level = 1; level <= owner_max; ++level) {
        const auto item = candidate(single_owner == 1 ? level : 0, single_owner == 2 ? level : 0, target_power_w);
        if (oq_curve::better_dispatch_candidate(item, best_single, previous_hp1, previous_hp2)) best_single = item;
      }
    oq_curve::DispatchCandidate best_duo;
    if (demand_active)
      for (int first = 1; first <= hp1_max; ++first)
        for (int second = 1; second <= hp2_max; ++second) {
          if (std::abs(first - second) > 1) continue;
          const auto item = candidate(first, second, target_power_w);
          if (oq_curve::better_dispatch_candidate(item, best_duo, previous_hp1, previous_hp2)) best_duo = item;
        }

    const int dual_on_hold_min = std::max(1, static_cast<int>(std::lround(id(oq_dual_hp_enable_hold_min).state)));
    const int dual_off_hold_min = std::max(1, static_cast<int>(std::lround(id(oq_dual_hp_disable_hold_min).state)));
    const float dt_min = static_cast<float>(config.loop_ms) / 60000.0f;
    const uint32_t lead_last_start_ms = lead_is_hp1 ? id(hp1_last_start_ms) : id(hp2_last_start_ms);
    const bool startup_grace = oq_curve::elapsed_window_active(
        now_ms, lead_last_start_ms, static_cast<uint32_t>(std::max(0, tuning.dual_startup_grace_s)) * 1000UL);
    const float duo_enable_margin_w = heat_phase ? 700.0f : 450.0f;
    constexpr float duo_disable_margin_w = 250.0f;
    const float duo_enable_min_u = heat_phase ? 0.90f : 0.80f;
    const float duo_disable_max_u = heat_phase ? 0.70f : 0.55f;
    const bool single_saturated = best_single.valid && (best_single.hp1_level >= std::max(6, hp1_max - 1) ||
                                                        best_single.hp2_level >= std::max(6, hp2_max - 1));
    const bool duo_better =
        best_duo.valid && best_single.valid && best_duo.error_w + duo_enable_margin_w < best_single.error_w;
    const bool single_sufficient =
        best_single.valid && (!best_duo.valid || best_single.error_w <= best_duo.error_w + duo_disable_margin_w);
    const bool dual_defrost = demand_active && hp1_valve_defrost && hp2_valve_defrost;
    const bool emergency_dual = demand_active && best_duo.valid && heat_phase && !startup_grace && !demand_rundown &&
                                single_saturated && duo_better && std::isfinite(temperature_error_c) &&
                                temperature_error_c >= tuning.dual_emergency_temp_err_c && dispatch_u >= 0.95f;
    id(oq_dual_hp_emergency_hold_elapsed_accum_min) =
        emergency_dual ? id(oq_dual_hp_emergency_hold_elapsed_accum_min) + dt_min : 0.0f;
    const bool forced_capacity = id(oq_dual_hp_emergency_hold_elapsed_accum_min) >=
                                 static_cast<float>(std::max(1, tuning.dual_emergency_hold_min));
    const bool dual_on =
        demand_active && best_duo.valid && !startup_grace && !demand_rundown &&
        (forced_capacity || dual_defrost ||
         (duo_better && dispatch_u >= duo_enable_min_u && (heat_phase ? single_saturated : !single_sufficient) &&
          (!std::isfinite(temperature_error_c) || temperature_error_c >= -0.05f)));
    const bool dual_off =
        !demand_active || !best_duo.valid || (!id(oq_dual_hp_enabled) && !dual_on) ||
        (single_sufficient && dispatch_u <= duo_disable_max_u &&
         (!std::isfinite(temperature_error_c) || temperature_error_c <= tuning.dual_disable_temp_err_max_c));
    if (!demand_active) {
      oq_request::reset_dual_hold_state(id(oq_dual_hp_enabled), id(oq_dual_hp_enable_hold_elapsed_accum_min),
                                        id(oq_dual_hp_disable_hold_elapsed_accum_min),
                                        id(oq_dual_hp_emergency_hold_elapsed_accum_min));
    } else {
      id(oq_dual_hp_enable_hold_elapsed_accum_min) =
          dual_on ? id(oq_dual_hp_enable_hold_elapsed_accum_min) + dt_min : 0.0f;
      id(oq_dual_hp_disable_hold_elapsed_accum_min) =
          dual_off ? id(oq_dual_hp_disable_hold_elapsed_accum_min) + dt_min : 0.0f;
      if (!id(oq_dual_hp_enabled) && id(oq_dual_hp_enable_hold_elapsed_accum_min) >= dual_on_hold_min) {
        id(oq_dual_hp_enabled) = true;
        id(oq_dual_hp_disable_hold_elapsed_accum_min) = 0.0f;
      } else if (id(oq_dual_hp_enabled) && id(oq_dual_hp_disable_hold_elapsed_accum_min) >= dual_off_hold_min) {
        id(oq_dual_hp_enabled) = false;
        id(oq_dual_hp_enable_hold_elapsed_accum_min) = 0.0f;
      }
    }
    const bool dual_enabled =
        demand_active && best_duo.valid && (forced_capacity || dual_defrost || id(oq_dual_hp_enabled));
    if (!demand_active) {
      id(oq_curve_capacity_mode_code) = 0;
    } else if (dual_enabled) {
      id(oq_curve_single_owner_hp) = 0;
      hp1_level = best_duo.hp1_level;
      hp2_level = best_duo.hp2_level;
      id(oq_curve_capacity_mode_code) = 2;
    } else {
      id(oq_curve_single_owner_hp) = single_owner;
      owner = single_owner;
      hp1_level = best_single.valid ? best_single.hp1_level : 0;
      hp2_level = best_single.valid ? best_single.hp2_level : 0;
      id(oq_curve_capacity_mode_code) = best_single.valid && (hp1_level > 0 || hp2_level > 0) ? 1 : 0;
    }
#else
    oq_request::reset_dual_runtime_state(id(oq_dual_hp_enabled), id(oq_dual_hp_enable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_disable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_emergency_hold_elapsed_accum_min), id(oq_curve_single_owner_hp),
                                         id(oq_duo_request_hold_until_ms), 1);
    owner = demand_active && hp1_available ? 1 : 0;
    hp1_level =
        hp1_available ? std::max(0, std::min(level_cap, static_cast<int>(std::lround(demand_u * level_cap)))) : 0;
    id(oq_curve_capacity_mode_code) = hp1_level > 0 ? 1 : 0;
#endif

    const auto suspect_hold = oq_hp_candidate::preserve_active_topology_during_suspect(
        {hp1_level, hp2_level, hp1_candidate, hp2_candidate, demand_active});
    if (suspect_hold.active) {
      hp1_level = suspect_hold.hp1_level;
      hp2_level = suspect_hold.hp2_level;
      owner = suspect_hold.owner_hp;
      id(oq_curve_capacity_mode_code) = suspect_hold.capacity_mode;
      id(oq_curve_single_owner_hp) = owner;
    }
    id(oq_curve_request_total_level) = hp1_level + hp2_level;
    id(oq_curve_request_owner_hp) = owner;
    id(oq_curve_dispatch_hp1_level) = hp1_level;
    id(oq_curve_dispatch_hp2_level) = hp2_level;
  }

 private:
  static constexpr uint32_t kOilReturnHoldMs = 180000UL;
  static constexpr float kSetpointRaiseHysteresisC = 0.20f;

  oq_curve::ControlProfileTuning tuning_() const {
    return oq_curve::control_profile(
        id(oq_curve_control_profile).has_state() ? id(oq_curve_control_profile).current_option() : std::string());
  }

  static bool working_mode_heating_(float mode) { return std::isfinite(mode) && std::lround(mode) == 2; }

  bool room_temperature_fresh_(bool ot_fresh) const {
    if (!id(room_temp_source).has_state()) return false;
    const auto source = id(room_temp_source).current_option();
    return (source == "HA input" && id(room_temp_valid_ha).state && id(thermostat_room_temp_ha).has_state() &&
            std::isfinite(id(thermostat_room_temp_ha).state) && !id(oq_room_temp_selected_hold_active)) ||
           (source == "OT thermostat" && ot_fresh && id(ot_thermostat_room_temp).has_state() &&
            std::isfinite(id(ot_thermostat_room_temp).state)) ||
           (source == "CIC" && id(feed_ok).has_state() && id(feed_ok).state && id(cic_data_stale).has_state() &&
            !id(cic_data_stale).state && id(cic_room_temp).has_state() && std::isfinite(id(cic_room_temp).state)) ||
           (source == "API input" && id(api_input_room_temperature_valid).has_state() &&
            id(api_input_room_temperature_valid).state && id(api_input_room_temperature).has_state() &&
            std::isfinite(id(api_input_room_temperature).state)) ||
           (source == "MQTT" && id(mqtt_room_temperature_valid).has_state() && id(mqtt_room_temperature_valid).state &&
            id(mqtt_room_temperature).has_state() && std::isfinite(id(mqtt_room_temperature).state));
  }

  bool room_setpoint_fresh_(bool ot_fresh) const {
    if (!id(room_setpoint_source).has_state()) return false;
    const auto source = id(room_setpoint_source).current_option();
    return (source == "HA input" && id(room_setpoint_valid_ha).state && id(thermostat_setpoint_ha).has_state() &&
            std::isfinite(id(thermostat_setpoint_ha).state) && !id(oq_room_setpoint_selected_hold_active)) ||
           (source == "OT thermostat" && ot_fresh && id(ot_thermostat_room_setpoint).has_state() &&
            std::isfinite(id(ot_thermostat_room_setpoint).state)) ||
           (source == "CIC" && id(feed_ok).has_state() && id(feed_ok).state && id(cic_data_stale).has_state() &&
            !id(cic_data_stale).state && id(cic_room_setpoint).has_state() &&
            std::isfinite(id(cic_room_setpoint).state)) ||
           (source == "API input" && id(api_input_room_setpoint_valid).has_state() &&
            id(api_input_room_setpoint_valid).state && id(api_input_room_setpoint).has_state() &&
            std::isfinite(id(api_input_room_setpoint).state)) ||
           (source == "MQTT" && id(mqtt_room_setpoint_valid).has_state() && id(mqtt_room_setpoint_valid).state &&
            id(mqtt_room_setpoint).has_state() && std::isfinite(id(mqtt_room_setpoint).state));
  }

  void reset_control_() {
    oq_curve::reset_control_state(
        id(oq_curve_demand_continuous), id(oq_demand_curve), id(oq_curve_demand_pre_guardrail),
        id(oq_curve_heat_request_active), id(oq_curve_stop_arm_ms), id(oq_curve_off_since_ms),
        id(oq_curve_restart_inhibit_active), id(oq_curve_restart_blocked_by_room), id(oq_curve_regime_code));
    id(oq_curve_last_room_setpoint_c) = NAN;
    id(oq_curve_setpoint_raise_latch) = false;
    id(oq_curve_last_restart_reason) = 0;
  }

  void reset_outside_ema_() {
    oq_curve::reset_outside_ema_state(id(oq_curve_outside_ema_c), id(oq_curve_outside_ema_initialized),
                                      id(oq_curve_outside_ema_last_ms));
  }

  void reset_request_(int owner) {
    oq_curve::reset_request_state(id(oq_curve_request_last_loop_ms), id(oq_curve_request_total_level),
                                  id(oq_curve_request_owner_hp), id(oq_curve_dispatch_hp1_level),
                                  id(oq_curve_dispatch_hp2_level), id(oq_curve_capacity_mode_code));
    oq_request::reset_dual_runtime_state(id(oq_dual_hp_enabled), id(oq_dual_hp_enable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_disable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_emergency_hold_elapsed_accum_min), id(oq_curve_single_owner_hp),
                                         id(oq_duo_request_hold_until_ms), owner);
  }

  void log_demand_transition_(uint32_t now_ms, float room_c, float room_setpoint_c, bool oil_return_mask_active,
                              const oq_curve::ControlProfileTuning& tuning, const oq_curve::DemandState& previous,
                              const oq_curve::DemandDecision& demand) {
    if (oil_return_mask_active != this->last_oil_return_mask_active_) {
      ESP_LOGI("quatt.heatcurve", "Oil return hold %s", oil_return_mask_active ? "active" : "cleared");
      this->last_oil_return_mask_active_ = oil_return_mask_active;
    }
    if (demand.next.restart_inhibit_active != previous.restart_inhibit_active) {
      if (demand.next.restart_inhibit_active)
        ESP_LOGI("quatt.heatcurve", "Restart inhibit active: off-time hold (%u/%u s)",
                 static_cast<unsigned>((now_ms - demand.next.off_since_ms) / 1000UL),
                 static_cast<unsigned>(tuning.off_reentry_min_ms / 1000UL));
      else
        ESP_LOGI("quatt.heatcurve", "Restart inhibit cleared");
    }
    if (demand.next.restart_blocked_by_room != previous.restart_blocked_by_room) {
      if (demand.next.restart_blocked_by_room)
        ESP_LOGI("quatt.heatcurve", "Restart blocked: fresh room %.2f C is above %.2f C setpoint", room_c,
                 room_setpoint_c);
      else
        ESP_LOGI("quatt.heatcurve", "Warm-room restart block cleared");
    }
    if (demand.next.heat_request_active != previous.heat_request_active) {
      const char* reason =
          demand.next.heat_request_active
              ? (demand.restart_reason == oq_curve::RESTART_ROOM_DEMAND
                     ? "room demand"
                     : (demand.restart_reason == oq_curve::RESTART_USER_RAISE ? "user setpoint raise"
                                                                             : "restart band reached"))
              : (demand.stop_reason == oq_curve::STOP_LOW_LOAD ? "low-load release" : "normal stop");
      ESP_LOGI("quatt.heatcurve", "Heat request %s: %s", demand.next.heat_request_active ? "resumed" : "stopped",
               reason);
    }
  }

  bool last_oil_return_mask_active_{false};
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_heating_curve_runtime
#endif
