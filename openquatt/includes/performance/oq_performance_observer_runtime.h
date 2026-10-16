#pragma once

#include <cstdio>
#include <string>

#include "oq_water_performance_shadow.h"
#include "../sources/oq_source_receipt_runtime.h"
#include "../control/oq_compressor_frequency_runtime.h"

#if defined(ESP_PLATFORM) && OQ_TOPOLOGY_DUO
namespace oq_performance_observer {
// Called synchronously by a template sensor on the ESPHome loop; no retained job or buffer.
inline std::string report(bool enabled) {
  if (!enabled) return "disabled";
  const auto now = oq_sources::monotonic_ms();
  const auto& a = oq_sources::hp1;
  const auto& b = oq_sources::hp2;
  const auto ready = [&](const auto& hp, bool online) {
    return online && hp.working_mode.fresh(now, 30000) && hp.defrost.fresh(now, 30000) && hp.defrost.value == 0 &&
           (hp.working_mode.value == 0 || hp.working_mode.value == 2) && hp.compressor_frequency.fresh(now, 30000) &&
           hp.water_out.fresh(now, 30000) && ((hp.compressor_frequency.value > 0) == (hp.working_mode.value == 2));
  };
  if (!ready(a, id(hp1_is_online)) || !ready(b, id(hp2_is_online)) || !a.outside.fresh(now, 1800000) ||
      !a.flow.fresh(now, 30000) || !a.water_in.fresh(now, 30000) || !b.water_in.fresh(now, 30000))
    return "unavailable";
  const float inlet = a.water_in.value + id(hp1_water_in_temp_offset).state;
  const std::array<float, 2> outlet{a.water_out.value + id(hp1_water_out_temp_offset).state,
                                    b.water_out.value + id(hp2_water_out_temp_offset).state};
  const float second_inlet = b.water_in.value + id(hp2_water_in_temp_offset).state;
  if (!std::isfinite(second_inlet) || !std::isfinite(outlet[0]) || std::fabs(outlet[0] - second_inlet) > .5f)
    return "unavailable";
  const auto frequency = oq_frequency_runtime::capture();
  const std::array<oq_odu::Variant, 2> variants{frequency.performance_variant(true),
                                                frequency.performance_variant(false)};
  const std::array<float, 2> hz{a.compressor_frequency.value, b.compressor_frequency.value};
  const auto own1 = oq_water_shadow::observed(variants[0], hz[0], a.outside.value, outlet[0]);
  const auto own2 = oq_water_shadow::observed(variants[1], hz[1], a.outside.value, outlet[1]);
  const auto pair = oq_water_shadow::series(variants, hz, a.outside.value, inlet, a.flow.value);
  if (!own1.valid() || !own2.valid() || !pair[0].valid() || !pair[1].valid()) return "unavailable";
  const float reference =
      id(oq_heat_mode_code) == 1 ? id(oq_strategy_supply_target_temp) : id(water_supply_temp_selected).state;
  const auto shared1 = oq_water_shadow::observed(variants[0], hz[0], a.outside.value, reference);
  const auto shared2 = oq_water_shadow::observed(variants[1], hz[1], a.outside.value, reference);
  char text[224];
  snprintf(text, sizeof(text), "hz=%.0f/%.0f own=%.2f/%.2f shared=%.2f/%.2f series=%.2f pel=%.2f/%.2f tout=%.3f/%.3f",
           hz[0], hz[1], own1.thermal_w, own2.thermal_w, shared1.thermal_w, shared2.thermal_w,
           pair[0].thermal_w + pair[1].thermal_w, own1.electrical_w + own2.electrical_w,
           pair[0].electrical_w + pair[1].electrical_w, pair[0].outlet_c, pair[1].outlet_c);
  return text;
}
}  // namespace oq_performance_observer
#endif
