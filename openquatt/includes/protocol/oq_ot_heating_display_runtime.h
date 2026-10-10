#pragma once

#include "../sources/oq_source_receipt_runtime.h"
#include "oq_ot_heating_display.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_ot_display {

// Snapshot physical receipts on the ESPHome loop, outside the OT transport.
// No control state is written; this uses no persistence or heap allocation.
inline Display current_display(uint64_t stale_ms, float defrost_factor) {
  const auto hp1_variant =
      oq_odu::confirmed_variant(id(hp1_odu_generation_detection_complete), id(hp1_generation_variant_code));
  const HeatPump hp1{hp1_variant, id(hp1_is_online), oq_sources::hp1.working_mode, oq_sources::hp1.compressor_frequency,
                     oq_sources::hp1.defrost};
#if OQ_TOPOLOGY_DUO
  const auto hp2_variant =
      oq_odu::confirmed_variant(id(hp2_odu_generation_detection_complete), id(hp2_generation_variant_code));
  const HeatPump hp2{hp2_variant, id(hp2_is_online), oq_sources::hp2.working_mode, oq_sources::hp2.compressor_frequency,
                     oq_sources::hp2.defrost};
#else
  const HeatPump hp2;
#endif
  return calculate(hp1, hp2, OQ_TOPOLOGY_DUO, oq_sources::monotonic_ms(), stale_ms, id(outside_temp_selected).state,
                   id(oq_system_supply_temp).state, defrost_factor,
                   id(oq_cold_start_session_active) && !id(oq_cold_start_hp_blocked));
}

}  // namespace oq_ot_display
#endif
