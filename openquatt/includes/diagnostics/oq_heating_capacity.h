#pragma once

#include <cmath>

#include "../control/oq_compressor_frequency_policy.h"
#include "../performance/hp_perf_frequency.h"
#include "../performance/oq_water_performance.h"
#include "../control/oq_power_house_dispatch_logic.h"

namespace oq_heating_capacity {

// Read-only maximum output estimate. No demand, dispatch or boiler state is
// written; it uses the same water-side model as the heating strategies.
template <typename FrequencyContext>
inline float estimate(const FrequencyContext& frequency, bool duo, float outside_c, float supply_c,
                      const oq_hp_candidate::HpCandidateState& hp1, const oq_hp_candidate::HpCandidateState& hp2,
                      float soft_limit_w, float peak_limit_w, bool hp1_valve_defrost, bool hp2_valve_defrost,
                      float defrost_factor, bool allow_low_supply_boundary_estimate,
                      const oq_water_performance::CandidateModel* model = nullptr) {
  const oq_power_house_dispatch::DispatchTuning limits{soft_limit_w, peak_limit_w};
  if (!std::isfinite(outside_c) || (!std::isfinite(supply_c) && !(model && model->coupled)) ||
      !oq_power_house_dispatch::electrical_limits_valid(limits))
    return NAN;
  if (frequency.cap_hz < 0 || frequency.cap_hz > oq_frequency_policy::MAX_POLICY_FREQUENCY_HZ) return NAN;

  oq_power_house_dispatch::DispatchInput input;
  input.duo = duo;
  input.performance_valid = true;
  if (model && model->coupled) {
    input.pair_power = &oq_water_performance::CandidateModel::dispatch;
    input.pair_context = model;
  }
  if (!std::isfinite(defrost_factor)) defrost_factor = 0.55f;
  defrost_factor = std::max(0.10f, std::min(1.00f, defrost_factor));
  bool any_allowed_frequency = false;
  const auto build_hp = [&](oq_power_house_dispatch::HpInput& output, bool is_hp1,
                            const oq_hp_candidate::HpCandidateState& candidate, bool valve_defrost) {
    output.candidate = candidate;
    output.levels[0] = {true, true, true, 0.0f, 0.0f};
    if (!oq_hp_candidate::may_serve_candidate(candidate)) return true;
    if (candidate.link_suspect || !oq_frequency_policy::valid_frequency_range(frequency.excluded_range(is_hp1)))
      return false;
    bool allowed_frequency = false;
    bool modeled_candidate = false;
    const int maximum = frequency.automatic_level_limit(is_hp1, 2);
    if (maximum <= 0) return false;
    for (int level = 1; level <= maximum; ++level) {
      const auto prediction =
          oq_perf::predict_candidate(frequency, frequency.performance_variant(is_hp1), is_hp1, level, outside_c,
                                     supply_c, allow_low_supply_boundary_estimate);
      // Unknown runtime frequencies or an unsupported model are not zero
      // capacity. A known frequency blocked by policy needs no prediction.
      if (!prediction.runtime_frequency_known) return false;
      if (!prediction.frequency_policy_allowed) continue;
      allowed_frequency = any_allowed_frequency = true;
      output.levels[level].allowed = true;
      output.levels[level].frequency_hz = static_cast<uint8_t>(prediction.runtime_frequency_hz);
      // Masked map points cannot be dispatched. Keep the maximum of the
      // remaining modeled candidates, just like Power House dispatch.
      const auto power = model ? model->unit(is_hp1, level)
                               : oq_water_performance::Prediction{prediction.performance.pth_w,
                                                                  prediction.performance.pel_w, supply_c};
      if (!power.valid() || power.thermal_w <= 0) continue;
      modeled_candidate = true;
      const float thermal_w = power.thermal_w * (valve_defrost ? defrost_factor : 1.0f);
      output.levels[level] = {
          true, true, true, thermal_w, power.electrical_w, static_cast<uint8_t>(prediction.runtime_frequency_hz)};
    }
    return !allowed_frequency || modeled_candidate ||
           (model && model->coupled && frequency.performance_variant(is_hp1) != oq_odu::Variant::UNKNOWN);
  };
  if (!build_hp(input.hp1, true, hp1, hp1_valve_defrost)) return NAN;
  if (duo && !build_hp(input.hp2, false, hp2, hp2_valve_defrost)) return NAN;
  bool model_available = false;
  const float capacity = oq_power_house_dispatch::dispatch_capacity(input, limits, &model_available);
  return model && model->coupled && any_allowed_frequency && !model_available ? NAN : capacity;
}

inline float fresh_outlet(uint32_t now_ms, uint32_t stale_ms, bool online, uint32_t updated_ms, bool has_state,
                          float value) {
  return online && updated_ms > 0 && static_cast<uint32_t>(now_ms - updated_ms) <= stale_ms && has_state &&
                 std::isfinite(value)
             ? value
             : NAN;
}

}  // namespace oq_heating_capacity
