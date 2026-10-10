#pragma once

#include "../control/oq_compressor_frequency_runtime.h"
#include "oq_heating_capacity.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_heating_capacity {

// Evaluated only by the diagnostic sensor, on ESPHome's main loop. Keep this
// independent of strategy-local telemetry resets and actuator decisions.
inline float current_estimate(uint32_t minimum_off_ms, uint32_t outlet_stale_ms, float defrost_factor) {
  if (id(oq_control_mode_code) == 5) return NAN;
  const uint32_t now_ms = static_cast<uint32_t>(millis());
  auto hp1 = oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(1), id(hp1_last_applied_level));
  hp1.minimum_off_ready =
      oq_hp_candidate::minimum_off_ready(now_ms, id(hp1_last_stop_ms), minimum_off_ms, id(hp1_last_applied_level));
  const float hp1_outlet_c =
      fresh_outlet(now_ms, outlet_stale_ms, id(hp1_is_online), id(hp1_water_out_temp_last_update_ms),
                   id(hp1_water_out_temp).has_state(), id(hp1_water_out_temp).state);
#if OQ_TOPOLOGY_DUO
  auto hp2 = oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(2), id(hp2_last_applied_level));
  hp2.minimum_off_ready =
      oq_hp_candidate::minimum_off_ready(now_ms, id(hp2_last_stop_ms), minimum_off_ms, id(hp2_last_applied_level));
  const float hp2_outlet_c =
      fresh_outlet(now_ms, outlet_stale_ms, id(hp2_is_online), id(hp2_water_out_temp_last_update_ms),
                   id(hp2_water_out_temp).has_state(), id(hp2_water_out_temp).state);
  const bool hp2_valve_defrost = id(hp2_4_way_valve).state;
#else
  const oq_hp_candidate::HpCandidateState hp2;
  const float hp2_outlet_c = NAN;
  const bool hp2_valve_defrost = false;
#endif
  const auto supply = oq_power_house_dispatch::select_performance_supply(
      {id(oq_system_supply_temp).state, hp1_outlet_c, hp2_outlet_c, oq_hp_candidate::may_serve_candidate(hp1),
       OQ_TOPOLOGY_DUO && oq_hp_candidate::may_serve_candidate(hp2)});
  const bool boundary_estimate = id(oq_cold_start_session_active) && !id(oq_cold_start_hp_blocked);
  return estimate(oq_frequency_runtime::capture(), OQ_TOPOLOGY_DUO, id(outside_temp_selected).state, supply.supply_c,
                  hp1, hp2, id(oq_power_limit_soft_w), id(oq_power_limit_peak_w), id(hp1_4_way_valve).state,
                  hp2_valve_defrost, defrost_factor, boundary_estimate);
}

}  // namespace oq_heating_capacity
#endif
