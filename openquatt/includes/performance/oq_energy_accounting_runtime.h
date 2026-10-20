#pragma once

#include "oq_energy_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_energy_accounting_runtime {

// Keep the former total-counter keys as recovery checkpoints. They no longer
// integrate power independently. Cache the preference handles, not new backends.
inline auto& daily_checkpoint() {
  static auto preference = id(oq_electrical_energy_daily).template make_entity_preference<float>();
  return preference;
}

inline auto& cumulative_checkpoint() {
  static auto preference = id(oq_electrical_energy_cumulative).template make_entity_preference<float>();
  return preference;
}

inline auto& daily_publication() {
  static oq_energy::DailyElectricalPublication daily_publication;
  return daily_publication;
}

inline void observe_daily_source(uint32_t date_key, bool for_cooling) {
  oq_energy::observe_daily_electrical_source(daily_publication(), date_key, for_cooling,
                                             for_cooling ? id(oq_cooling_electrical_energy_daily).raw_state
                                                         : id(oq_heating_electrical_energy_daily).raw_state);
}

inline bool daily_totals_current(uint32_t date_key) {
  const auto& published = daily_publication();
  return date_key != 0 && published.date_key == date_key && !published.waiting_for_reset && isfinite(published.total) &&
         published.heating == id(oq_heating_electrical_energy_daily).raw_state &&
         published.cooling == id(oq_cooling_electrical_energy_daily).raw_state &&
         published.total == id(oq_electrical_energy_daily).raw_state;
}

inline bool publish_totals(uint32_t date_key = 0) {
  const float daily = oq_energy::reconcile_electrical_energy(id(oq_heating_electrical_energy_daily).raw_state,
                                                             id(oq_cooling_electrical_energy_daily).raw_state, NAN)
                          .total;
  const float cumulative =
      oq_energy::reconcile_electrical_energy(id(oq_heating_electrical_energy_cumulative).raw_state,
                                             id(oq_cooling_electrical_energy_cumulative).raw_state, NAN)
          .total;
  bool saved = true;
  if (oq_energy::daily_electrical_total_ready(daily_publication(), date_key,
                                              id(oq_heating_electrical_energy_daily).raw_state,
                                              id(oq_cooling_electrical_energy_daily).raw_state)) {
    if (isfinite(daily)) saved = daily_checkpoint().save(&daily) && saved;
    id(oq_electrical_energy_daily).publish_state(daily);
  }
  if (isfinite(cumulative)) saved = cumulative_checkpoint().save(&cumulative) && saved;
  id(oq_electrical_energy_cumulative).publish_state(cumulative);
  return saved;
}

inline bool restore_totals(bool cooling_purpose = false) {
  float daily_checkpoint_wh = NAN;
  float cumulative_checkpoint_wh = NAN;
  daily_checkpoint().load(&daily_checkpoint_wh);
  cumulative_checkpoint().load(&cumulative_checkpoint_wh);
  const auto daily = oq_energy::reconcile_electrical_energy(id(oq_heating_electrical_energy_daily).raw_state,
                                                            id(oq_cooling_electrical_energy_daily).raw_state,
                                                            daily_checkpoint_wh, cooling_purpose);
  const auto cumulative = oq_energy::reconcile_electrical_energy(id(oq_heating_electrical_energy_cumulative).raw_state,
                                                                 id(oq_cooling_electrical_energy_cumulative).raw_state,
                                                                 cumulative_checkpoint_wh, cooling_purpose);
  if (isfinite(daily.heating) && daily.heating != id(oq_heating_electrical_energy_daily).raw_state)
    id(oq_heating_electrical_energy_daily).publish_state_and_save(daily.heating);
  if (isfinite(cumulative.heating) && cumulative.heating != id(oq_heating_electrical_energy_cumulative).raw_state)
    id(oq_heating_electrical_energy_cumulative).set_value(cumulative.heating);
  if (isfinite(daily.cooling) && daily.cooling != id(oq_cooling_electrical_energy_daily).raw_state)
    id(oq_cooling_electrical_energy_daily).publish_state_and_save(daily.cooling);
  if (isfinite(cumulative.cooling) && cumulative.cooling != id(oq_cooling_electrical_energy_cumulative).raw_state)
    id(oq_cooling_electrical_energy_cumulative).set_value(cumulative.cooling);
  return publish_totals();
}

}  // namespace oq_energy_accounting_runtime
#endif
