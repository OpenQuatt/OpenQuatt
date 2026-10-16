#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "../sources/oq_raw_receipt.h"

// Experimental, observation-only water-performance calculations. No I/O,
// allocation, actuator access, persistent state or start authorization.
namespace oq_water_shadow {

enum class Status : uint8_t {
  VALID = 0,
  OFF,
  DISABLED,
  INVALID_INPUT,
  STALE,
  TIME_SKEW,
  TRANSIENT,
  UNKNOWN_ORDER,
  HYDRAULIC_MISMATCH,
  INVALID_FLOW,
  MODEL_UNAVAILABLE,
  NO_CONVERGENCE,
  FREQUENCY_POLICY,
  SETTLING,
  CONTEXT_CHANGED,
  ALLOCATION_FAILED,
  BASELINE_MISMATCH,
};

inline const char* status_name(Status status) {
  static constexpr const char* names[] = {"valid",
                                          "off",
                                          "disabled",
                                          "invalid_input",
                                          "stale",
                                          "time_skew",
                                          "transient",
                                          "unknown_order",
                                          "hydraulic_mismatch",
                                          "invalid_flow",
                                          "model_unavailable",
                                          "no_convergence",
                                          "frequency_policy",
                                          "settling",
                                          "context_changed",
                                          "allocation_failed",
                                          "baseline_mismatch"};
  const auto index = static_cast<unsigned>(status);
  return index < sizeof(names) / sizeof(names[0]) ? names[index] : "invalid_input";
}

struct Limits {
  uint64_t water_age_ms = 30000;
  uint64_t outside_age_ms = 1800000;
  uint64_t maximum_skew_ms = 12000;
  float minimum_flow_lph = 300.0f;
  float maximum_flow_lph = 3000.0f;
  float link_tolerance_c = 0.5f;
  float balance_tolerance_w = 1.0f;
  uint8_t maximum_iterations = 24;
};

struct Snapshot {
  uint64_t now_ms = 0;
  oq_sources::RawFloatReceipt outside;
  oq_sources::RawFloatReceipt flow;
  std::array<oq_sources::RawFloatReceipt, 2> inlet;
  std::array<oq_sources::RawFloatReceipt, 2> outlet;
  std::array<oq_sources::RawFloatReceipt, 2> frequency;
  std::array<bool, 2> online{};
  std::array<bool, 2> heating{};
  std::array<bool, 2> transient{};
  std::array<uint64_t, 2> context_valid_until_ms{};
  bool order_confirmed = false;
  bool hp1_upstream = true;
  bool common_flow_confirmed = false;
};

struct ModelPoint {
  float thermal_w = NAN;
  float electrical_w = NAN;
  bool available = false;
  bool boundary_estimate = false;
};

struct UnitPrediction {
  Status status = Status::INVALID_INPUT;
  float outlet_c = NAN;
  float thermal_w = NAN;
  float electrical_w = NAN;
  float balance_error_w = NAN;
  uint8_t evaluations = 0;
  bool boundary_estimate = false;

  bool valid() const { return status == Status::VALID || status == Status::OFF; }
};

struct PairPrediction {
  Status status = Status::INVALID_INPUT;
  std::array<UnitPrediction, 2> unit;
  float thermal_w = NAN;
  float electrical_w = NAN;

  bool valid() const { return status == Status::VALID || status == Status::OFF; }
};

inline bool valid_frequency(float hz) { return std::isfinite(hz) && hz >= 0.0f && hz <= 90.0f; }

inline bool valid_model(const ModelPoint& point) {
  return point.available && std::isfinite(point.thermal_w) && point.thermal_w >= 0.0f &&
         std::isfinite(point.electrical_w) && point.electrical_w >= 0.0f;
}

inline Status receipt_status(const oq_sources::RawFloatReceipt& receipt, uint64_t now_ms, uint64_t max_age_ms) {
  if (!receipt.received || !receipt.valid || receipt.received_ms == 0 || !std::isfinite(receipt.value))
    return Status::INVALID_INPUT;
  return receipt.fresh(now_ms, max_age_ms) ? Status::VALID : Status::STALE;
}

inline bool aligned(uint64_t first_ms, uint64_t second_ms, uint64_t maximum_skew_ms) {
  const uint64_t difference = first_ms >= second_ms ? first_ms - second_ms : second_ms - first_ms;
  return difference <= maximum_skew_ms;
}

inline bool stationary_context(const oq_sources::RawFloatReceipt& mode, const oq_sources::RawFloatReceipt& defrost,
                               const oq_sources::RawFloatReceipt& status_2108,
                               const oq_sources::RawFloatReceipt& status_2119, uint64_t now_ms,
                               const Limits& limits = {}) {
  uint64_t earliest_ms = now_ms, latest_ms = 0;
  for (const auto* receipt : {&mode, &defrost, &status_2108, &status_2119}) {
    if (receipt_status(*receipt, now_ms, limits.water_age_ms) != Status::VALID) return false;
    if (receipt->received_ms < earliest_ms) earliest_ms = receipt->received_ms;
    if (receipt->received_ms > latest_ms) latest_ms = receipt->received_ms;
  }
  const auto word_valid = [](float value) { return value >= 0.0f && value <= 65535.0f && std::trunc(value) == value; };
  if (!aligned(earliest_ms, latest_ms, limits.maximum_skew_ms) || (mode.value != 0.0f && mode.value != 2.0f) ||
      defrost.value != 0.0f || !word_valid(status_2108.value) || !word_valid(status_2119.value))
    return false;
  return (static_cast<uint16_t>(status_2108.value) & 0x0040U) == 0 && static_cast<uint16_t>(status_2119.value) == 0;
}

template <typename Model>
inline UnitPrediction predict_observed(const Snapshot& snapshot, unsigned hp, const Limits& limits, Model model) {
  UnitPrediction result;
  if (hp >= 2 || !snapshot.online[hp]) return result;
  if (snapshot.transient[hp]) {
    result.status = Status::TRANSIENT;
    return result;
  }
  if (snapshot.now_ms > snapshot.context_valid_until_ms[hp]) {
    result.status = Status::STALE;
    return result;
  }
  for (const auto* receipt : {&snapshot.outlet[hp], &snapshot.frequency[hp]}) {
    result.status = receipt_status(*receipt, snapshot.now_ms, limits.water_age_ms);
    if (result.status != Status::VALID) return result;
  }
  const float hz = snapshot.frequency[hp].value;
  if (!valid_frequency(hz)) {
    result.status = Status::FREQUENCY_POLICY;
    return result;
  }
  if (!aligned(snapshot.outlet[hp].received_ms, snapshot.frequency[hp].received_ms, limits.maximum_skew_ms)) {
    result.status = Status::TIME_SKEW;
    return result;
  }
  if (hz == 0.0f && !snapshot.heating[hp]) return {Status::OFF, snapshot.outlet[hp].value, 0.0f, 0.0f, 0.0f};
  if (hz == 0.0f || !snapshot.heating[hp]) {
    result.status = Status::TRANSIENT;
    return result;
  }
  result.status = receipt_status(snapshot.outside, snapshot.now_ms, limits.outside_age_ms);
  if (result.status != Status::VALID) return result;
  const auto point = model(hp, hz, snapshot.outside.value, snapshot.outlet[hp].value);
  if (!valid_model(point)) {
    result.status = Status::MODEL_UNAVAILABLE;
    return result;
  }
  return {Status::VALID,          snapshot.outlet[hp].value, point.thermal_w, point.electrical_w, NAN, 1,
          point.boundary_estimate};
}

// Conservative stationary solver: require a valid map at Tin and at the
// zero-order upper bracket Tin+P(Tin)/K. Unsupported brackets remain visibly
// unavailable, even if an unproven search elsewhere could find a root.
template <typename Model>
inline UnitPrediction solve_unit(unsigned hp, float hz, float outside_c, float inlet_c, float capacity_w_per_k,
                                 const Limits& limits, Model model) {
  UnitPrediction result;
  if (!valid_frequency(hz)) {
    result.status = Status::FREQUENCY_POLICY;
    return result;
  }
  if (hp >= 2 || !std::isfinite(outside_c) || !std::isfinite(inlet_c) || !std::isfinite(capacity_w_per_k) ||
      capacity_w_per_k <= 0.0f || !std::isfinite(limits.balance_tolerance_w) || limits.balance_tolerance_w <= 0.0f ||
      limits.maximum_iterations == 0 || limits.maximum_iterations > 24)
    return result;
  if (hz == 0.0f) return {Status::OFF, inlet_c, 0.0f, 0.0f, 0.0f};
  float low_c = inlet_c;
  auto point = model(hp, hz, outside_c, low_c);
  result.evaluations = 1;
  if (!valid_model(point)) {
    result.status = Status::MODEL_UNAVAILABLE;
    return result;
  }
  float high_c = inlet_c + point.thermal_w / capacity_w_per_k;
  if (!std::isfinite(high_c)) return result;
  bool boundary_estimate = point.boundary_estimate;
  point = model(hp, hz, outside_c, high_c);
  ++result.evaluations;
  if (!valid_model(point)) {
    result.status = Status::MODEL_UNAVAILABLE;
    return result;
  }
  boundary_estimate |= point.boundary_estimate;
  float residual_w = capacity_w_per_k * (high_c - inlet_c) - point.thermal_w;
  if (residual_w < -limits.balance_tolerance_w) {
    result.status = Status::NO_CONVERGENCE;
    return result;
  }
  float outlet_c = high_c;
  for (unsigned step = 0; step < limits.maximum_iterations; ++step) {
    if (std::fabs(residual_w) <= limits.balance_tolerance_w)
      return {Status::VALID, outlet_c,           point.thermal_w,  point.electrical_w,
              residual_w,    result.evaluations, boundary_estimate};
    outlet_c = low_c + (high_c - low_c) * 0.5f;
    point = model(hp, hz, outside_c, outlet_c);
    ++result.evaluations;
    if (!valid_model(point)) {
      result.status = Status::MODEL_UNAVAILABLE;
      return result;
    }
    boundary_estimate |= point.boundary_estimate;
    residual_w = capacity_w_per_k * (outlet_c - inlet_c) - point.thermal_w;
    if (residual_w > 0.0f)
      high_c = outlet_c;
    else
      low_c = outlet_c;
  }
  if (std::fabs(residual_w) <= limits.balance_tolerance_w)
    return {Status::VALID, outlet_c,           point.thermal_w,  point.electrical_w,
            residual_w,    result.evaluations, boundary_estimate};
  result.status = Status::NO_CONVERGENCE;
  return result;
}

template <typename Model, typename Solver>
inline PairPrediction predict_series_using(const Snapshot& snapshot, const std::array<float, 2>& hz,
                                           const Limits& limits, Model model, Solver solver) {
  PairPrediction result;
  if (!valid_frequency(hz[0]) || !valid_frequency(hz[1])) {
    result.status = Status::FREQUENCY_POLICY;
    return result;
  }
  if (!snapshot.order_confirmed) {
    result.status = Status::UNKNOWN_ORDER;
    return result;
  }
  // OpenQuatt Duo has a fixed installation contract: HP1 precedes HP2.
  // A reversed installation is a fault, never a selectable alternative model.
  if (!snapshot.hp1_upstream) {
    result.status = Status::HYDRAULIC_MISMATCH;
    return result;
  }
  if (!snapshot.common_flow_confirmed) {
    result.status = Status::INVALID_FLOW;
    return result;
  }
  for (const bool transient : snapshot.transient) {
    if (transient) {
      result.status = Status::TRANSIENT;
      return result;
    }
  }
  if (snapshot.now_ms > snapshot.context_valid_until_ms[0] || snapshot.now_ms > snapshot.context_valid_until_ms[1]) {
    result.status = Status::STALE;
    return result;
  }
  result.status = receipt_status(snapshot.outside, snapshot.now_ms, limits.outside_age_ms);
  if (result.status != Status::VALID) return result;
  result.status = receipt_status(snapshot.flow, snapshot.now_ms, limits.water_age_ms);
  if (result.status != Status::VALID) return result;
  if (!std::isfinite(limits.minimum_flow_lph) || !std::isfinite(limits.maximum_flow_lph) ||
      limits.minimum_flow_lph <= 0.0f || limits.maximum_flow_lph < limits.minimum_flow_lph ||
      snapshot.flow.value < limits.minimum_flow_lph || snapshot.flow.value > limits.maximum_flow_lph) {
    result.status = Status::INVALID_FLOW;
    return result;
  }
  constexpr unsigned first = 0, second = 1;
  uint64_t earliest_ms = snapshot.flow.received_ms;
  uint64_t latest_ms = snapshot.flow.received_ms;
  for (unsigned hp = 0; hp < 2; ++hp) {
    if (!snapshot.online[hp]) {
      result.status = Status::INVALID_INPUT;
      return result;
    }
    for (const auto* receipt : {&snapshot.inlet[hp], &snapshot.outlet[hp], &snapshot.frequency[hp]}) {
      result.status = receipt_status(*receipt, snapshot.now_ms, limits.water_age_ms);
      if (result.status != Status::VALID) return result;
      if (receipt->received_ms < earliest_ms) earliest_ms = receipt->received_ms;
      if (receipt->received_ms > latest_ms) latest_ms = receipt->received_ms;
    }
    if (!valid_frequency(snapshot.frequency[hp].value)) {
      result.status = Status::FREQUENCY_POLICY;
      return result;
    }
  }
  if (!aligned(earliest_ms, latest_ms, limits.maximum_skew_ms)) {
    result.status = Status::TIME_SKEW;
    return result;
  }
  if (!std::isfinite(limits.link_tolerance_c) || limits.link_tolerance_c < 0.0f ||
      std::fabs(snapshot.outlet[first].value - snapshot.inlet[second].value) > limits.link_tolerance_c) {
    result.status = Status::HYDRAULIC_MISMATCH;
    return result;
  }
  const float capacity = snapshot.flow.value * (4186.0f / 3600.0f);
  result.unit[first] =
      solver(first, hz[first], snapshot.outside.value, snapshot.inlet[first].value, capacity, limits, model);
  if (!result.unit[first].valid()) {
    result.status = result.unit[first].status;
    return result;
  }
  result.unit[second] =
      solver(second, hz[second], snapshot.outside.value, result.unit[first].outlet_c, capacity, limits, model);
  if (!result.unit[second].valid()) {
    result.status = result.unit[second].status;
    return result;
  }
  result.thermal_w = result.unit[0].thermal_w + result.unit[1].thermal_w;
  result.electrical_w = result.unit[0].electrical_w + result.unit[1].electrical_w;
  if (!std::isfinite(result.thermal_w) || !std::isfinite(result.electrical_w)) {
    result.thermal_w = NAN;
    result.electrical_w = NAN;
    result.status = Status::INVALID_INPUT;
    return result;
  }
  result.status = hz[0] == 0.0f && hz[1] == 0.0f ? Status::OFF : Status::VALID;
  return result;
}

template <typename Model>
inline PairPrediction predict_series(const Snapshot& snapshot, const std::array<float, 2>& hz, const Limits& limits,
                                     Model model) {
  return predict_series_using(
      snapshot, hz, limits, model,
      [](unsigned hp, float frequency, float outside, float inlet, float capacity, const Limits& bounds,
         auto evaluate) { return solve_unit(hp, frequency, outside, inlet, capacity, bounds, evaluate); });
}

struct Movement {
  bool valid = false;
  unsigned starts = 0;
  unsigned stops = 0;
  bool topology_change = false;
  bool owner_swap = false;
  float modulation_hz = NAN;
};

// Diagnostic decomposition only. Starts/stops are never hidden in a frequency
// distance and no unmeasured Wh, wear or topology penalties are invented.
inline Movement movement(const std::array<float, 2>& previous, const std::array<float, 2>& next) {
  Movement result;
  unsigned before_count = 0, after_count = 0;
  result.modulation_hz = 0.0f;
  for (unsigned hp = 0; hp < 2; ++hp) {
    if (!valid_frequency(previous[hp]) || !valid_frequency(next[hp])) return {};
    const bool before = previous[hp] > 0.0f, after = next[hp] > 0.0f;
    before_count += before;
    after_count += after;
    result.starts += !before && after;
    result.stops += before && !after;
    if (before && after) result.modulation_hz += std::fabs(previous[hp] - next[hp]);
  }
  result.topology_change = before_count != after_count;
  result.owner_swap = before_count == 1 && after_count == 1 && (previous[0] > 0.0f) != (next[0] > 0.0f);
  result.valid = true;
  return result;
}

}  // namespace oq_water_shadow
