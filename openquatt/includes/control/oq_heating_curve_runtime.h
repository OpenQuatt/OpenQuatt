#pragma once

#include "../performance/oq_performance_observer_context.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "esphome/components/openquatt_decision_log/OpenQuattHeatingStop.h"

#include "../performance/hp_perf_frequency.h"
#include "oq_compressor_frequency_runtime.h"
#include "oq_heat_intent_runtime.h"
#include "oq_heating_curve_duo_floor_logic.h"
#include "oq_heating_curve_logic.h"
#include "oq_heating_supply_target_logic.h"
#include "oq_hp_candidate_logic.h"
#include "oq_sensor_source_runtime.h"
#include "oq_thermal_request_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_heating_curve_runtime {
struct DispatchConfig {
  uint32_t loop_ms;
  uint32_t minimum_off_ms;
  int demand_max_f;
};
class Runtime {
 public:
  void reset_profile() {
    this->stop_snapshot_ = {};
    this->reset_control_();
    id(oq_curve_oil_return_hold_until_ms) = 0;
    id(oq_curve_supply_external) = false;
    this->reset_outside_ema_();
    this->reset_request_(0);
  }
  void write_pid_output(float pid_output, int demand_max_f, bool ot_room_temperature_fresh,
                        bool ot_room_setpoint_fresh) {
    if (id(oq_heat_mode_code) != 1) {
      this->reset_control_();
      id(oq_curve_supply_external) = false;
      return;
    }
    this->last_pid_output_ = pid_output;
    const float target_c = this->effective_supply_target(id(oq_supply_target_temp).state);
    const float supply_c = id(oq_system_supply_temp).state;
    const auto tuning = this->tuning_();
    const float room_c = id(room_temp_selected).state;
    const float room_setpoint_c = id(room_setpoint_selected).state;
    const bool room_data_fresh = std::isfinite(room_c) && std::isfinite(room_setpoint_c) &&
                                 oq_heat_intent_runtime::room_temperature_fresh(ot_room_temperature_fresh) &&
                                 oq_heat_intent_runtime::room_setpoint_fresh(ot_room_setpoint_fresh);
    const uint32_t now_ms = static_cast<uint32_t>(millis());
    bool oil_return_active = id(hp1_prot_oil_return).state;
#if OQ_TOPOLOGY_DUO
    oil_return_active = oil_return_active || id(hp2_prot_oil_return).state;
#endif
    const auto oil_return = oq_curve::update_oil_return_hold(now_ms, oil_return_active,
                                                             id(oq_curve_oil_return_hold_until_ms), kOilReturnHoldMs);
    // Physical stand numbers are not the normalized PID demand scale.
    const auto demand_frequency = oq_frequency_runtime::capture();
    int applied_total = oq_odu::normalized_heating_demand_level(
        demand_frequency.configured_v2, demand_frequency.snapshot(true), id(hp1_last_applied_level));
#if OQ_TOPOLOGY_DUO
    applied_total += oq_odu::normalized_heating_demand_level(
        demand_frequency.configured_v2, demand_frequency.snapshot(false), id(hp2_last_applied_level));
#endif
    oq_curve::DemandState previous{id(oq_curve_heat_request_active),
                                   id(oq_curve_stop_arm_ms),
                                   id(oq_curve_off_since_ms),
                                   id(oq_curve_restart_inhibit_active),
                                   id(oq_curve_restart_blocked_by_room),
                                   id(oq_curve_regime_code)};
    const bool compressor_active = id(hp1_last_applied_level) > 0
#if OQ_TOPOLOGY_DUO
                                   || id(hp2_last_applied_level) > 0
#endif
        ;
    const auto intent =
        oq_heat_intent_runtime::evaluate(now_ms, compressor_active, tuning.room_resume_heat_c, 0,
                                         ot_room_temperature_fresh, ot_room_setpoint_fresh, this->intent_state_);
    this->intent_state_ = intent.next;
    id(oq_curve_fast_intent_code) = static_cast<int>(intent.reason);
    if (intent.setpoint_raise_cancelled && this->setpoint_started_request_ && !compressor_active) {
      previous.heat_request_active = false;
      previous.stop_arm_ms = 0;
      previous.off_since_ms = oq_curve::timestamp_ms(now_ms);
      previous.regime_code = 0;
      this->setpoint_started_request_ = false;
    }
    const auto demand = oq_curve::decide_demand({now_ms, pid_output, target_c, supply_c, room_c, room_setpoint_c,
                                                 room_data_fresh, oil_return.mask_active, applied_total, demand_max_f},
                                                tuning, previous);
    namespace stop = esphome::openquatt_decision_log::heating_stop;
    if (stop::begin_capture(this->stop_snapshot_, now_ms,
                            stop::curve_demand_reason(demand.valid, static_cast<uint8_t>(demand.stop_reason)),
                            !demand.next.heat_request_active)) {
      auto& stop_snapshot = this->stop_snapshot_;
      stop_snapshot.room_c = room_data_fresh ? stop::encode(room_c, 100.0f) : stop::UNKNOWN_VALUE;
      stop_snapshot.room_stop_c = room_data_fresh
                                      ? stop::encode(room_setpoint_c + (demand.stop_reason == oq_curve::STOP_LOW_LOAD
                                                                            ? -tuning.room_resume_heat_c
                                                                            : tuning.room_overheat_off_c),
                                                     100.0f)
                                      : stop::UNKNOWN_VALUE;
      stop_snapshot.room_restart_c =
          room_data_fresh ? stop::encode(room_setpoint_c - tuning.room_resume_heat_c, 100.0f) : stop::UNKNOWN_VALUE;
      stop_snapshot.supply_c = stop::encode(supply_c, 100.0f);
      stop_snapshot.supply_stop_c =
          demand.valid ? stop::encode(target_c + (demand.stop_reason == oq_curve::STOP_LOW_LOAD
                                                      ? 0.25f
                                                      : std::max(tuning.stop_delta_c, tuning.start_delta_c + 0.10f)),
                                      100.0f)
                       : stop::UNKNOWN_VALUE;
      stop_snapshot.supply_restart_c =
          demand.valid ? stop::encode(target_c - std::max(tuning.restart_delta_c, tuning.start_delta_c + 0.05f), 100.0f)
                       : stop::UNKNOWN_VALUE;
    }
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
    if (!previous.heat_request_active && demand.next.heat_request_active &&
        intent.reason == oq_heat_intent::SETPOINT_RAISE)
      this->setpoint_started_request_ = true;
    if (compressor_active || !demand.next.heat_request_active) this->setpoint_started_request_ = false;
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
  float supply_target(uint32_t ha_modifier_stale_s) const {
    const std::array<oq_curve::CurvePoint, 6> points{{
        {-20.0f, id(curve_tsupply_m20).state},
        {-10.0f, id(curve_tsupply_m10).state},
        {0.0f, id(curve_tsupply_0).state},
        {5.0f, id(curve_tsupply_5).state},
        {10.0f, id(curve_tsupply_10).state},
        {15.0f, id(curve_tsupply_15).state},
    }};
    const float modifier_c =
        oq_sensor_source::runtime().heating_curve_modifier(static_cast<uint32_t>(millis()), ha_modifier_stale_s);
    const auto target =
        oq_curve::target_breakdown(id(oq_curve_outside_temp_filtered).state, id(curve_fallback_supply_temp).state,
                                   points, modifier_c, id(room_temp_selected).state, id(room_setpoint_selected).state,
                                   this->tuning_(), id(max_water_temp_limit_c).state);
    id(oq_curve_base_target_c) = target.base_c;
    id(oq_curve_modifier_applied_k) = target.modifier_c;
    id(oq_curve_room_trim_k) = target.room_trim_c;
    return target.selected_c;
  }
  float effective_supply_target(float local_curve_c) const {
    // An external target replaces the local curve output as-is (no second
    // room-trim); downstream limits, PID, demand and dispatch are untouched.
    const bool has_selected = id(heating_supply_target_selected).has_state();
    const float external_c = has_selected ? id(heating_supply_target_selected).state : NAN;
    const float max_water_c = id(max_water_temp_limit_c).has_state() ? id(max_water_temp_limit_c).state : NAN;
    const float ceiling_c = std::isfinite(max_water_c) ? max_water_c : 70.0f;
    const auto effective = oq_heating_supply::select_effective_target(
        local_curve_c, external_c, has_selected && std::isfinite(external_c), 20.0f, ceiling_c);
    id(oq_curve_supply_external) = effective.external;
    return effective.supply_target_c;
  }

  const esphome::openquatt_decision_log::heating_stop::Snapshot& stop_snapshot() const { return this->stop_snapshot_; }
  uint8_t dispatch_stop_reason() const { return this->dispatch_stop_reason_; }

  void strategy_tick(int demand_max_f, bool ot_room_temperature_fresh, bool ot_room_setpoint_fresh) {
    const bool active = id(oq_control_mode_code) != 5 && id(oq_heat_mode_code) == 1;
    const uint32_t now_ms = static_cast<uint32_t>(millis());
    if (!active) {
      this->stop_snapshot_ = {};
      if (id(oq_heating_curve_pid).mode != CLIMATE_MODE_OFF) {
        auto call = id(oq_heating_curve_pid).make_call();
        call.set_mode(CLIMATE_MODE_OFF);
        call.perform();
      }
      this->reset_control_();
      id(oq_curve_supply_external) = false;
      return;
    }
    const float target_c = this->effective_supply_target(id(oq_supply_target_temp).state);
    const float supply_c = id(oq_system_supply_temp).state;
    if (!std::isfinite(target_c) || !std::isfinite(supply_c)) {
      this->reset_control_();
      this->stop_snapshot_ = {};
      this->stop_snapshot_.captured_ms = now_ms;
      this->stop_snapshot_.reason = esphome::openquatt_decision_log::REASON_HEATING_INPUT_INVALID;
      this->stop_snapshot_.supply_c = esphome::openquatt_decision_log::heating_stop::encode(supply_c, 100.0f);
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
    if (std::isfinite(this->last_pid_output_))
      this->write_pid_output(this->last_pid_output_, demand_max_f, ot_room_temperature_fresh, ot_room_setpoint_fresh);
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
    if (!std::isfinite(this->effective_supply_target(id(oq_supply_target_temp).state)) ||
        !std::isfinite(id(oq_system_supply_temp).state))
      return true;
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
    auto hp1_candidate =
        oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(1), id(hp1_last_applied_level));
    hp1_candidate.minimum_off_ready = oq_hp_candidate::minimum_off_ready(
        now_ms, id(hp1_last_stop_ms), config.minimum_off_ms, id(hp1_last_applied_level));
    const bool hp1_available = oq_hp_candidate::may_serve_candidate(hp1_candidate);
#if OQ_TOPOLOGY_DUO
    auto hp2_candidate =
        oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(2), id(hp2_last_applied_level));
    hp2_candidate.minimum_off_ready = oq_hp_candidate::minimum_off_ready(
        now_ms, id(hp2_last_stop_ms), config.minimum_off_ms, id(hp2_last_applied_level));
    const bool hp2_available = oq_hp_candidate::may_serve_candidate(hp2_candidate);
#else
    const oq_hp_candidate::HpCandidateState hp2_candidate;
    const bool hp2_available = false;
#endif
    const bool demand_active_now = id(oq_curve_heat_request_active);
    const bool urgent = !this->dispatch_snapshot_initialized_ || demand_active_now != this->last_dispatch_demand_ ||
                        (demand_active_now &&
                         (hp1_available != this->last_hp1_available_ || hp2_available != this->last_hp2_available_));
    this->dispatch_snapshot_initialized_ = true;
    this->last_dispatch_demand_ = demand_active_now;
    this->last_hp1_available_ = hp1_available;
    this->last_hp2_available_ = hp2_available;
    if (!urgent && !oq_curve::cadence_due(now_ms, id(oq_curve_request_last_loop_ms), config.loop_ms)) return;
    this->dispatch_stop_reason_ = esphome::openquatt_decision_log::REASON_HEATING_NO_CANDIDATE;
    id(oq_curve_request_last_loop_ms) = oq_curve::timestamp_ms(now_ms);

    constexpr int level_cap = oq_odu::EXTENDED_PHYSICAL_LEVEL_MAX;
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
    const bool allow_low_supply_boundary_estimate = id(oq_cold_start_session_active) && !id(oq_cold_start_hp_blocked);
    id(oq_P_hp_cap_w) = 0.0f;
    id(oq_P_deficit_w) = 0.0f;
    int owner = 0;
    int hp1_level = 0;
    int hp2_level = 0;
    const float demand_u = oq_curve::power_capped_demand_u(id(oq_curve_demand_continuous), capped, config.demand_max_f);
    const bool demand_active = demand_u > 0.0f;
#if OQ_TOPOLOGY_DUO
    const bool hp1_valve_defrost = id(hp1_4_way_valve).state;
    const bool hp2_valve_defrost = id(hp2_4_way_valve).state;
    const float dispatch_u = config.demand_max_f > 0 ? static_cast<float>(capped) / config.demand_max_f : 0.0f;
    const auto tuning = this->tuning_();
    const float target_c = this->effective_supply_target(id(oq_supply_target_temp).state);
    const float supply_c = id(oq_system_supply_temp).state;
    const float temperature_error_c = std::isfinite(target_c) && std::isfinite(supply_c) ? target_c - supply_c : NAN;
    const bool heat_phase = demand_active && id(oq_curve_regime_code) == 1;
    const int previous_capped = id(oq_demand_filtered_prev);
    bool hp1_model_available = false;
    bool hp2_model_available = false;
    std::array<float, level_cap + 1> hp1_power{}, hp2_power{};
    const auto build_power = [&](bool hp1, auto& powers, bool& model_available) {
      powers.fill(NAN);
      powers[0] = 0.0f;
      for (int level = 1; level <= frequency.automatic_level_limit(hp1, 2); ++level) {
        const auto prediction =
            oq_perf::predict_candidate(frequency, frequency.performance_variant(hp1), hp1, level,
                                       id(outside_temp_selected).state, target_c, allow_low_supply_boundary_estimate);
        model_available |= prediction.runtime_frequency_known && prediction.performance.available;
        if (prediction.usable_for_running_optimization()) powers[level] = prediction.performance.pth_w;
      }
    };
    build_power(true, hp1_power, hp1_model_available);
    build_power(false, hp2_power, hp2_model_available);
    oq_curve_duo::Input comparison;
    comparison.now_ms = now_ms;
    comparison.lead_last_start_ms = lead_is_hp1 ? id(hp1_last_start_ms) : id(hp2_last_start_ms);
    comparison.loop_ms = config.loop_ms;
    comparison.capped = capped;
    comparison.previous_capped = previous_capped;
    comparison.previous_hp1 = id(hp1_last_applied_level);
    comparison.previous_hp2 = id(hp2_last_applied_level);
    comparison.single_owner = id(oq_curve_single_owner_hp);
    comparison.demand_u = demand_u;
    comparison.dispatch_u = dispatch_u;
    comparison.temperature_error_c = temperature_error_c;
    comparison.demand_active = demand_active;
    comparison.heat_phase = heat_phase;
    comparison.lead_hp1 = lead_is_hp1;
    comparison.hp1 = hp1_candidate;
    comparison.hp2 = hp2_candidate;
    comparison.hp1_model_available = hp1_model_available;
    comparison.hp2_model_available = hp2_model_available;
    comparison.hp1_valve_defrost = hp1_valve_defrost;
    comparison.hp2_valve_defrost = hp2_valve_defrost;
    comparison.dual_on_hold_min = std::max(1, static_cast<int>(std::lround(id(oq_dual_hp_enable_hold_min).state)));
    comparison.dual_off_hold_min = std::max(1, static_cast<int>(std::lround(id(oq_dual_hp_disable_hold_min).state)));
    comparison.tuning = tuning;
    for (int level = 0; level <= level_cap; ++level) {
      comparison.hz[0][level] =
          std::max(0, oq_odu::frequency_for_physical_level(frequency.snapshot(true).heating, level));
      comparison.hz[1][level] =
          std::max(0, oq_odu::frequency_for_physical_level(frequency.snapshot(false).heating, level));
    }
    const oq_curve_duo::State previous_state{id(oq_dual_hp_enabled), id(oq_dual_hp_enable_hold_elapsed_accum_min),
                                             id(oq_dual_hp_disable_hold_elapsed_accum_min),
                                             id(oq_dual_hp_emergency_hold_elapsed_accum_min)};
    const auto decision = oq_curve_duo::decide(comparison, previous_state, [&](int first, int second) {
      const float value = hp1_power[first] + hp2_power[second];
      return oq_power_house_dispatch::PairPower{value, NAN, std::isfinite(value)};
    });
    id(oq_dual_hp_enabled) = decision.next.dual_enabled;
    id(oq_dual_hp_enable_hold_elapsed_accum_min) = decision.next.on_min;
    id(oq_dual_hp_disable_hold_elapsed_accum_min) = decision.next.off_min;
    id(oq_dual_hp_emergency_hold_elapsed_accum_min) = decision.next.emergency_min;
    id(oq_curve_single_owner_hp) = decision.single_owner;
    id(oq_curve_capacity_mode_code) = decision.capacity_mode;
    hp1_level = decision.hp1_level;
    hp2_level = decision.hp2_level;
    owner = decision.owner;
    if (decision.single_branch)
      this->dispatch_stop_reason_ = esphome::openquatt_decision_log::heating_stop::curve_single_reason(
          decision.regular_single_stop, decision.duo_valid);
    if (oq_performance_observer::curve_hook != nullptr)
      oq_performance_observer::curve_hook(comparison, previous_state, {hp1_power, hp2_power}, decision,
                                          {id(outside_temp_selected).state, target_c});
#else
    oq_request::reset_dual_runtime_state(id(oq_dual_hp_enabled), id(oq_dual_hp_enable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_disable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_emergency_hold_elapsed_accum_min), id(oq_curve_single_owner_hp),
                                         id(oq_duo_request_hold_until_ms), 1);
    owner = demand_active && hp1_available ? 1 : 0;
    std::array<float, level_cap + 1> powers{};
    powers.fill(NAN);
    const float target_c = this->effective_supply_target(id(oq_supply_target_temp).state);
    float capacity_w = 0.0f;
    bool model_available = false;
    for (int level = 1; level <= frequency.automatic_level_limit(true, 2); ++level) {
      const auto prediction =
          oq_perf::predict_candidate(frequency, frequency.performance_variant(true), true, level,
                                     id(outside_temp_selected).state, target_c, allow_low_supply_boundary_estimate);
      model_available |= prediction.runtime_frequency_known && prediction.performance.available;
      if (prediction.usable_for_running_optimization()) {
        powers[level] = prediction.performance.pth_w;
        capacity_w = std::max(capacity_w, powers[level]);
      }
    }
    const float target_power_w = demand_u * capacity_w;
    oq_curve::DispatchCandidate best;
    if (demand_active && hp1_available) {
      for (int level = 1; level <= frequency.automatic_level_limit(true, 2); ++level) {
        if (!std::isfinite(powers[level])) continue;
        const oq_curve::DispatchCandidate item{true, level, 0, powers[level], std::fabs(powers[level] - target_power_w),
                                               1,    0};
        if (oq_curve::better_dispatch_candidate(item, best, id(hp1_last_applied_level), 0)) best = item;
      }
      hp1_level = best.valid ? best.hp1_level : 0;
      if (!model_available)
        hp1_level =
            oq_hp_candidate::preserve_active_topology_without_model(true, hp1_candidate, hp2_candidate).hp1_level;
    }
    id(oq_P_hp_cap_w) = capacity_w;
    id(oq_P_deficit_w) = best.valid ? std::max(0.0f, target_power_w - best.power_w) : NAN;
    id(oq_curve_capacity_mode_code) = hp1_level > 0 ? 1 : 0;
#endif

#if !OQ_TOPOLOGY_DUO
    const auto suspect_hold = oq_hp_candidate::preserve_active_topology_during_suspect(
        {hp1_level, hp2_level, hp1_candidate, hp2_candidate, demand_active});
    if (suspect_hold.active) {
      hp1_level = suspect_hold.hp1_level;
      hp2_level = suspect_hold.hp2_level;
      owner = suspect_hold.owner_hp;
      id(oq_curve_capacity_mode_code) = suspect_hold.capacity_mode;
      id(oq_curve_single_owner_hp) = owner;
    }
#endif
    id(oq_curve_request_total_level) = hp1_level + hp2_level;
    id(oq_curve_request_owner_hp) = owner;
    id(oq_curve_dispatch_hp1_level) = hp1_level;
    id(oq_curve_dispatch_hp2_level) = hp2_level;
  }

 private:
  uint8_t dispatch_stop_reason_{0};
  esphome::openquatt_decision_log::heating_stop::Snapshot stop_snapshot_;
  static constexpr uint32_t kOilReturnHoldMs = 180000UL;
  oq_curve::ControlProfileTuning tuning_() const {
    return oq_curve::control_profile(
        id(oq_curve_control_profile).has_state() ? id(oq_curve_control_profile).current_option() : std::string());
  }
  static bool working_mode_heating_(float mode) { return std::isfinite(mode) && std::lround(mode) == 2; }
  void reset_control_() {
    this->last_pid_output_ = NAN;
    this->intent_state_ = {};
    this->setpoint_started_request_ = false;
    id(oq_curve_fast_intent_code) = 0;
    oq_curve::reset_control_state(
        id(oq_curve_demand_continuous), id(oq_demand_curve), id(oq_curve_demand_pre_guardrail),
        id(oq_curve_heat_request_active), id(oq_curve_stop_arm_ms), id(oq_curve_off_since_ms),
        id(oq_curve_restart_inhibit_active), id(oq_curve_restart_blocked_by_room), id(oq_curve_regime_code));
  }
  void reset_outside_ema_() {
    oq_curve::reset_outside_ema_state(id(oq_curve_outside_ema_c), id(oq_curve_outside_ema_initialized),
                                      id(oq_curve_outside_ema_last_ms));
  }
  void reset_request_(int owner) {
    this->dispatch_snapshot_initialized_ = false;
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
              ? (demand.restart_reason == oq_curve::RESTART_ROOM_DEMAND ? "room demand" : "restart band reached")
              : (demand.stop_reason == oq_curve::STOP_LOW_LOAD ? "low-load release" : "normal stop");
      ESP_LOGI("quatt.heatcurve", "Heat request %s: %s", demand.next.heat_request_active ? "resumed" : "stopped",
               reason);
    }
  }

  bool last_oil_return_mask_active_{false};
  float last_pid_output_{NAN};
  oq_heat_intent::State intent_state_;
  bool setpoint_started_request_{false};
  bool dispatch_snapshot_initialized_{false};
  bool last_dispatch_demand_{false};
  bool last_hp1_available_{false};
  bool last_hp2_available_{false};
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_heating_curve_runtime
#endif
