#pragma once

#include "oq_water_performance.h"
#include "../sources/oq_source_receipt_runtime.h"
#include "../control/oq_compressor_frequency_runtime.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_water_performance {
// Use own Tout only for a running HP; a stopped HP keeps the existing start reference.
inline float own_outlet([[maybe_unused]] bool hp1, float fallback) {
  const auto now = oq_sources::monotonic_ms();
  const auto* raw = &oq_sources::hp1;
  float offset = id(hp1_water_out_temp_offset).state;
#if OQ_TOPOLOGY_DUO
  if (!hp1) {
    raw = &oq_sources::hp2;
    offset = id(hp2_water_out_temp_offset).state;
  }
#endif
  return raw->working_mode.fresh(now, 30000) && raw->working_mode.value == 2 &&
                 raw->compressor_frequency.fresh(now, 30000) && raw->compressor_frequency.value > 0 &&
                 raw->water_out.fresh(now, 30000) && std::isfinite(offset)
             ? raw->water_out.value + offset
             : fallback;
}
inline CandidateModel capture(const oq_frequency_runtime::Context& frequency, float fallback, bool defrost) {
  const auto now = oq_sources::monotonic_ms();
  const auto& raw = oq_sources::hp1;
  const float inlet = raw.water_in.value + id(hp1_water_in_temp_offset).state, flow = raw.flow.value;
  const bool coupled = !defrost && raw.water_in.fresh(now, 30000) && raw.flow.fresh(now, 30000) &&
                       std::isfinite(inlet) && flow >= 300 && flow <= 3000;
  return {frequency,
          id(outside_temp_selected).state,
          inlet,
          flow,
          {defrost ? fallback : own_outlet(true, fallback), defrost ? fallback : own_outlet(false, fallback)},
          coupled,
          id(oq_cold_start_session_active) && !id(oq_cold_start_hp_blocked)};
}
}  // namespace oq_water_performance
#endif
