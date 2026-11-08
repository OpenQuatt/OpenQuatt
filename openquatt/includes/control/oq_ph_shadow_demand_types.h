#pragma once

#include <stdint.h>
#include <stddef.h>
#include <type_traits>

#include "oq_power_house_demand_logic.h"

namespace oq_power_house {

enum class ShadowDemandReason : uint8_t {
  READY,
  INACTIVE,
  STARTUP,
  WARMUP_ACTIVE,
  EXTERNAL_OVERRIDE,
  STALE_FRAME,
  SOURCE_CHANGED,
  SETTINGS_CHANGED,
  SERVICE_OR_OTA,
  RESET_PENDING,
  INVALID_INPUTS,
  BATCH_NOT_READY,
  CONTEXT_MISMATCH,
  OUTSIDE_RANGE,
  INVALID_CANDIDATE,
};

inline const char* shadow_demand_reason_name(ShadowDemandReason reason) {
  switch (reason) {
    case ShadowDemandReason::READY:
      return "ready";
    case ShadowDemandReason::INACTIVE:
      return "inactive";
    case ShadowDemandReason::STARTUP:
      return "startup";
    case ShadowDemandReason::WARMUP_ACTIVE:
      return "warmup_active";
    case ShadowDemandReason::EXTERNAL_OVERRIDE:
      return "external_override";
    case ShadowDemandReason::STALE_FRAME:
      return "stale_frame";
    case ShadowDemandReason::SOURCE_CHANGED:
      return "source_changed";
    case ShadowDemandReason::SETTINGS_CHANGED:
      return "settings_changed";
    case ShadowDemandReason::SERVICE_OR_OTA:
      return "service_or_ota";
    case ShadowDemandReason::RESET_PENDING:
      return "reset_pending";
    case ShadowDemandReason::INVALID_INPUTS:
      return "invalid_inputs";
    case ShadowDemandReason::BATCH_NOT_READY:
      return "batch_not_ready";
    case ShadowDemandReason::CONTEXT_MISMATCH:
      return "context_mismatch";
    case ShadowDemandReason::OUTSIDE_RANGE:
      return "outside_range";
    case ShadowDemandReason::INVALID_CANDIDATE:
      return "invalid_candidate";
  }
  return "invalid_inputs";
}

// Main-loop-owned, bounded copy of one actual, unboosted control calculation.
// No control owner accepts this object back. In particular next DemandState is
// deliberately absent: the counterfactual cannot become a second control loop.
struct DemandEvaluationFrame {
  uint64_t monotonic_ms = 0;
  uint32_t cadence_ms = 0;
  uint32_t generation = 0;
  uint32_t source_generation[3]{};
  DemandInput input;
  DemandTuning tuning;
  DemandState previous;
  float manual_w = NAN;
  ShadowDemandReason reason = ShadowDemandReason::INACTIVE;
};
static_assert(std::is_trivially_copyable_v<DemandEvaluationFrame>);
static_assert(sizeof(DemandEvaluationFrame) <= 144, "control diagnostic DRAM copy must remain small");

struct ShadowDemandResult {
  uint64_t tick_ms = 0;
  uint64_t expires_ms = 0;
  uint32_t context_revision = 0;
  uint32_t generation = 0;
  float manual_w = NAN, candidate_w = NAN, delta_w = NAN;
  ShadowDemandReason reason = ShadowDemandReason::INACTIVE;
  bool available() const { return reason == ShadowDemandReason::READY; }
};
static_assert(std::is_trivially_copyable_v<ShadowDemandResult>);
static_assert(sizeof(ShadowDemandResult) <= 40, "published shadow must remain a bounded value copy");

// Fixed literals, two unsigned integers, and three finite %.6g numbers.
constexpr size_t kMaxShadowDemandJsonBytes = 512;
template <typename Writer>
void write_shadow_demand_json(Writer& json, const ShadowDemandResult& result) {
  const bool available = result.available();
  json.add(
      "{\"schema\":1,\"semantics\":\"one_step_from_observed_state\",\"scope\":\"base_before_start_stop_dispatch\","
      "\"source\":\"actual\",\"state_origin\":\"live_pre_tick\",\"run_id\":null,"
      "\"context_revision\":%u,\"tick_ms\":%llu,\"available\":%s,\"reason\":\"%s\",\"manual_w\":",
      static_cast<unsigned>(result.context_revision), static_cast<unsigned long long>(result.tick_ms),
      available ? "true" : "false", shadow_demand_reason_name(result.reason));
  json.number(available ? result.manual_w : NAN);
  json.add(",\"candidate_w\":");
  json.number(available ? result.candidate_w : NAN);
  json.add(",\"delta_w\":");
  json.number(available ? result.delta_w : NAN);
  json.add(",\"auto_apply_allowed\":false}");
}

}  // namespace oq_power_house
