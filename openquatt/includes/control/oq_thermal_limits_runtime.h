#pragma once

#include "oq_thermal_limits_logic.h"
#include "../sources/oq_source_receipt_runtime.h"

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
#include "oq_sensor_source_runtime.h"
#include "oq_supply_calibration_logic.h"
#include "../boiler/oq_otb_telemetry.h"
#endif

namespace oq_thermal_limits_runtime {

class Runtime {
 public:
  void tick(float minimum_flow_lph, float mismatch_threshold_lph) {
    oq_thermal_limits::Inputs inputs{static_cast<uint32_t>(millis()), id(water_supply_temp_selected).state,
                                     id(max_water_temp_limit_c).state, id(oq_control_mode_code)};
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    if (id(oq_post_dhw_run_through).state || state_.post_dhw_active || state_.awaiting_supply ||
        state_.awaiting_heat_pump_water) {
      const uint64_t now = oq_sources::monotonic_ms();
      const bool local_pt1000 = id(water_supply_source).current_option() == "Local" &&
                                id(oq_local_supply_temp_source).current_option() == "PT1000";
      const bool fresh_pt1000 =
          oq_sources::local_supply_pt1000.fresh(now, 7500) &&
          id(oq_water_supply_temp_current_source_code) == oq_supply_calibration::SOURCE_LOCAL_PT1000;
      inputs.supply_observed = isfinite(inputs.supply_c) && id(oq_water_supply_temp_current_source_ready) &&
                               !id(oq_water_supply_temp_fallback_runtime_active) &&
                               !id(oq_water_supply_temp_selected_hold_active) && (!local_pt1000 || fresh_pt1000);
      if (local_pt1000 && inputs.supply_observed) inputs.dhw_end_ms = post_dhw_end_(now, inputs.control_mode);
      if (local_pt1000 && inputs.supply_observed &&
          (inputs.dhw_end_ms != 0 || state_.post_dhw_active || state_.awaiting_supply ||
           state_.awaiting_heat_pump_water)) {
        // Check the newest receipt as well as the last selected update. The
        // selection and the MAX31865 publish independently of this loop.
        const float offset = id(water_supply_temp_calibration_offset).state;
        const float raw_c = oq_sources::local_supply_pt1000.value;
        if (isfinite(offset)) inputs.supply_c = fmaxf(inputs.supply_c, raw_c + offset);
        if (raw_c >= oq_thermal_limits::kPostDhwSystemCeilingC) inputs.supply_c = fmaxf(inputs.supply_c, raw_c);
      }
      if (inputs.dhw_end_ms != 0 || state_.post_dhw_active || state_.awaiting_heat_pump_water) {
        const uint8_t applied = applied_units_();
        if (!state_.post_dhw_active && !state_.awaiting_heat_pump_water) guard_units_ = applied;
        observe_water_(inputs, now, guard_units_);
        const float flow = oq_sensor_source::runtime().circulation_flow_lph(now, mismatch_threshold_lph);
        inputs.running_heat_pumps &= applied == guard_units_ && std::isfinite(flow) && flow >= minimum_flow_lph;
      }
    }
#else
    (void)minimum_flow_lph;
    (void)mismatch_threshold_lph;
#endif
    oq_thermal_limits::update(state_, inputs);
    id(oq_post_dhw_hp_water_c) = state_.post_dhw_active ? inputs.heat_pump_water_c : NAN;
    id(oq_water_temp_limit_factor) = state_.limit_factor;
    id(oq_water_temp_boiler_inhibit_active) = state_.boiler_inhibit;
    id(oq_water_temp_trip_active) = state_.trip || state_.awaiting_heat_pump_water;
    id(oq_water_temp_hard_trip_active) = state_.heat_pump_hard_trip();
    id(oq_system_water_temp_hard_trip_active) = state_.hard_trip;
  }

 private:
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
  uint64_t post_dhw_end_(uint64_t now, int cm) const {
    if (!id(oq_post_dhw_run_through).state || (cm != 2 && cm != 3) || id(oq_heat_mode_code) != 0 ||
        id(oq_strategy_output_source_code) != 3 || id(oq_commissioning_active) || id(oq_runtime_polling_paused).state ||
        id(oq_lowflow_fault_active) || !id(oq_aux_heat_source_present).state || !id(oq_otb_link_available_state) ||
        id(oq_boiler_connection).current_option() != "OpenTherm")
      return 0;
    const auto status = oq_otb::telemetry_state.field_receipt(oq_otb::FIELD_STATUS);
    // Fault, flame, or diagnostic indication invalidate the handover.
    if ((status.value & 0x0049U) != 0) return 0;
    const auto end = oq_otb::telemetry_state.recent_dhw_end_ms(now, oq_thermal_limits::kPostDhwMaxAgeMs);
    return oq_thermal_limits::post_dhw_window_open(now, end, state_.post_dhw_active) ? end : 0;
  }

  static uint8_t applied_units_() {
    uint8_t units = id(hp1_last_applied_level) > 0 ? 1U : 0U;
#if OQ_TOPOLOGY_DUO
    if (id(hp2_last_applied_level) > 0) units |= 2U;
#endif
    return units;
  }

  static void observe_unit_(oq_thermal_limits::Inputs& inputs, const oq_sources::HeatPumpReceipts& raw, uint64_t now,
                            bool online, bool normal_heating, float in_offset_c, float out_offset_c) {
    // The normal ODU poll is 10 s; allow bus and scheduling jitter but not a missed poll.
    constexpr uint64_t fresh_ms = 15000;
    inputs.running_heat_pumps &= online && normal_heating && raw.working_mode.fresh(now, fresh_ms) &&
                                 raw.working_mode.value == 2.0f && raw.compressor_frequency.fresh(now, fresh_ms) &&
                                 raw.compressor_frequency.value > 0.5f;
    const bool in_valid = online && raw.water_in.fresh(now, fresh_ms) && isfinite(in_offset_c);
    const bool out_valid = online && raw.water_out.fresh(now, fresh_ms) && isfinite(out_offset_c);
    inputs.heat_pump_water_observed &= in_valid && out_valid;
    // A valid hot reading can require a stop even while a different reading is missing.
    if (in_valid)
      inputs.heat_pump_water_c = fmaxf(inputs.heat_pump_water_c, raw.water_in.value + fmaxf(in_offset_c, 0.0f));
    if (out_valid)
      inputs.heat_pump_water_c = fmaxf(inputs.heat_pump_water_c, raw.water_out.value + fmaxf(out_offset_c, 0.0f));
  }

  static void observe_water_(oq_thermal_limits::Inputs& inputs, uint64_t now, uint8_t units) {
    inputs.running_heat_pumps = inputs.heat_pump_water_observed = units != 0;
    if ((units & 1U) != 0) {
      observe_unit_(inputs, oq_sources::hp1, now, id(hp1_is_online),
                    !id(hp1_defrost).state && !id(hp1_4_way_valve).state && !id(hp1_prot_oil_return).state,
                    id(hp1_water_in_temp_offset).state, id(hp1_water_out_temp_offset).state);
    }
#if OQ_TOPOLOGY_DUO
    if ((units & 2U) != 0) {
      observe_unit_(inputs, oq_sources::hp2, now, id(hp2_is_online),
                    !id(hp2_defrost).state && !id(hp2_4_way_valve).state && !id(hp2_prot_oil_return).state,
                    id(hp2_water_in_temp_offset).state, id(hp2_water_out_temp_offset).state);
    }
#endif
  }

  // Keep reading the affected circuits after a stop to require their own recovery.
  uint8_t guard_units_ = 0;
#endif

  oq_thermal_limits::State state_{};
};

inline Runtime& runtime() {
  static Runtime value;
  return value;
}

}  // namespace oq_thermal_limits_runtime
