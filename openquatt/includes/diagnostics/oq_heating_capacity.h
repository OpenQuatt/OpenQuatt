#pragma once

#include <cmath>

#include "../control/oq_compressor_frequency_policy.h"
#include "../performance/hp_perf_frequency.h"
#include "../control/oq_power_house_dispatch_logic.h"

namespace oq_heating_capacity {

// Read-only maximum output estimate. No demand, dispatch or boiler state is
// written, and both heating strategies use the same measured supply input.
template <typename FrequencyContext>
inline float estimate(const FrequencyContext& frequency, bool duo, float outside_c, float supply_c,
                      const oq_hp_candidate::HpCandidateState& hp1, const oq_hp_candidate::HpCandidateState& hp2,
                      float soft_limit_w, float peak_limit_w, bool hp1_valve_defrost, bool hp2_valve_defrost,
                      float defrost_factor, bool allow_low_supply_boundary_estimate) {
  const oq_power_house_dispatch::DispatchTuning limits{soft_limit_w, peak_limit_w};
  if (!std::isfinite(outside_c) || !std::isfinite(supply_c) ||
      !oq_power_house_dispatch::electrical_limits_valid(limits))
    return NAN;
  if (frequency.cap_hz < 0 || frequency.cap_hz > oq_frequency_policy::MAX_POLICY_FREQUENCY_HZ) return NAN;

  oq_power_house_dispatch::DispatchInput input;
  input.duo = duo;
  input.performance_valid = true;
  if (!std::isfinite(defrost_factor)) defrost_factor = 0.55f;
  defrost_factor = std::max(0.10f, std::min(1.00f, defrost_factor));
  const auto build_hp = [&](oq_power_house_dispatch::HpInput& output, bool is_hp1,
                            const oq_hp_candidate::HpCandidateState& candidate, bool valve_defrost) {
    output.candidate = candidate;
    output.levels[0] = {true, true, true, 0.0f, 0.0f};
    if (!oq_hp_candidate::may_serve_candidate(candidate)) return true;
    if (candidate.link_suspect || !oq_frequency_policy::valid_frequency_range(frequency.excluded_range(is_hp1)))
      return false;
    bool allowed_frequency = false;
    bool modeled_candidate = false;
    for (int level = 1; level <= oq_power_house_dispatch::kMaxLevel; ++level) {
      const auto prediction =
          oq_perf::predict_candidate(frequency, frequency.performance_variant(is_hp1), is_hp1, level, outside_c,
                                     supply_c, allow_low_supply_boundary_estimate);
      // Unknown runtime frequencies or an unsupported model are not zero
      // capacity. A known frequency blocked by policy needs no prediction.
      if (!prediction.runtime_frequency_known) return false;
      if (!prediction.frequency_policy_allowed) continue;
      allowed_frequency = true;
      // Masked map points cannot be dispatched. Keep the maximum of the
      // remaining modeled candidates, just like Power House dispatch.
      if (!prediction.usable_for_running_optimization()) continue;
      modeled_candidate = true;
      const float thermal_w = prediction.performance.pth_w * (valve_defrost ? defrost_factor : 1.0f);
      output.levels[level] = {true, true, true, thermal_w, prediction.performance.pel_w};
    }
    return !allowed_frequency || modeled_candidate;
  };
  if (!build_hp(input.hp1, true, hp1, hp1_valve_defrost)) return NAN;
  if (duo && !build_hp(input.hp2, false, hp2, hp2_valve_defrost)) return NAN;
  return oq_power_house_dispatch::dispatch_capacity(input, limits);
}

inline float fresh_outlet(uint32_t now_ms, uint32_t stale_ms, bool online, uint32_t updated_ms, bool has_state,
                          float value) {
  return online && updated_ms > 0 && static_cast<uint32_t>(now_ms - updated_ms) <= stale_ms && has_state &&
                 std::isfinite(value)
             ? value
             : NAN;
}

}  // namespace oq_heating_capacity
