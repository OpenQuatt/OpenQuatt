#pragma once

#include <math.h>
#include <stdint.h>

#include "oq_raw_receipt.h"

namespace oq_sources {

// Exact route selected by the existing control source resolver. Composite and
// synthetic routes remain observable, but cannot claim a physical receipt.
enum class LearningSourceRoute : uint8_t {
  NONE = 0,
  OPENTHERM_ROOM,
  OPENTHERM_SETPOINT,
  CIC_ROOM,
  CIC_SETPOINT,
  CIC_FLOW,
  HA_ROOM,
  HA_SETPOINT,
  HA_OUTSIDE,
  API_ROOM,
  API_SETPOINT,
  API_OUTSIDE,
  MQTT_ROOM,
  MQTT_SETPOINT,
  MQTT_OUTSIDE,
  HP1_OUTSIDE,
  HP2_OUTSIDE,
  OUTSIDE_AGGREGATE,
  CONTROLLER_FLOW,
  HP1_FLOW,
  HP2_FLOW,
  FLOW_AGGREGATE,
  SYNTHESIZED_ZERO_FLOW,
};

enum class LearningSourceProvenance : uint8_t {
  UNKNOWN = 0,
  SELECTED_VALUE,
  PHYSICAL_RECEIPT,
  HELD,
  UNSUPPORTED,
  SYNTHESIZED,
};

enum class LearningCompositeOperation : uint8_t { NONE = 0, ARITHMETIC_MEAN, MAXIMUM, MINIMUM };

struct SourceConfigurationKey {
  uint8_t selected = 0;
  uint8_t auxiliary_a = 0;
  uint8_t auxiliary_b = 0;
  uint32_t endpoint_generation = 0;
};

struct ResolvedLearningSource {
  // This is always the value selected by the control source resolver. Receipts
  // remain diagnostic metadata for routes that provide them.
  float value = NAN;
  LearningSourceRoute route = LearningSourceRoute::NONE;
  LearningSourceRoute component_route = LearningSourceRoute::NONE;
  RawFloatReceipt receipt;
  LearningSourceRoute secondary_route = LearningSourceRoute::NONE;
  RawFloatReceipt secondary_receipt;
  LearningCompositeOperation composite_operation = LearningCompositeOperation::NONE;
  SourceConfigurationKey configuration;
  uint32_t configuration_generation = 0;
  bool valid = false;
  LearningSourceProvenance provenance = LearningSourceProvenance::UNKNOWN;
};

static_assert(sizeof(ResolvedLearningSource) <= 96U, "Resolved source metadata must remain bounded");

inline bool same_configuration(const SourceConfigurationKey& lhs, const SourceConfigurationKey& rhs) {
  return lhs.selected == rhs.selected && lhs.auxiliary_a == rhs.auxiliary_a && lhs.auxiliary_b == rhs.auxiliary_b &&
         lhs.endpoint_generation == rhs.endpoint_generation;
}

inline uint32_t next_configuration_generation(uint32_t current) { return current == UINT32_MAX ? 0U : current + 1U; }

// Main-loop owner for an exact selector configuration. A->B->A therefore
// yields three distinct generations without using a collision-prone hash.
class SourceConfigurationGeneration {
 public:
  uint32_t observe(const SourceConfigurationKey& key) {
    if (!this->initialized_ || !same_configuration(this->key_, key)) {
      this->key_ = key;
      this->initialized_ = true;
      this->increment_();
    }
    return this->generation_;
  }

  uint32_t observe_resolution(const ResolvedLearningSource& source) {
    const ResolutionKey next{static_cast<uint8_t>(source.route), static_cast<uint8_t>(source.component_route),
                             static_cast<uint8_t>(source.secondary_route),
                             static_cast<uint8_t>(source.composite_operation), static_cast<uint8_t>(source.provenance)};
    if (!this->resolution_initialized_ || !same_resolution_(this->resolution_, next)) {
      this->resolution_ = next;
      this->resolution_initialized_ = true;
      this->increment_();
    }
    return this->generation_;
  }

  uint32_t current() const { return this->generation_; }
  SourceConfigurationKey current_key() const { return this->key_; }
  bool blocked() const { return this->blocked_; }

 private:
  struct ResolutionKey {
    uint8_t route = 0;
    uint8_t component_route = 0;
    uint8_t secondary_route = 0;
    uint8_t operation = 0;
    uint8_t provenance = 0;
  };

  static bool same_resolution_(const ResolutionKey& lhs, const ResolutionKey& rhs) {
    return lhs.route == rhs.route && lhs.component_route == rhs.component_route &&
           lhs.secondary_route == rhs.secondary_route && lhs.operation == rhs.operation &&
           lhs.provenance == rhs.provenance;
  }

  void increment_() {
    const uint32_t next = next_configuration_generation(this->generation_);
    if (next == 0U) {
      this->generation_ = 0U;
      this->blocked_ = true;
    } else if (!this->blocked_) {
      this->generation_ = next;
    }
  }

  SourceConfigurationKey key_{};
  ResolutionKey resolution_{};
  uint32_t generation_ = 0;
  bool initialized_ = false;
  bool resolution_initialized_ = false;
  bool blocked_ = false;
};

inline void observe_optional(RawFloatReceipt& receipt, bool present, float value, uint64_t received_ms) {
  receipt.observe(present ? value : NAN, received_ms, present);
}

inline bool usable_current_receipt(const RawFloatReceipt& receipt) {
  return receipt.received && receipt.valid && isfinite(receipt.value) && receipt.received_ms != 0U;
}

inline ResolvedLearningSource validate_current_receipts(const ResolvedLearningSource& cached,
                                                        const RawFloatReceipt& current,
                                                        const RawFloatReceipt& secondary = {}) {
  const bool direct = cached.provenance == LearningSourceProvenance::PHYSICAL_RECEIPT;
  const bool composite = cached.secondary_route != LearningSourceRoute::NONE;
  if ((!direct || usable_current_receipt(current)) &&
      (!composite || (usable_current_receipt(current) && usable_current_receipt(secondary))))
    return cached;
  auto invalid = cached;
  invalid.value = NAN;
  invalid.receipt = current;
  if (composite) invalid.secondary_receipt = secondary;
  invalid.valid = false;
  invalid.provenance = LearningSourceProvenance::UNKNOWN;
  return invalid;
}

inline ResolvedLearningSource physical_source(LearningSourceRoute route, const RawFloatReceipt& receipt,
                                              uint32_t configuration_generation, bool selected_valid = true,
                                              const SourceConfigurationKey& configuration = {}) {
  ResolvedLearningSource result;
  result.route = route;
  result.receipt = receipt;
  result.configuration = configuration;
  result.configuration_generation = configuration_generation;
  result.valid = selected_valid && receipt.received && receipt.valid && isfinite(receipt.value);
  result.provenance = result.valid ? LearningSourceProvenance::PHYSICAL_RECEIPT : LearningSourceProvenance::UNKNOWN;
  result.value = result.valid ? receipt.value : NAN;
  return result;
}

inline ResolvedLearningSource selected_source(
    float value, bool selected_valid, LearningSourceRoute route, uint32_t configuration_generation,
    LearningSourceProvenance provenance = LearningSourceProvenance::SELECTED_VALUE,
    const SourceConfigurationKey& configuration = {}) {
  ResolvedLearningSource result;
  result.value = selected_valid && isfinite(value) ? value : NAN;
  result.route = route;
  result.configuration = configuration;
  result.configuration_generation = configuration_generation;
  result.valid = selected_valid && isfinite(value);
  result.provenance = result.valid ? provenance : LearningSourceProvenance::UNKNOWN;
  return result;
}

inline ResolvedLearningSource unsupported_source(
    float selected_value, bool selected_valid, LearningSourceRoute route, uint32_t configuration_generation,
    LearningSourceProvenance provenance = LearningSourceProvenance::UNSUPPORTED,
    const SourceConfigurationKey& configuration = {}) {
  ResolvedLearningSource result;
  result.value = selected_valid && isfinite(selected_value) ? selected_value : NAN;
  result.route = route;
  result.configuration = configuration;
  result.configuration_generation = configuration_generation;
  result.valid = selected_valid && isfinite(selected_value);
  result.provenance = provenance;
  return result;
}

inline ResolvedLearningSource unsupported_composite_source(
    float selected_value, bool selected_valid, LearningSourceRoute route, LearningSourceRoute component_route,
    LearningSourceRoute secondary_route, const RawFloatReceipt& receipt, const RawFloatReceipt& secondary_receipt,
    LearningCompositeOperation operation, const SourceConfigurationKey& configuration,
    uint32_t configuration_generation) {
  ResolvedLearningSource result = unsupported_source(selected_value, selected_valid, route, configuration_generation,
                                                     LearningSourceProvenance::UNSUPPORTED, configuration);
  result.component_route = component_route;
  result.receipt = receipt;
  result.secondary_route = secondary_route;
  result.secondary_receipt = secondary_receipt;
  result.composite_operation = operation;
  return result;
}

}  // namespace oq_sources
