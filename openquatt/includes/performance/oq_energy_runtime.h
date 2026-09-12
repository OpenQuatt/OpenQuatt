#pragma once

#include "oq_energy_logic.h"

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
#if OQ_TOPOLOGY_DUO
  if (v2_power_quality_contract_active())
    return oq_energy::heating_input_power_required(id(oq_cooling_energy_session_active), id(hp1_working_mode).state,
                                                   id(hp1_power_input).state, true, id(hp2_working_mode).state,
                                                   id(hp2_power_input).state);
  return oq_energy::heating_input_power(id(oq_cooling_energy_session_active), id(hp1_working_mode).state,
                                        id(hp1_power_input).state, id(hp2_working_mode).state,
                                        id(hp2_power_input).state);
#else
  if (v2_power_quality_contract_active())
    return oq_energy::heating_input_power_required(id(oq_cooling_energy_session_active), id(hp1_working_mode).state,
                                                   id(hp1_power_input).state);
  return oq_energy::heating_input_power(id(oq_cooling_energy_session_active), id(hp1_working_mode).state,
                                        id(hp1_power_input).state);
#endif
}

inline float cooling_power_input() {
#if OQ_TOPOLOGY_DUO
  if (v2_power_quality_contract_active())
    return id(oq_cooling_energy_session_active)
               ? oq_energy::nonnegative_sum_required(id(hp1_power_input).state, id(hp2_power_input).state, true)
               : 0.0f;
  return oq_energy::cooling_input_power(id(oq_cooling_energy_session_active), id(hp1_power_input).state,
                                        id(hp2_power_input).state);
#else
  if (v2_power_quality_contract_active())
    return id(oq_cooling_energy_session_active) ? oq_energy::nonnegative_sum_required(id(hp1_power_input).state) : 0.0f;
  return oq_energy::cooling_input_power(id(oq_cooling_energy_session_active), id(hp1_power_input).state);
#endif
}

inline float total_heat_power() {
#if OQ_TOPOLOGY_DUO
  return oq_energy::sum_available(id(hp1_heat_power).state, id(hp2_heat_power).state);
#else
  return oq_energy::sum_available(id(hp1_heat_power).state);
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
