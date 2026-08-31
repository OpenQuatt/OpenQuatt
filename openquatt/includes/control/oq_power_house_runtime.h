#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "../performance/hp_perf_frequency.h"
#include "oq_compressor_frequency_runtime.h"
#include "oq_power_house_demand_logic.h"
#include "oq_power_house_dispatch_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_power_house_runtime {

struct TickConfig {
  uint32_t loop_ms;
  int demand_max_f;
  float temperature_guard_c;
  float defrost_power_factor;
  float optimizer_penalty_per_w;
  float topology_power_margin_w;
  float topology_heat_advantage_w;
  int defrost_comp_min_f;
  int defrost_comp_boost_steps;
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
    const bool active = id(oq_control_mode_code) != 5 && id(oq_heat_mode_code) != 1;
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
    const auto demand = oq_power_house::decide_demand(
        demand_input, demand_tuning, {id(oq_phouse_last_w), id(oq_phouse_last_ms), id(oq_phouse_comfort_memory_c)});
    id(oq_phouse_req_w) = demand.requested_w;
    id(oq_phouse_last_w) = demand.next.last_w;
    id(oq_phouse_last_ms) = demand.next.last_ms;
    id(oq_phouse_comfort_memory_c) = demand.next.comfort_memory_c;
    id(oq_phouse_demand_external) = demand.external;
    id(oq_demand_raw) = demand.raw_demand;

    const auto filtered =
        oq_power_house::filter_demand(demand.raw_demand, id(oq_demand_filtered), id(oq_demand_filter_ramp_up_budget),
                                      id(oq_demand_filter_ramp_up_step_min).state, cadence.dt_s, config.demand_max_f);
    id(oq_demand_filtered_prev) = filtered.previous;
    id(oq_demand_filter_ramp_up_budget) = filtered.ramp_budget;
    id(oq_demand_filtered) = filtered.filtered;
    id(oq_heating_demand_filtered) = filtered.filtered;
    const int capped_demand =
        std::min(filtered.filtered, std::max(0, std::min(config.demand_max_f, static_cast<int>(id(oq_power_cap_f)))));

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
    const float supply_c = id(oq_system_supply_temp).state;
    const bool performance_valid = std::isfinite(outside_c) && std::isfinite(supply_c);
    const auto hp1_candidate =
        oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(1), id(hp1_last_applied_level));
    const bool hp1_defrost_active = id(hp1_defrost).state;
#if OQ_TOPOLOGY_DUO
    const auto hp2_candidate =
        oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(2), hp2_applied_level);
    const bool hp2_defrost_active = id(hp2_defrost).state;
#else
    const oq_hp_candidate::HpCandidateState hp2_candidate;
    const bool hp2_defrost_active = false;
#endif

    float defrost_factor = config.defrost_power_factor;
    if (!std::isfinite(defrost_factor)) defrost_factor = 0.55f;
    defrost_factor = std::max(0.10f, std::min(1.00f, defrost_factor));
    const auto build_hp = [&](oq_power_house_dispatch::HpInput& result, bool hp1,
                              const oq_hp_candidate::HpCandidateState& candidate, bool defrost, bool valve_defrost) {
      result.candidate = candidate;
      result.defrost = defrost;
      result.valve_defrost = valve_defrost;
      result.levels[0] = {true, true, true, 0.0f, 0.0f};
      for (int level = 1; level <= oq_power_house_dispatch::kMaxLevel; ++level) {
        const bool allowed = frequency.frequency_allowed(hp1, 2, level);
        float thermal_w = NAN;
        float electrical_w = NAN;
        if (performance_valid) {
          const float hz = oq_perf::model_frequency_hz(level);
          thermal_w = oq_perf::interp_power_th_w_hz(hz, outside_c, supply_c);
          electrical_w = oq_perf::interp_power_el_w_hz(hz, outside_c, supply_c);
          if (valve_defrost && std::isfinite(thermal_w)) thermal_w *= defrost_factor;
        }
        result.levels[level] = {allowed, std::isfinite(thermal_w) && thermal_w >= 0.0f,
                                std::isfinite(electrical_w) && electrical_w >= 0.0f, thermal_w, electrical_w};
      }
    };

    float requested_w = id(oq_phouse_req_w);
    const float rated_w = id(house_rated_power_w).state;
    if (std::isfinite(requested_w) && std::isfinite(rated_w) && rated_w > 0.0f && config.demand_max_f > 0)
      requested_w = std::min(requested_w, rated_w * static_cast<float>(capped_demand) / config.demand_max_f);
    oq_power_house_dispatch::DispatchInput dispatch_input{now_ms, capped_demand,     requested_w,
                                                          duo,    performance_valid, lead_is_hp1};
    build_hp(dispatch_input.hp1, true, hp1_candidate, hp1_defrost_active, hp1_valve_defrost);
#if OQ_TOPOLOGY_DUO
    build_hp(dispatch_input.hp2, false, hp2_candidate, hp2_defrost_active, hp2_valve_defrost);
#endif
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
    id(oq_P_deficit_w) = dispatch.deficit_w;

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
    id(oq_strategy_hp_saturated) = dispatch.saturated;
    id(oq_strategy_output_valid) = dispatch.output_valid;
    id(oq_strategy_output_source_code) = 3;
    id(oq_strategy_output_updated_ms) = now_ms;
    id(oq_strategy_phase_text).publish_state(capped_demand > 0 ? "heat" : "idle");
    ESP_LOGD("quatt.strategy", "ph f=%d raw=%d preq=%.0f owner=%d reason=%d", capped_demand, demand.raw_demand,
             requested_w, dispatch.owner_hp, static_cast<int>(dispatch.reason));
  }

  void reset() {
    this->dispatch_state_ = {};
    this->last_optimizer_reason_.clear();
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

 private:
  static bool near_(float value, float expected) { return std::isfinite(value) && std::fabs(value - expected) < 0.25f; }

  template <typename T>
  static void set_number_(T& number, float value) {
    auto call = number.make_call();
    call.set_value(value);
    call.perform();
  }

  oq_power_house_dispatch::DispatchState dispatch_state_;
  std::string last_optimizer_reason_;
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_power_house_runtime
#endif
