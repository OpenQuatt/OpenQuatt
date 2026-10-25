#pragma once

#include "oq_resolved_learning_source.h"

namespace oq_sources {

struct ResolvedControlInput {
  float value = NAN;
  bool valid = false;
  bool fresh = false;
  bool held = false;
  uint32_t configuration_generation = 0;
};

struct RoomControlSnapshot {
  ResolvedControlInput room;
  ResolvedControlInput setpoint;
};

// CIC suppresses producer publications within 0.001 C. A valid new
// receipt may precede the selected sensor publication by one sensor tick.
inline bool current_receipt_matches(const RawFloatReceipt& receipt, float producer_c) {
  return usable_current_receipt(receipt) && isfinite(producer_c) && fabsf(receipt.value - producer_c) <= 0.001f;
}

inline ResolvedControlInput control_input(const ResolvedLearningSource& selected, float published_value,
                                          bool producer_current) {
  const bool held = selected.provenance == LearningSourceProvenance::HELD;
  const bool valid = selected.valid && isfinite(selected.value) && selected.value == published_value;
  return {selected.value, valid, valid && producer_current && !held, held, selected.configuration_generation};
}

}  // namespace oq_sources
