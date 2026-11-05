#pragma once

#include "oq_ph_shadow_demand_types.h"
#include "../learning/oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE
#include "../learning/oq_ph_learning_fit.h"

namespace oq_power_house {

inline ShadowDemandResult evaluate_shadow_demand(const DemandEvaluationFrame& frame,
                                                 const learning::AdviceResult& batch, const learning::FitConfig& limits,
                                                 bool batch_ready, uint32_t context_revision, uint64_t now_ms,
                                                 ShadowDemandReason gate = ShadowDemandReason::READY) {
  ShadowDemandResult out;
  out.tick_ms = frame.monotonic_ms;
  out.context_revision = context_revision;
  out.reason = gate != ShadowDemandReason::READY ? gate : frame.reason;
  if (out.reason != ShadowDemandReason::READY) return out;
  // The existing demand cadence is the entire validity lease. Never extend a
  // frozen control frame merely because learning or HTTP continues to run.
  if (frame.monotonic_ms == 0 || frame.cadence_ms == 0 || now_ms < frame.monotonic_ms ||
      now_ms - frame.monotonic_ms >= frame.cadence_ms) {
    out.reason = ShadowDemandReason::STALE_FRAME;
    return out;
  }
  // external_valid records received-state presence (Sensor.has_state), which
  // can remain true for NAN. Match actual select_feedforward's finite-value gate.
  if (frame.input.external_valid && isfinite(frame.input.external_w)) {
    out.reason = ShadowDemandReason::EXTERNAL_OVERRIDE;
    return out;
  }
  if (!batch_ready || !batch.advice_ready || !batch.candidate_available ||
      batch.status != learning::LearningStatus::ADVICE_READY) {
    out.reason = ShadowDemandReason::BATCH_NOT_READY;
    return out;
  }
  if (context_revision == 0 || batch.context_revision != context_revision) {
    out.reason = ShadowDemandReason::CONTEXT_MISMATCH;
    return out;
  }
  if (!learning::valid_fit_config(limits) || !learning::detail::plausible_line(batch.candidate, limits)) {
    out.reason = ShadowDemandReason::INVALID_CANDIDATE;
    return out;
  }
  if (!isfinite(batch.validated_temp_min_c) || !isfinite(batch.validated_temp_max_c) ||
      !(batch.validated_temp_max_c > batch.validated_temp_min_c) || !isfinite(frame.input.outside_c) ||
      frame.input.outside_c < batch.validated_temp_min_c || frame.input.outside_c > batch.validated_temp_max_c) {
    out.reason = ShadowDemandReason::OUTSIDE_RANGE;
    return out;
  }
  const auto candidate =
      decide_demand_with_line(frame.input, frame.tuning, frame.previous, batch.candidate, frame.envelope);
  if (!candidate.valid || candidate.external || !isfinite(frame.manual_w) || frame.manual_w < 0.0f ||
      !isfinite(candidate.requested_w) || !isfinite(candidate.requested_w - frame.manual_w)) {
    out.reason = ShadowDemandReason::INVALID_INPUTS;
    return out;
  }
  out.expires_ms = frame.monotonic_ms + frame.cadence_ms;
  out.generation = frame.generation;
  out.manual_w = frame.manual_w;
  out.candidate_w = candidate.requested_w;
  out.delta_w = candidate.requested_w - frame.manual_w;
  return out;
}

}  // namespace oq_power_house
#endif
