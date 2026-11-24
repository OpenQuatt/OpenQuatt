#pragma once

#include "oq_energy_logic.h"
#include "../sources/oq_source_receipt_runtime.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_energy_runtime {

inline bool v2_power_quality_contract_active() {
  const bool configured_v2 = id(hp_generation).has_state() && id(hp_generation).current_option() == "V2";
  const auto detected_v2 = [](bool detection_complete, int variant_code) {
    const auto variant = oq_odu::confirmed_variant(detection_complete, variant_code);
    return variant == oq_odu::Variant::V2_OLD_MODEL || variant == oq_odu::Variant::V2_NEW_MODEL;
  };
  bool active =
      configured_v2 || detected_v2(id(hp1_odu_generation_detection_complete), id(hp1_generation_variant_code));
#if OQ_TOPOLOGY_DUO
  active = active || detected_v2(id(hp2_odu_generation_detection_complete), id(hp2_generation_variant_code));
#endif
  return active;
}

inline oq_energy::HeatingCopState heating_cop_state(const oq_sources::HeatPumpReceipts& receipts, bool online,
                                                    uint64_t now_ms, uint64_t stale_ms) {
  if (id(oq_control_mode_code) == 1) return {};
  const float valve_status = receipts.status_2108.value;
  const bool valve_fresh =
      receipts.status_2108.fresh(now_ms, stale_ms) && valve_status >= 0.0f && valve_status <= 65535.0f;
  return {receipts.working_mode.value, receipts.compressor_frequency.value, receipts.defrost.value,
          online && valve_fresh && receipts.working_mode.fresh(now_ms, stale_ms) &&
              receipts.compressor_frequency.fresh(now_ms, stale_ms) && receipts.defrost.fresh(now_ms, stale_ms) &&
              receipts.water_in.fresh(now_ms, stale_ms) && receipts.water_out.fresh(now_ms, stale_ms),
          valve_fresh && (static_cast<uint16_t>(valve_status) & 0x0040U) != 0U};
}

inline float heating_cop_output(const oq_sources::HeatPumpReceipts& receipts, const oq_energy::HeatingCopState& state,
                                float inlet_offset_c, float outlet_offset_c, float flow_lph, float cp_j_per_kgk) {
  if (!oq_energy::heating_cop_state_valid(state) || state.valve_reversed) return NAN;
  if (!isfinite(flow_lph) || flow_lph <= 0.0f) return NAN;
  const bool heating = oq_energy::heating_cop_compressor_running(state);
  const bool defrost_reported = state.working_mode == 4.0f || state.defrost == 1.0f;
  const float mode = state.working_mode == 4.0f ? 2.0f : state.working_mode;
  // COP uses the current water measurements; a cached positive wattage can outlive the change in direction.
  const float inlet_c = receipts.water_in.value + inlet_offset_c;
  const float outlet_c = receipts.water_out.value + outlet_offset_c;
  if (!isfinite(inlet_c) || !isfinite(outlet_c)) return NAN;
  const float output = oq_energy::hp_heating_power(mode, inlet_c, outlet_c, flow_lph, cp_j_per_kgk);
  if ((defrost_reported && !heating) || (heating && output <= 0.0f)) return NAN;
  return output;
}

inline float total_power_input() {
#if OQ_TOPOLOGY_DUO
  if (v2_power_quality_contract_active())
    return oq_energy::nonnegative_sum_required(id(hp1_power_input).state, id(hp2_power_input).state, true);
  return oq_energy::nonnegative_sum(id(hp1_power_input).state, id(hp2_power_input).state);
#else
  if (v2_power_quality_contract_active()) return oq_energy::nonnegative_sum_required(id(hp1_power_input).state);
  return oq_energy::nonnegative_sum(id(hp1_power_input).state);
#endif
}

inline float heating_power_input() {
  return oq_energy::attributed_input_power(total_power_input(), id(oq_cooling_energy_session_active), false);
}

inline float cooling_power_input() {
  return oq_energy::attributed_input_power(total_power_input(), id(oq_cooling_energy_session_active), true);
}

inline float total_heat_power() {
#if OQ_TOPOLOGY_DUO
  return oq_energy::sum_available(id(hp1_heat_power).state, id(hp2_heat_power).state);
#else
  return oq_energy::sum_available(id(hp1_heat_power).state);
#endif
}

inline float total_cop(uint64_t now_ms, uint64_t stale_ms, float minimum_input, float flow_lph, float cp_j_per_kgk) {
  const auto first = heating_cop_state(oq_sources::hp1, id(hp1_is_online), now_ms, stale_ms);
  const float first_output = heating_cop_output(oq_sources::hp1, first, id(hp1_water_in_temp_offset).state,
                                                id(hp1_water_out_temp_offset).state, flow_lph, cp_j_per_kgk);
#if OQ_TOPOLOGY_DUO
  const auto second = heating_cop_state(oq_sources::hp2, id(hp2_is_online), now_ms, stale_ms);
  const float second_output = heating_cop_output(oq_sources::hp2, second, id(hp2_water_in_temp_offset).state,
                                                 id(hp2_water_out_temp_offset).state, flow_lph, cp_j_per_kgk);
  // A missing unit must not become zero in the live efficiency denominator.
  return oq_energy::heating_cop_or_nan(first_output + second_output,
                                       id(hp1_power_input).state + id(hp2_power_input).state, minimum_input, first,
                                       second, true);
#else
  return oq_energy::heating_cop_or_nan(first_output, id(hp1_power_input).state, minimum_input, first);
#endif
}

inline float total_cooling_power() {
#if OQ_TOPOLOGY_DUO
  return oq_energy::sum_available(id(hp1_cooling_power).state, id(hp2_cooling_power).state);
#else
  return oq_energy::sum_available(id(hp1_cooling_power).state);
#endif
}

}  // namespace oq_energy_runtime
#endif
