#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "../performance/hp_perf_frequency.h"
#include "oq_compressor_frequency_runtime.h"
#include "oq_heat_intent_runtime.h"
#include "oq_power_house_demand_logic.h"
#include "oq_power_house_dispatch_logic.h"
#include "oq_power_house_run_extension_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_power_house_runtime {

struct TickConfig {
  uint32_t loop_ms;
  uint32_t minimum_off_ms;
  uint32_t hp_water_temp_stale_ms;
  int demand_max_f;
  float temperature_guard_c;
  float defrost_power_factor;
  float optimizer_penalty_per_w;
  float topology_power_margin_w;
  float topology_heat_advantage_w;
  int defrost_comp_min_f;
  int defrost_comp_boost_steps;
  bool ot_room_temperature_fresh;
  bool ot_room_setpoint_fresh;
  float run_extension_restart_hysteresis_c;
};

class Runtime {
 public:
  std::string response_profile() const {
    const float rise = id(ph_demand_rise_time_min).state;
    const float fall = id(ph_demand_fall_time_min).state;
    if (this->near_(rise, 12.0f) && this->near_(fall, 5.0f)) return "Calm";
    if (this->near_(rise, 8.0f) && this->near_(fall, 3.0f)) return "Balanced";
    if (this->near_(rise, 5.0f) && this->near_(fall, 2.0f)) return "Responsive";
    return "Custom";
  }

  void set_response_profile(const std::string& profile) {
    if (profile == "Calm") {
      this->set_number_(id(ph_demand_rise_time_min), 12.0f);
      this->set_number_(id(ph_demand_fall_time_min), 5.0f);
    } else if (profile == "Balanced") {
      this->set_number_(id(ph_demand_rise_time_min), 8.0f);
      this->set_number_(id(ph_demand_fall_time_min), 3.0f);
    } else if (profile == "Responsive") {
      this->set_number_(id(ph_demand_rise_time_min), 5.0f);
      this->set_number_(id(ph_demand_fall_time_min), 2.0f);
    }
  }

  void tick(const TickConfig& config) {
    const bool active = id(oq_strategy_active_code) == 3 && id(oq_control_mode_code) != 5 && id(oq_heat_mode_code) != 1;
    if (!active) {
      this->reset();
      return;
    }

    const uint32_t now_ms = static_cast<uint32_t>(millis());
    if (id(oq_strategy_output_source_code) != 3) this->dispatch_state_ = {};
    const bool hp1_valve_defrost = id(hp1_4_way_valve).state;
    const bool hp1_oil_return = id(hp1_prot_oil_return).state;
#if OQ_TOPOLOGY_DUO
    const bool hp2_valve_defrost = id(hp2_4_way_valve).state;
    const bool hp2_oil_return = id(hp2_prot_oil_return).state;
    const int hp2_applied_level = id(hp2_last_applied_level);
#else
    const bool hp2_valve_defrost = false;
    const bool hp2_oil_return = false;
    const int hp2_applied_level = 0;
#endif
    this->dispatch_state_ = oq_power_house_dispatch::observe_protection(
        this->dispatch_state_, {now_ms, hp1_valve_defrost, hp2_valve_defrost, hp1_oil_return || hp2_oil_return,
                                id(hp1_last_applied_level), hp2_applied_level});

    const auto cadence = oq_power_house::decide_cadence(now_ms, id(oq_ph_request_last_loop_ms), config.loop_ms);
    if (!cadence.due) return;
    id(oq_ph_request_last_loop_ms) = now_ms == 0 ? UINT32_MAX : now_ms;

    const oq_power_house::DemandInput demand_input{
        now_ms,
        id(outside_temp_selected).state,
        id(house_cold_temp_c).state,
        id(house_zero_power_temp_c).state,
        id(house_rated_power_w).state,
        id(room_temp_selected).state,
        id(room_setpoint_selected).state,
        id(external_heat_demand_selected).state,
        id(oq_water_temp_limit_factor),
        id(external_heat_demand_selected).has_state(),
    };
    const oq_power_house::DemandTuning demand_tuning{
        config.temperature_guard_c,
        id(ph_kp_w_per_k).state,
        id(ph_comfort_band_below_c).state,
        id(ph_comfort_band_above_c).state,
        id(ph_demand_rise_time_min).state,
        id(ph_demand_fall_time_min).state,
        config.demand_max_f,
    };
    const int applied_total =
        std::max(0, static_cast<int>(id(hp1_last_applied_level))) + std::max(0, static_cast<int>(hp2_applied_level));
    if (applied_total > 0 && this->fast_floor_w_ > 0.0f) {
      this->demand_state_.last_w = std::max(this->demand_state_.last_w, this->fast_floor_w_);
      this->fast_floor_w_ = 0.0f;
    }
    const auto demand = oq_power_house::decide_demand(demand_input, demand_tuning, this->demand_state_);
    this->demand_state_ = demand.next;
    float requested_w = demand.requested_w;
    float next_last_w = demand.next.last_w;
    int raw_demand = demand.raw_demand;
    const auto intent = oq_heat_intent_runtime::evaluate(
        now_ms, applied_total > 0, std::max(0.0f, demand_tuning.comfort_below_c), 10000UL,
        config.ot_room_temperature_fresh, config.ot_room_setpoint_fresh, this->intent_state_);
    this->intent_state_ = intent.next;
    // An interrupted recovery re-enters through the normal confirmed path.
    id(oq_ph_fast_intent_code) = intent.fast_start ? static_cast<int>(intent.reason) : 0;
    id(oq_phouse_last_ms) = demand.next.last_ms;
    id(oq_phouse_comfort_memory_c) = demand.next.comfort_memory_c;
    id(oq_phouse_demand_external) = demand.external;

#if OQ_TOPOLOGY_DUO
    const bool lead_is_hp1 = id(hp1_minutes) <= id(hp2_minutes);
    constexpr bool duo = true;
#else
    const bool lead_is_hp1 = true;
    constexpr bool duo = false;
#endif
    id(oq_last_lead_hp) = lead_is_hp1 ? 1 : 2;

    const auto frequency = oq_frequency_runtime::capture();
    const float outside_c = id(outside_temp_selected).state;
    const float system_supply_c = id(oq_system_supply_temp).state;
    const bool allow_low_supply_boundary_estimate = id(oq_cold_start_session_active) && !id(oq_cold_start_hp_blocked);
    auto hp1_candidate =
        oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(1), id(hp1_last_applied_level));
    hp1_candidate.minimum_off_ready = oq_hp_candidate::minimum_off_ready(
        now_ms, id(hp1_last_stop_ms), config.minimum_off_ms, id(hp1_last_applied_level));
    const bool hp1_defrost_active = id(hp1_defrost).state;
#if OQ_TOPOLOGY_DUO
    auto hp2_candidate = oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(2), hp2_applied_level);
    hp2_candidate.minimum_off_ready =
        oq_hp_candidate::minimum_off_ready(now_ms, id(hp2_last_stop_ms), config.minimum_off_ms, hp2_applied_level);
    const bool hp2_defrost_active = id(hp2_defrost).state;
#else
    const oq_hp_candidate::HpCandidateState hp2_candidate;
    const bool hp2_defrost_active = false;
#endif

    const auto fresh_hp_outlet = [&](bool online, uint32_t last_update_ms, bool has_state, float value) {
      const bool fresh = online && last_update_ms > 0 &&
                         static_cast<uint32_t>(now_ms - last_update_ms) <= config.hp_water_temp_stale_ms;
      return fresh && has_state && std::isfinite(value) ? value : NAN;
    };
    const float hp1_outlet_c = fresh_hp_outlet(id(hp1_is_online), id(hp1_water_out_temp_last_update_ms),
                                               id(hp1_water_out_temp).has_state(), id(hp1_water_out_temp).state);
#if OQ_TOPOLOGY_DUO
    const float hp2_outlet_c = fresh_hp_outlet(id(hp2_is_online), id(hp2_water_out_temp_last_update_ms),
                                               id(hp2_water_out_temp).has_state(), id(hp2_water_out_temp).state);
#else
    const float hp2_outlet_c = NAN;
#endif
    const auto performance_supply = oq_power_house_dispatch::select_performance_supply(
        {system_supply_c, hp1_outlet_c, hp2_outlet_c, oq_hp_candidate::may_serve_candidate(hp1_candidate),
         duo && oq_hp_candidate::may_serve_candidate(hp2_candidate)});
    const float supply_c = performance_supply.supply_c;
    const bool performance_valid = std::isfinite(outside_c) && performance_supply.valid;

    float defrost_factor = config.defrost_power_factor;
    if (!std::isfinite(defrost_factor)) defrost_factor = 0.55f;
    defrost_factor = std::max(0.10f, std::min(1.00f, defrost_factor));
    bool hp1_model_available = false;
    bool hp2_model_available = false;
    bool hp1_runnable_candidate = false;
    bool hp2_runnable_candidate = false;
    const auto build_hp = [&](oq_power_house_dispatch::HpInput& result, bool hp1,
                              const oq_hp_candidate::HpCandidateState& candidate, bool defrost, bool valve_defrost) {
      result.candidate = candidate;
      result.defrost = defrost;
      result.valve_defrost = valve_defrost;
      result.levels[0] = {true, true, true, 0.0f, 0.0f};
      for (int level = 1; level <= oq_power_house_dispatch::kMaxLevel; ++level) {
        const auto prediction = oq_perf::predict_candidate(frequency, frequency.performance_variant(hp1), hp1, level,
                                                           outside_c, supply_c, allow_low_supply_boundary_estimate);
        const bool allowed = prediction.frequency_policy_allowed;
        float thermal_w = performance_valid ? prediction.performance.pth_w : NAN;
        float electrical_w = performance_valid ? prediction.performance.pel_w : NAN;
        if (valve_defrost && std::isfinite(thermal_w)) thermal_w *= defrost_factor;
        const bool thermal_valid = std::isfinite(thermal_w) && thermal_w >= 0.0f;
        const bool electrical_valid = std::isfinite(electrical_w) && electrical_w >= 0.0f;
        result.levels[level] = {allowed, thermal_valid, electrical_valid, thermal_w, electrical_w};
        bool& model_available = hp1 ? hp1_model_available : hp2_model_available;
        bool& runnable_candidate = hp1 ? hp1_runnable_candidate : hp2_runnable_candidate;
        model_available |= prediction.runtime_frequency_known && prediction.performance.available;
        runnable_candidate |= allowed && thermal_valid && electrical_valid;
      }
    };

    oq_power_house_dispatch::DispatchInput dispatch_input{now_ms, raw_demand,        requested_w,
                                                          duo,    performance_valid, lead_is_hp1};
    build_hp(dispatch_input.hp1, true, hp1_candidate, hp1_defrost_active, hp1_valve_defrost);
#if OQ_TOPOLOGY_DUO
    build_hp(dispatch_input.hp2, false, hp2_candidate, hp2_defrost_active, hp2_valve_defrost);
#endif
    const bool active_model_missing =
        (hp1_candidate.previous_applied_level > 0 && !hp1_candidate.must_stop && !hp1_model_available) ||
        (duo && hp2_candidate.previous_applied_level > 0 && !hp2_candidate.must_stop && !hp2_model_available);
    const bool any_servable_candidate =
        (oq_hp_candidate::may_serve_candidate(hp1_candidate) && hp1_runnable_candidate) ||
        (duo && oq_hp_candidate::may_serve_candidate(hp2_candidate) && hp2_runnable_candidate);
    dispatch_input.performance_valid = performance_valid && any_servable_candidate && !active_model_missing;
    float minimum_viable_w = NAN;
    const auto include_minimum = [&](const oq_power_house_dispatch::HpInput& hp) {
      if (!oq_hp_candidate::may_serve_candidate(hp.candidate)) return;
      for (int level = 1; level <= oq_power_house_dispatch::kMaxLevel; ++level) {
        const auto& estimate = hp.levels[level];
        if (!estimate.allowed || !estimate.thermal_valid || !std::isfinite(estimate.thermal_w) ||
            estimate.thermal_w <= 0.0f)
          continue;
        minimum_viable_w =
            std::isfinite(minimum_viable_w) ? std::min(minimum_viable_w, estimate.thermal_w) : estimate.thermal_w;
        break;
      }
    };
    include_minimum(dispatch_input.hp1);
#if OQ_TOPOLOGY_DUO
    include_minimum(dispatch_input.hp2);
#endif
    // A room recovery follows a room-demand start only; it is released halfway
    // through the restart band. It therefore cannot turn every below-setpoint
    // interval into an implicit keep-running-at-minimum mode.
    if ((intent.fast_start || intent.room_recovery_active) && std::isfinite(minimum_viable_w) &&
        id(oq_water_temp_limit_factor) >= 0.999f) {
      requested_w = std::max(requested_w, minimum_viable_w);
      next_last_w = std::max(next_last_w, requested_w);
      this->fast_floor_w_ = requested_w;
      const float rated_w = id(house_rated_power_w).state;
      if (std::isfinite(rated_w) && rated_w > 0.0f && config.demand_max_f > 0)
        raw_demand = std::max(
            raw_demand,
            std::min(config.demand_max_f, static_cast<int>(std::ceil(requested_w * config.demand_max_f / rated_w))));
    } else if (applied_total == 0) {
      this->fast_floor_w_ = 0.0f;
    }
    // #608 run extension: keep base (house need) and effective (HP target) apart.
    // Power House decides what the house needs; #608 may only decide what the
    // heat pump minimally delivers. The floor is never fed back into the house
    // model and never counts as house deficit for boiler assist.
    const float base_requested_w = requested_w;
    const int base_raw_demand = raw_demand;
    const float base_last_w = next_last_w;
    const float room_c = id(room_temp_selected).state;
    const float setpoint_c = id(room_setpoint_selected).state;
    const bool run_ext_enabled = id(ph_run_extension_enabled).state;
    float stop_margin_c = 0.5f;
    if (id(ph_run_extension_stop_margin_c).has_state() && std::isfinite(id(ph_run_extension_stop_margin_c).state))
      stop_margin_c = std::max(0.1f, std::min(1.0f, id(ph_run_extension_stop_margin_c).state));
    const bool room_inputs_valid = std::isfinite(room_c) && std::isfinite(setpoint_c) &&
                                   oq_heat_intent_runtime::room_temperature_fresh(config.ot_room_temperature_fresh) &&
                                   oq_heat_intent_runtime::room_setpoint_fresh(config.ot_room_setpoint_fresh);
    const bool heating_allowed = id(heating_enable_valid).has_state() && id(heating_enable_valid).state &&
                                 id(heating_enable_selected).has_state() && id(heating_enable_selected).state;
    // Defrost and oil-return continuity only: a topology-optimizer hold must
    // never count as an active #608 run, otherwise #608 could request Pmin
    // from a true standstill after both compressors already stopped.
    const bool protection_hold = oq_power_house_dispatch::tail_active(this->dispatch_state_.hp1_defrost, now_ms,
                                                                      oq_power_house_dispatch::kDefrostHoldMs) ||
                                 oq_power_house_dispatch::tail_active(this->dispatch_state_.hp2_defrost, now_ms,
                                                                      oq_power_house_dispatch::kDefrostHoldMs) ||
                                 oq_power_house_dispatch::tail_active(this->dispatch_state_.oil_return, now_ms,
                                                                      oq_power_house_dispatch::kOilReturnHoldMs);
    const bool run_cycle_active = applied_total > 0 || protection_hold || hp1_valve_defrost || hp2_valve_defrost ||
                                  hp1_oil_return || hp2_oil_return;
    oq_power_house_run_extension::Input run_ext_input;
    run_ext_input.enabled = run_ext_enabled;
    run_ext_input.cycle_active = run_cycle_active;
    run_ext_input.actual_heating_active = applied_total > 0;
    run_ext_input.inputs_valid = demand.valid && room_inputs_valid;
    run_ext_input.heating_allowed = heating_allowed;
    run_ext_input.room_c = room_c;
    run_ext_input.setpoint_c = setpoint_c;
    run_ext_input.base_requested_w = base_requested_w;
    run_ext_input.minimum_viable_w = minimum_viable_w;
    run_ext_input.water_limit_factor = id(oq_water_temp_limit_factor);
    const float restart_hysteresis_c = std::isfinite(config.run_extension_restart_hysteresis_c)
                                           ? config.run_extension_restart_hysteresis_c
                                           : oq_power_house_run_extension::kRestartHysteresisC;
    const auto run_ext_decision = oq_power_house_run_extension::evaluate(
        run_ext_input, {stop_margin_c, restart_hysteresis_c}, this->run_extension_state_);
    if (run_ext_decision.next.phase != this->last_run_ext_phase_) {
      ESP_LOGI("quatt.strategy", "ph run extension %s -> %s (room %.2f stop %.2f restart %.2f base %.0f floor %.0f)",
               oq_power_house_run_extension::phase_name(this->last_run_ext_phase_),
               oq_power_house_run_extension::phase_name(run_ext_decision.next.phase), (double)room_c,
               (double)run_ext_decision.comfort_stop_c, (double)run_ext_decision.warm_restart_c,
               (double)base_requested_w, (double)run_ext_decision.floor_w);
      this->last_run_ext_phase_ = run_ext_decision.next.phase;
    }
    this->run_extension_state_ = run_ext_decision.next;
    this->run_ext_enabled_ = run_ext_enabled;
    this->run_ext_base_w_ = base_requested_w;
    this->run_ext_floor_w_ = run_ext_decision.floor_active ? run_ext_decision.floor_w : 0.0f;
    this->run_ext_comfort_stop_c_ = run_ext_decision.comfort_stop_c;
    this->run_ext_warm_restart_c_ = run_ext_decision.warm_restart_c;
    // Blocked diagnostic: enabled and armed below Pmin, but water limiter or
    // missing Pmin prevents the floor from being applied.
    this->run_ext_blocked_ = run_ext_enabled && room_inputs_valid && heating_allowed &&
                             this->run_extension_state_.cycle_armed && std::isfinite(minimum_viable_w) == false &&
                             base_requested_w > 0.0f;
    if (run_ext_enabled && room_inputs_valid && heating_allowed && this->run_extension_state_.cycle_armed &&
        oq_power_house_run_extension::pmin_valid(minimum_viable_w) && base_requested_w < minimum_viable_w &&
        !oq_power_house_run_extension::water_floor_allowed(id(oq_water_temp_limit_factor)) && std::isfinite(room_c) &&
        std::isfinite(run_ext_decision.comfort_stop_c) && room_c < run_ext_decision.comfort_stop_c)
      this->run_ext_blocked_ = true;
    float effective_requested_w = base_requested_w;
    int effective_raw_demand = base_raw_demand;
    if (run_ext_decision.force_comfort_stop) {
      effective_requested_w = 0.0f;
      effective_raw_demand = 0;
    } else if (run_ext_decision.floor_active) {
      effective_requested_w = std::max(base_requested_w, run_ext_decision.floor_w);
      const float rated_tmp_w = id(house_rated_power_w).state;
      if (std::isfinite(rated_tmp_w) && rated_tmp_w > 0.0f && config.demand_max_f > 0)
        effective_raw_demand =
            std::max(base_raw_demand,
                     std::min(config.demand_max_f,
                              static_cast<int>(std::ceil(effective_requested_w * config.demand_max_f / rated_tmp_w))));
    }
    this->run_ext_effective_w_ = effective_requested_w;
    requested_w = effective_requested_w;
    raw_demand = effective_raw_demand;
    id(oq_phouse_req_w) = requested_w;
    id(oq_phouse_last_w) = base_last_w;
    id(oq_ph_run_ext_base_w) = base_requested_w;
    id(oq_ph_run_ext_floor_w) = this->run_ext_floor_w_;
    id(oq_ph_run_ext_state_code) = static_cast<int>(this->run_extension_state_.phase);
    id(oq_demand_raw) = raw_demand;
    id(oq_demand_filtered_prev) = id(oq_demand_filtered);
    id(oq_demand_filtered) = raw_demand;
    id(oq_heating_demand_filtered) = raw_demand;
    const int capped_demand =
        std::min(raw_demand, std::max(0, std::min(config.demand_max_f, static_cast<int>(id(oq_power_cap_f)))));
    const float rated_w = id(house_rated_power_w).state;
    if (std::isfinite(requested_w) && std::isfinite(rated_w) && rated_w > 0.0f && config.demand_max_f > 0)
      requested_w = std::min(requested_w, rated_w * static_cast<float>(capped_demand) / config.demand_max_f);
    dispatch_input.demand_level = capped_demand;
    dispatch_input.requested_w = requested_w;
    const oq_power_house_dispatch::DispatchTuning dispatch_tuning{
        id(oq_power_limit_soft_w),       id(oq_power_limit_peak_w),        config.optimizer_penalty_per_w,
        config.topology_power_margin_w,  config.topology_heat_advantage_w, config.defrost_comp_min_f,
        config.defrost_comp_boost_steps,
    };
    const auto dispatch =
        oq_power_house_dispatch::decide_dispatch(dispatch_input, dispatch_tuning, this->dispatch_state_);
    id(oq_ph_request_hp1_level) = dispatch.hp1_level;
    id(oq_ph_request_hp2_level) = dispatch.hp2_level;
    id(oq_ph_request_owner_hp) = dispatch.owner_hp;
    id(oq_ph_request_reason_code) = static_cast<int>(dispatch.reason);
    id(oq_P_hp_cap_w) = dispatch.capacity_w;
    // House deficit stays on base house need after caps, never on the #608 floor.
    const int base_capped_demand =
        std::min(base_raw_demand, std::max(0, std::min(config.demand_max_f, static_cast<int>(id(oq_power_cap_f)))));
    float base_capped_w = base_requested_w;
    if (std::isfinite(base_capped_w) && std::isfinite(rated_w) && rated_w > 0.0f && config.demand_max_f > 0)
      base_capped_w = std::min(base_capped_w, rated_w * static_cast<float>(base_capped_demand) / config.demand_max_f);
    const float house_deficit_w = oq_power_house_run_extension::compute_house_deficit_w(
        base_capped_w, dispatch.capacity_w, dispatch.output_valid, dispatch.deficit_w);
    const bool house_saturated =
        oq_power_house_run_extension::compute_house_saturated(base_capped_demand, house_deficit_w);
    id(oq_P_deficit_w) = house_deficit_w;

#if OQ_TOPOLOGY_DUO
    const std::string optimizer_reason(oq_power_house_dispatch::request_reason_name(static_cast<int>(dispatch.reason)));
    if (optimizer_reason != this->last_optimizer_reason_) {
      id(oq_duo_optimizer_reason).publish_state(optimizer_reason.c_str());
      this->last_optimizer_reason_ = optimizer_reason;
    }
#endif

    id(oq_strategy_phase_code) = capped_demand > 0 ? 1 : 0;
    id(oq_strategy_requested_power_w) = requested_w;
    id(oq_strategy_supply_target_temp) = NAN;
    id(oq_strategy_heat_request_active) = capped_demand > 0;
    id(oq_strategy_hp_expected_power_w) = dispatch.expected_w;
    id(oq_strategy_hp_max_power_w) = dispatch.capacity_w;
    id(oq_strategy_hp_saturated) = house_saturated;
    id(oq_strategy_output_valid) = dispatch.output_valid;
    id(oq_strategy_output_source_code) = 3;
    id(oq_strategy_output_updated_ms) = now_ms;
    id(oq_strategy_phase_text).publish_state(capped_demand > 0 ? "heat" : "idle");
    ESP_LOGD("quatt.strategy", "ph f=%d raw=%d preq=%.0f base=%.0f floor=%.0f ext=%d intent=%s owner=%d reason=%d",
             capped_demand, raw_demand, requested_w, (double)base_requested_w, (double)this->run_ext_floor_w_,
             static_cast<int>(this->run_extension_state_.phase), oq_heat_intent::reason_name(intent.reason),
             dispatch.owner_hp, static_cast<int>(dispatch.reason));
  }

  void reset() {
    this->dispatch_state_ = {};
    this->last_optimizer_reason_.clear();
    this->intent_state_ = {};
    this->demand_state_ = {};
    this->fast_floor_w_ = 0.0f;
    this->run_extension_state_ = {};
    this->run_ext_enabled_ = false;
    this->run_ext_blocked_ = false;
    this->run_ext_base_w_ = 0.0f;
    this->run_ext_effective_w_ = 0.0f;
    this->run_ext_floor_w_ = 0.0f;
    this->run_ext_comfort_stop_c_ = NAN;
    this->run_ext_warm_restart_c_ = NAN;
    this->last_run_ext_phase_ = oq_power_house_run_extension::Phase::INACTIVE;
    id(oq_ph_fast_intent_code) = 0;
    id(oq_ph_run_ext_base_w) = 0.0f;
    id(oq_ph_run_ext_floor_w) = 0.0f;
    id(oq_ph_run_ext_state_code) = 0;
    id(oq_ph_request_last_loop_ms) = 0;
    id(oq_ph_request_hp1_level) = 0;
    id(oq_ph_request_hp2_level) = 0;
    id(oq_ph_request_owner_hp) = 0;
    id(oq_ph_request_reason_code) = 0;
    id(oq_phouse_last_ms) = 0;
    id(oq_phouse_last_w) = 0.0f;
    id(oq_phouse_comfort_memory_c) = 0.0f;
    id(oq_phouse_demand_external) = false;
    id(oq_P_hp_cap_w) = 0.0f;
    id(oq_P_deficit_w) = 0.0f;
  }

  std::string run_extension_status() const {
    if (!this->run_ext_enabled_) return "inactive";
    if (this->run_ext_blocked_) return "blocked";
    return std::string(oq_power_house_run_extension::phase_name(this->run_extension_state_.phase));
  }

  float run_extension_base_w() const { return this->run_ext_base_w_; }
  float run_extension_floor_w() const { return this->run_ext_floor_w_; }
  float run_extension_effective_w() const { return this->run_ext_effective_w_; }
  float run_extension_comfort_stop_c() const { return this->run_ext_comfort_stop_c_; }
  float run_extension_warm_restart_c() const { return this->run_ext_warm_restart_c_; }

 private:
  static bool near_(float value, float expected) { return std::isfinite(value) && std::fabs(value - expected) < 0.25f; }

  template <typename T>
  static void set_number_(T& number, float value) {
    auto call = number.make_call();
    call.set_value(value);
    call.perform();
  }

  oq_power_house_dispatch::DispatchState dispatch_state_;
  oq_heat_intent::State intent_state_;
  oq_power_house::DemandState demand_state_;
  float fast_floor_w_{0.0f};
  std::string last_optimizer_reason_;
  oq_power_house_run_extension::State run_extension_state_;
  oq_power_house_run_extension::Phase last_run_ext_phase_{oq_power_house_run_extension::Phase::INACTIVE};
  bool run_ext_enabled_{false};
  bool run_ext_blocked_{false};
  float run_ext_base_w_{0.0f};
  float run_ext_effective_w_{0.0f};
  float run_ext_floor_w_{0.0f};
  float run_ext_comfort_stop_c_{NAN};
  float run_ext_warm_restart_c_{NAN};
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_power_house_runtime
#endif
