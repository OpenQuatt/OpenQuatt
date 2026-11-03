#pragma once

#include "hp_perf_map.h"

namespace oq_perf {

enum class ReferenceGeneration : uint8_t { UNKNOWN = 0, V1, V1_5, V2 };

// Use the entire reference domain. An instantaneous capacity/demand estimate
// cannot bound an unobserved interval. V2 clamps each native frequency surface
// before frequency interpolation, so its raw (unclamped) cells are not outputs.
inline constexpr float kV1ReferenceMaximumW = oq_perf_v1::maximum_reference_power_th_w();
inline constexpr float kV2ReferenceMaximumW = static_cast<float>(oq_v2_model::data::PTH_MAX_W);
static_assert(kV1ReferenceMaximumW > 0.0f && kV1ReferenceMaximumW <= 50000.0f);
static_assert(kV2ReferenceMaximumW > 0.0f && kV2ReferenceMaximumW <= 50000.0f);

// Zero means no supported generation envelope: the consumer must retain its
// broad validation bound, never interpret unknown hardware as zero heat.
inline constexpr float reference_maximum_heating_w(ReferenceGeneration generation, bool duo) {
  const float single = generation == ReferenceGeneration::V1 || generation == ReferenceGeneration::V1_5
                           ? kV1ReferenceMaximumW
                       : generation == ReferenceGeneration::V2 ? kV2ReferenceMaximumW
                                                               : 0.0f;
  return single * (duo ? 2.0f : 1.0f);
}

}  // namespace oq_perf
