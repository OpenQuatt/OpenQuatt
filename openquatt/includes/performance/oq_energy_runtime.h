#pragma once

#include "oq_energy_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_energy_runtime {

inline float total_power_input() {
#if OQ_TOPOLOGY_DUO
  return oq_energy::nonnegative_sum(id(hp1_power_input).state, id(hp2_power_input).state);
#else
  return oq_energy::nonnegative_sum(id(hp1_power_input).state);
#endif
}

inline float heating_power_input() {
#if OQ_TOPOLOGY_DUO
  return oq_energy::heating_input_power(id(oq_cooling_energy_session_active), id(hp1_working_mode).state,
                                        id(hp1_power_input).state, id(hp2_working_mode).state,
                                        id(hp2_power_input).state);
#else
  return oq_energy::heating_input_power(id(oq_cooling_energy_session_active), id(hp1_working_mode).state,
                                        id(hp1_power_input).state);
#endif
}

inline float cooling_power_input() {
#if OQ_TOPOLOGY_DUO
  return oq_energy::cooling_input_power(id(oq_cooling_energy_session_active), id(hp1_power_input).state,
                                        id(hp2_power_input).state);
#else
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
