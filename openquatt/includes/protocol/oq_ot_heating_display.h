#pragma once

#include <algorithm>
#include <cmath>

#include "../performance/hp_perf_frequency.h"
#include "../sources/oq_raw_receipt.h"

namespace oq_ot_display {

// Thermostat-side virtual heat source. Capacity/modulation always describe
// the installed HPs, independently of the downstream boiler connection.
struct HeatPump {
  oq_odu::Variant variant{oq_odu::Variant::UNKNOWN};
  bool online{false};
  oq_sources::RawFloatReceipt working_mode{};
  oq_sources::RawFloatReceipt compressor_frequency{};
  oq_sources::RawFloatReceipt defrost{};
};

struct Display {
  float max_capacity_kw{NAN};
  float min_modulation_percent{NAN};
  float modulation_percent{NAN};
  bool heating_active{false};
  bool cooling_active{false};
};

inline bool activity_known(const HeatPump& hp, uint64_t now_ms, uint64_t stale_ms) {
  return hp.online && hp.working_mode.fresh(now_ms, stale_ms) && hp.compressor_frequency.fresh(now_ms, stale_ms) &&
         (hp.working_mode.value == 0.0f || hp.working_mode.value == 1.0f || hp.working_mode.value == 2.0f ||
          hp.working_mode.value == 4.0f) &&
         hp.compressor_frequency.value >= 0.0f && hp.compressor_frequency.value <= 120.0f;
}

inline bool active_in_mode(const HeatPump& hp, uint64_t now_ms, uint64_t stale_ms, float mode) {
  return activity_known(hp, now_ms, stale_ms) && hp.working_mode.value == mode && hp.compressor_frequency.value > 0.0f;
}

inline bool heating_active(const HeatPump& hp, uint64_t now_ms, uint64_t stale_ms) {
  return active_in_mode(hp, now_ms, stale_ms, 2.0f) || active_in_mode(hp, now_ms, stale_ms, 4.0f);
}

inline float v1_power(int level, float outside_c, float supply_c) {
  if (level < 1 || level > 10 || !std::isfinite(outside_c) || !std::isfinite(supply_c)) return NAN;
  const int ai = oq_perf_v1::find_interval(oq_perf_v1::T_amb_bp, oq_perf_v1::N_AMB, outside_c);
  const int si = oq_perf_v1::find_interval(oq_perf_v1::T_sup_bp, oq_perf_v1::N_SUP, supply_c);
  const float tx = std::max(0.0f, std::min(1.0f, (outside_c - oq_perf_v1::T_amb_bp[ai]) /
                                                     (oq_perf_v1::T_amb_bp[ai + 1] - oq_perf_v1::T_amb_bp[ai])));
  const float ty = std::max(0.0f, std::min(1.0f, (supply_c - oq_perf_v1::T_sup_bp[si]) /
                                                     (oq_perf_v1::T_sup_bp[si + 1] - oq_perf_v1::T_sup_bp[si])));
  float watts = 0.0f;
  for (int ambient_side = 0; ambient_side < 2; ++ambient_side) {
    for (int supply_side = 0; supply_side < 2; ++supply_side) {
      const float weight = (ambient_side ? tx : 1.0f - tx) * (supply_side ? ty : 1.0f - ty);
      // Exact measured nodes do not depend on masked zero-weight neighbours.
      if (weight == 0.0f) continue;
      const float cell = oq_perf_v1::P_th_W[ai + ambient_side][si + supply_side][level - 1];
      if (!std::isfinite(cell)) return NAN;
      watts += cell * weight;
    }
  }
  return watts;
}

inline float power_at_frequency(oq_odu::Variant variant, float frequency_hz, float outside_c, float supply_c,
                                bool boundary_estimate = false) {
  if (variant == oq_odu::Variant::V1 || variant == oq_odu::Variant::V1_5)
    return oq_perf::interp_frequency_axis(oq_perf::V1_HEATING_FREQUENCIES_HZ, frequency_hz,
                                          [=](int level) { return v1_power(level, outside_c, supply_c); });
  const auto prediction = oq_perf::predict_heating_hz(variant, frequency_hz, outside_c, supply_c, boundary_estimate);
  return prediction.available ? prediction.pth_w : NAN;
}

inline float reference_power(const HeatPump& hp, int level) {
  if (level < 1 || level > 10) return NAN;
  const bool v2 = hp.variant == oq_odu::Variant::V2_OLD_MODEL || hp.variant == oq_odu::Variant::V2_NEW_MODEL;
  return power_at_frequency(hp.variant, oq_perf::model_frequency_hz(v2, level), 7.0f, 35.0f);
}

struct Reference {
  float maximum_w{NAN};
  float minimum_w{NAN};
};

inline Reference reference(const HeatPump& hp) {
  Reference result;
  for (int level = 1; level <= 10; ++level) {
    const float watts = reference_power(hp, level);
    if (!std::isfinite(watts) || watts <= 0.0f) continue;
    result.maximum_w = std::isfinite(result.maximum_w) ? std::max(result.maximum_w, watts) : watts;
    result.minimum_w = std::isfinite(result.minimum_w) ? std::min(result.minimum_w, watts) : watts;
  }
  return result;
}

inline float current_power(const HeatPump& hp, uint64_t now_ms, uint64_t stale_ms, float outside_c, float supply_c,
                           float defrost_factor, bool boundary_estimate) {
  if (!activity_known(hp, now_ms, stale_ms)) return NAN;
  if (!heating_active(hp, now_ms, stale_ms)) return 0.0f;
  const bool defrost_mode = hp.working_mode.value == 4.0f;
  if (!defrost_mode && (!hp.defrost.fresh(now_ms, stale_ms) || (hp.defrost.value != 0.0f && hp.defrost.value != 1.0f)))
    return NAN;
  if (!std::isfinite(outside_c) || !std::isfinite(supply_c)) return NAN;
  const float watts =
      power_at_frequency(hp.variant, hp.compressor_frequency.value, outside_c, supply_c, boundary_estimate);
  if (!std::isfinite(watts) || watts < 0.0f) return NAN;
  if (!defrost_mode && hp.defrost.value == 0.0f) return watts;
  if (!std::isfinite(defrost_factor) || defrost_factor < 0.0f || defrost_factor > 1.0f) return NAN;
  return watts * defrost_factor;
}

inline Display calculate(const HeatPump& hp1, const HeatPump& hp2, bool duo, uint64_t now_ms, uint64_t stale_ms,
                         float outside_c, float supply_c, float defrost_factor, bool boundary_estimate) {
  Display result;
  result.heating_active = heating_active(hp1, now_ms, stale_ms) || (duo && heating_active(hp2, now_ms, stale_ms));
  result.cooling_active =
      active_in_mode(hp1, now_ms, stale_ms, 1.0f) || (duo && active_in_mode(hp2, now_ms, stale_ms, 1.0f));
  const auto first_reference = reference(hp1);
  const auto second_reference = duo ? reference(hp2) : first_reference;
  const float hp1_max_w = first_reference.maximum_w;
  const float hp1_min_w = first_reference.minimum_w;
  const float hp2_max_w = duo ? second_reference.maximum_w : 0.0f;
  const float hp2_min_w = second_reference.minimum_w;
  // Unknown installed capacity must never become a smaller single-HP total.
  if (!std::isfinite(hp1_max_w) || !std::isfinite(hp1_min_w) || !std::isfinite(hp2_max_w) || !std::isfinite(hp2_min_w))
    return result;
  const float max_w = hp1_max_w + hp2_max_w;
  result.max_capacity_kw = max_w / 1000.0f;
  // A Duo can run either HP alone; its minimum is the smallest running HP.
  result.min_modulation_percent = std::min(hp1_min_w, hp2_min_w) * 100.0f / max_w;
  const float hp1_w = current_power(hp1, now_ms, stale_ms, outside_c, supply_c, defrost_factor, boundary_estimate);
  const float hp2_w =
      duo ? current_power(hp2, now_ms, stale_ms, outside_c, supply_c, defrost_factor, boundary_estimate) : 0.0f;
  if (std::isfinite(hp1_w) && std::isfinite(hp2_w))
    result.modulation_percent = std::max(0.0f, std::min(100.0f, (hp1_w + hp2_w) * 100.0f / max_w));
  return result;
}

inline bool flame_on(const Display& hp, bool opentherm_selected, bool boiler_flame, bool relay_active) {
  return hp.heating_active || (opentherm_selected ? boiler_flame : relay_active);
}

}  // namespace oq_ot_display
