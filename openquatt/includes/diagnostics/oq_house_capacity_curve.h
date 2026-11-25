#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "../performance/oq_water_performance.h"

namespace oq_house_capacity {

inline constexpr int MIN_OUTSIDE_C = -15;
inline constexpr int LEGACY_MAX_OUTSIDE_C = 7;
inline constexpr int V2_MAX_OUTSIDE_C = 20;
inline constexpr std::array<int, 2> SUPPLY_C{35, 45};
inline constexpr int16_t UNAVAILABLE = -1;
inline constexpr size_t MAX_POINTS = V2_MAX_OUTSIDE_C - MIN_OUTSIDE_C + 1 + 7;

struct Unit {
  oq_odu::Variant variant{oq_odu::Variant::UNKNOWN};
  std::array<uint8_t, oq_odu::EXTENDED_PHYSICAL_LEVEL_MAX> frequencies{};

  bool operator==(const Unit& other) const { return variant == other.variant && frequencies == other.frequencies; }
};

struct Installation {
  std::array<Unit, 2> units{};
  int flow_lph{0}, limit_w{0}, cap_hz{0};
  float max_water_c{0};
  bool duo{false}, available{false};

  bool operator==(const Installation& other) const {
    return units == other.units && flow_lph == other.flow_lph && limit_w == other.limit_w && cap_hz == other.cap_hz &&
           max_water_c == other.max_water_c && duo == other.duo && available == other.available;
  }
};

inline bool is_v2(oq_odu::Variant variant) {
  return variant == oq_odu::Variant::V2_OLD_MODEL || variant == oq_odu::Variant::V2_NEW_MODEL;
}

inline bool v2_only(const Installation& installation) {
  return is_v2(installation.units[0].variant) && (!installation.duo || is_v2(installation.units[1].variant));
}

inline bool has_v2(const Installation& installation) {
  return is_v2(installation.units[0].variant) || is_v2(installation.units[1].variant);
}

// These are the ambient edges with distinct left and right cells in the V2 map.
inline bool v2_boundary(int outside_c) {
  return outside_c == -12 || outside_c == -5 || outside_c == 0 || outside_c == 5 || outside_c == 11 ||
         outside_c == 14 || outside_c == 19;
}

// With a specified final outlet and steady flow the series balance can be solved
// upstream, one stage at a time. The result needs no search outside the map.
inline int16_t power_at(const Installation& installation, float outside_c, int supply_c) {
  if (supply_c > installation.max_water_c) return UNAVAILABLE;
  const auto& first_unit = installation.units[0];
  const auto& second_unit = installation.units[1];
  const auto highest_first = *std::max_element(first_unit.frequencies.begin(), first_unit.frequencies.end());
  const auto highest_second = *std::max_element(second_unit.frequencies.begin(), second_unit.frequencies.end());
  const float water_w_per_k = installation.flow_lph * 4186.0f / 3600.0f;
  const float first_min_supply = is_v2(first_unit.variant) ? oq_v2_model::data::SUPPLY_C[0] : oq_perf_v1::T_sup_bp[0];
  bool modeled = false;
  bool allowed = false;
  float best_w = 0;
  const auto evaluate_first = [&](const oq_water_performance::Prediction& second, float first_outlet_c) {
    if (second.thermal_w > 0) {
      modeled = true;
      if (second.electrical_w > installation.limit_w) return true;
      best_w = std::max(best_w, second.thermal_w);
    }
    for (const auto frequency : first_unit.frequencies) {
      if (frequency == 0) break;
      allowed = true;
      if (first_outlet_c < first_min_supply) {
        // A valid downstream stage can place the upstream outlet beyond the
        // map. That unknown combination can affect the maximum.
        if (second.electrical_w < installation.limit_w) return false;
        break;
      }
      const auto first = oq_water_performance::observed(first_unit.variant, frequency, outside_c, first_outlet_c);
      if (!first.valid()) {
        // A missing top stand cannot silently become a lower maximum. Masks
        // at the low end of the map are common, especially in cold V2 cells.
        if (frequency == highest_first && second.electrical_w < installation.limit_w) return false;
        continue;
      }
      modeled = true;
      if (first.electrical_w + second.electrical_w <= installation.limit_w)
        best_w = std::max(best_w, first.thermal_w + second.thermal_w);
    }
    return true;
  };
  if (!evaluate_first({0, 0, static_cast<float>(supply_c)}, supply_c)) return UNAVAILABLE;
  if (installation.duo) {
    for (const auto frequency : second_unit.frequencies) {
      if (frequency == 0) break;
      allowed = true;
      const auto second = oq_water_performance::observed(second_unit.variant, frequency, outside_c, supply_c);
      if (!second.valid()) {
        if (frequency == highest_second) return UNAVAILABLE;
        continue;
      }
      if (!evaluate_first(second, supply_c - second.thermal_w / water_w_per_k)) return UNAVAILABLE;
    }
  }
  return modeled || !allowed ? static_cast<int16_t>(std::lround(best_w)) : UNAVAILABLE;
}

struct Point {
  int16_t outside_centi{0};
  std::array<int16_t, SUPPLY_C.size()> watts{};
};

struct Curve {
  Installation installation{};
  std::array<Point, MAX_POINTS> points{};
  size_t count{0};
  int next_outside_c{MIN_OUTSIDE_C};
  bool left_of_boundary{false};

  bool complete() const {
    return installation.available && next_outside_c > (v2_only(installation) ? V2_MAX_OUTSIDE_C : LEGACY_MAX_OUTSIDE_C);
  }

  // Called by one main-loop owner. Publish only after both profiles for the
  // same configuration are complete; a changed input invalidates all points.
  void step(const Installation& current) {
    if (!(current == installation)) {
      installation = current;
      count = 0;
      next_outside_c = MIN_OUTSIDE_C;
      left_of_boundary = has_v2(installation) && v2_boundary(next_outside_c);
    }
    if (!installation.available || complete()) return;
    const int centi = next_outside_c * 100 - (left_of_boundary ? 1 : 0);
    auto& point = points[count++];
    point.outside_centi = static_cast<int16_t>(centi);
    for (size_t i = 0; i < SUPPLY_C.size(); ++i) point.watts[i] = power_at(installation, centi / 100.0f, SUPPLY_C[i]);
    if (left_of_boundary) {
      left_of_boundary = false;
    } else {
      ++next_outside_c;
      left_of_boundary = has_v2(installation) && v2_boundary(next_outside_c);
    }
  }
};

template <typename Json>
inline void write_json(Json& json, const Curve& curve) {
  if (!curve.complete()) {
    json.add("null");
    return;
  }
  json.add("{\"flow_lph\":%d,\"limit_w\":%d,\"cap_hz\":%d,\"points\":[", curve.installation.flow_lph,
           curve.installation.limit_w, curve.installation.cap_hz);
  for (size_t index = 0; index < curve.count; ++index) {
    const auto& point = curve.points[index];
    json.add("%s[%d", index == 0 ? "" : ",", static_cast<int>(point.outside_centi));
    for (const auto watts : point.watts) {
      if (watts == UNAVAILABLE)
        json.add(",null");
      else
        json.add(",%d", static_cast<int>(watts));
    }
    json.add("]");
  }
  json.add("]}");
}

}  // namespace oq_house_capacity
