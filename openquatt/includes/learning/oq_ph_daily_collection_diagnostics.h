#pragma once

#include "oq_ph_learning_aggregate.h"
#include "oq_ph_learning_source_logic.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

namespace oq_power_house::learning {

constexpr size_t kDailyCollectionCauseCount = static_cast<size_t>(DailyCollectionCause::COUNT);
constexpr uint64_t kUnknownDailyDurationMs = UINT64_MAX;

inline const char* daily_collection_cause_name(DailyCollectionCause cause) {
  switch (cause) {
    case DailyCollectionCause::NONE:
      return "none";
    case DailyCollectionCause::INVALID_MEASUREMENT:
      return "invalid_measurement";
    case DailyCollectionCause::MISSED_TICK:
      return "missed_tick";
    case DailyCollectionCause::GAP_TOO_LONG:
      return "gap_too_long";
    case DailyCollectionCause::CLOCK_DISCONTINUITY:
      return "clock_discontinuity";
    case DailyCollectionCause::CONTEXT_CHANGED:
      return "context_changed";
    case DailyCollectionCause::UNCERTAINTY_BUDGET:
      return "uncertainty_budget";
    case DailyCollectionCause::COMPLETED_QUALITY:
      return "completed_quality";
    case DailyCollectionCause::RESTART_DISCARDED:
      return "restart_discarded";
    case DailyCollectionCause::APPEND_REJECTED:
      return "append_rejected";
    case DailyCollectionCause::PAUSED:
      return "paused";
    case DailyCollectionCause::DISABLED:
      return "disabled";
    case DailyCollectionCause::INVALID_INPUT:
      return "invalid_input";
    case DailyCollectionCause::INVALID_CONFIGURATION:
      return "invalid_configuration";
    case DailyCollectionCause::EVIDENCE_REPLACED:
      return "evidence_replaced";
    default:
      return "unknown";
  }
}

struct DailyCollectionLoss {
  DailyCollectionCause cause = DailyCollectionCause::NONE;
  LearningStatus status = LearningStatus::OK;
  SnapshotSourceStatus source_status = SnapshotSourceStatus::OK;
  uint32_t epoch_s = 0;
  uint32_t context_revision = 0;
  uint32_t invalid_reasons = INVALID_NONE;
  uint64_t collected_ms = kUnknownDailyDurationMs;
  // Whole interval since the last valid endpoint, not just invalid tick time.
  uint64_t observed_gap_ms = kUnknownDailyDurationMs;
  double charged_uncertainty_ws = NAN;
  // Unknown until a coherent valid endpoint closes the gap. These are split
  // at the day boundary exactly as in the collector, and are not measurements.
  double attempted_day_uncertainty_ws = NAN;
  double attempted_next_day_uncertainty_ws = NAN;
};

struct DailyRemainderRejection {
  uint32_t epoch_s = 0;
  uint64_t duration_ms = kUnknownDailyDurationMs;
  double uncertainty_ws = NAN;
  double completed_day_uncertainty_ws = NAN;
};

// Bounded, main-owner RAM diagnostics, reset with the runtime. Never included
// in persisted learning evidence or copied into PassiveRuntimeSummary.
struct DailyCollectionDiagnostics {
  uint32_t interrupted_periods = 0;
  uint32_t completed_quality_rejections = 0;
  uint32_t append_rejections = 0;
  uint32_t bridged_gaps = 0;
  uint32_t rejected_next_day_remainders = 0;
  uint64_t discarded_collected_ms = 0;
  uint32_t cause_counts[kDailyCollectionCauseCount]{};
  DailyCollectionLoss last_loss;
  SnapshotSourceStatus pending_source_status = SnapshotSourceStatus::OK;
  uint32_t pending_invalid_reasons = INVALID_NONE;
  double last_completed_uncertainty_ws = NAN;
  DailyRemainderRejection last_remainder_rejection;
};

static_assert(sizeof(DailyCollectionDiagnostics) <= 208U, "Daily diagnostics must remain a small bounded RAM object");

namespace daily_diagnostics_detail {

inline void increment(uint32_t& value) {
  if (value != UINT32_MAX) ++value;
}

inline uint64_t collected_ms(const SegmentAccumulator& state) {
  if (state.last_monotonic_ms < state.start_monotonic_ms || state.carried_duration_ms > kSegmentDurationMs ||
      state.last_monotonic_ms - state.start_monotonic_ms > kSegmentDurationMs - state.carried_duration_ms)
    return kUnknownDailyDurationMs;
  return segment_elapsed_ms(state);
}

inline void remember_loss(DailyCollectionDiagnostics& diagnostics, const DailyCollectionLoss& loss) {
  const size_t index = static_cast<size_t>(loss.cause);
  if (index == 0 || index >= kDailyCollectionCauseCount) return;
  increment(diagnostics.cause_counts[index]);
  if (loss.cause == DailyCollectionCause::COMPLETED_QUALITY)
    increment(diagnostics.completed_quality_rejections);
  else if (loss.cause == DailyCollectionCause::APPEND_REJECTED)
    increment(diagnostics.append_rejections);
  else
    increment(diagnostics.interrupted_periods);
  if (loss.collected_ms != kUnknownDailyDurationMs) {
    const uint64_t remaining = UINT64_MAX - diagnostics.discarded_collected_ms;
    diagnostics.discarded_collected_ms += loss.collected_ms > remaining ? remaining : loss.collected_ms;
  }
  diagnostics.last_loss = loss;
}

inline DailyCollectionLoss capture(const SegmentAccumulator& state, const DailyCollectionDiagnostics& diagnostics,
                                   uint32_t epoch_s) {
  DailyCollectionLoss loss;
  loss.epoch_s = epoch_s;
  loss.context_revision = state.context_revision;
  loss.collected_ms = collected_ms(state);
  loss.charged_uncertainty_ws = state.missing_energy_uncertainty_ws;
  if (state.source_gap_pending) {
    loss.source_status = diagnostics.pending_source_status;
    loss.invalid_reasons = diagnostics.pending_invalid_reasons;
    if (state.last_gap_observation_ms >= state.last_monotonic_ms)
      loss.observed_gap_ms = state.last_gap_observation_ms - state.last_monotonic_ms;
  }
  return loss;
}

inline void clear_pending(DailyCollectionDiagnostics& diagnostics) {
  diagnostics.pending_source_status = SnapshotSourceStatus::OK;
  diagnostics.pending_invalid_reasons = INVALID_NONE;
}

}  // namespace daily_diagnostics_detail

// Call before discarding an active accumulator. Repeated waiting/invalid ticks
// after the discard do not count as additional interrupted periods.
inline void diagnose_daily_discard(DailyCollectionDiagnostics& diagnostics, const SegmentAccumulator& state,
                                   DailyCollectionCause cause, LearningStatus status, uint32_t epoch_s = 0) {
  if (state.active) {
    auto loss = daily_diagnostics_detail::capture(state, diagnostics, epoch_s);
    loss.cause = cause;
    loss.status = status;
    daily_diagnostics_detail::remember_loss(diagnostics, loss);
  }
  daily_diagnostics_detail::clear_pending(diagnostics);
}

// The collector remains the sole authority for acceptance. Capture only small
// diagnostic scalars before it can reset/seed, never copy the accumulator.
inline ObserveResult observe_daily_with_diagnostics(DailyCollectionDiagnostics& diagnostics, SegmentAccumulator& state,
                                                    const LearningSnapshot& snapshot, const QualityConfig& config,
                                                    bool may_bridge, SnapshotSourceStatus source_status) {
  const bool was_active = state.active;
  const bool was_gap = state.source_gap_pending || state.restart_pending;
  const bool checkpoint_valid = !state.restart_pending || valid_daily_checkpoint(state, config);
  uint64_t next_day_remainder_ms = kUnknownDailyDurationMs;
  auto loss = daily_diagnostics_detail::capture(state, diagnostics, snapshot.epoch_s);
  if (was_active && !state.restart_pending && snapshot.monotonic_ms > state.last_monotonic_ms)
    loss.observed_gap_ms = snapshot.monotonic_ms - state.last_monotonic_ms;
  if (!checkpoint_valid) {
    // An invalid checkpoint cannot establish either previous progress or a
    // conditional heat bound, even if some of its fields look plausible.
    loss.collected_ms = kUnknownDailyDurationMs;
    loss.observed_gap_ms = kUnknownDailyDurationMs;
    loss.charged_uncertainty_ws = NAN;
  }
  if (was_gap && checkpoint_valid) {
    if (state.restart_pending) {
      loss.observed_gap_ms = snapshot.epoch_s > state.last_epoch_s
                                 ? static_cast<uint64_t>(snapshot.epoch_s - state.last_epoch_s) * 1000ULL
                                 : kUnknownDailyDurationMs;
    } else if (snapshot.monotonic_ms > state.last_monotonic_ms) {
      loss.observed_gap_ms = snapshot.monotonic_ms - state.last_monotonic_ms;
    }
    if (loss.observed_gap_ms != kUnknownDailyDurationMs && loss.collected_ms != kUnknownDailyDurationMs &&
        validate_snapshot(snapshot, config) == LearningStatus::OK) {
      const double bound_w = config.max_abs_heat_w + fmax(fabs(state.last_heat_w), fabs(snapshot.heat_to_water_w));
      const uint64_t remaining_ms = kSegmentDurationMs - loss.collected_ms;
      const uint64_t day_gap_ms = loss.observed_gap_ms < remaining_ms ? loss.observed_gap_ms : remaining_ms;
      loss.attempted_day_uncertainty_ws = bound_w * day_gap_ms / 1000.0;
      next_day_remainder_ms = loss.observed_gap_ms - day_gap_ms;
      loss.attempted_next_day_uncertainty_ws = bound_w * next_day_remainder_ms / 1000.0;
    }
  }
  const auto result = observe_snapshot(state, snapshot, config, may_bridge);
  const bool completed_day = result.has_record || result.cause == DailyCollectionCause::COMPLETED_QUALITY;
  // Observe the collector's final decision without adding a control flag to
  // ObserveResult. A newer collector can retain the old day while rejecting
  // only its over-budget remainder; an older collector rejects the whole gap.
  const bool discarded_next_day_remainder =
      completed_day && was_gap && next_day_remainder_ms != kUnknownDailyDurationMs && next_day_remainder_ms > 0 &&
      isfinite(loss.attempted_next_day_uncertainty_ws) &&
      loss.attempted_next_day_uncertainty_ws > kDailyMissingHeatBudgetW * 86400.0 && state.active &&
      !state.source_gap_pending && !state.restart_pending && state.start_epoch_s == snapshot.epoch_s &&
      state.last_epoch_s == snapshot.epoch_s && state.start_monotonic_ms == snapshot.monotonic_ms &&
      state.last_monotonic_ms == snapshot.monotonic_ms && segment_elapsed_ms(state) == 0 &&
      state.missing_energy_uncertainty_ws == 0.0;
  if (completed_day) {
    diagnostics.last_completed_uncertainty_ws = loss.charged_uncertainty_ws;
    if (was_gap) diagnostics.last_completed_uncertainty_ws += loss.attempted_day_uncertainty_ws;
  }
  if (was_active && result.cause != DailyCollectionCause::NONE) {
    loss.cause = result.cause;
    loss.status = result.status;
    loss.invalid_reasons |= snapshot.invalid_reasons;
    if (source_status != SnapshotSourceStatus::OK) loss.source_status = source_status;
    if (result.cause == DailyCollectionCause::COMPLETED_QUALITY) {
      loss.collected_ms = kSegmentDurationMs;
      loss.charged_uncertainty_ws = diagnostics.last_completed_uncertainty_ws;
    } else if (result.cause != DailyCollectionCause::UNCERTAINTY_BUDGET) {
      // An endpoint rejected for time, context or operation cannot establish
      // the collector's conditional missing-energy uncertainty.
      loss.attempted_day_uncertainty_ws = loss.attempted_next_day_uncertainty_ws = NAN;
    }
    if (result.cause == DailyCollectionCause::CLOCK_DISCONTINUITY) loss.observed_gap_ms = kUnknownDailyDurationMs;
    daily_diagnostics_detail::remember_loss(diagnostics, loss);
  }
  if (discarded_next_day_remainder) {
    daily_diagnostics_detail::increment(diagnostics.rejected_next_day_remainders);
    diagnostics.last_remainder_rejection = {snapshot.epoch_s, next_day_remainder_ms,
                                            loss.attempted_next_day_uncertainty_ws,
                                            diagnostics.last_completed_uncertainty_ws};
  }
  if (was_gap && !state.source_gap_pending && !discarded_next_day_remainder &&
      (result.status == LearningStatus::COLLECTING || result.has_record ||
       result.cause == DailyCollectionCause::COMPLETED_QUALITY)) {
    daily_diagnostics_detail::increment(diagnostics.bridged_gaps);
  }
  if (state.source_gap_pending) {
    if (!was_gap || diagnostics.pending_source_status == SnapshotSourceStatus::OK)
      diagnostics.pending_source_status = source_status;
    diagnostics.pending_invalid_reasons |= snapshot.invalid_reasons;
  } else {
    daily_diagnostics_detail::clear_pending(diagnostics);
  }
  return result;
}

inline void diagnose_daily_append_rejection(DailyCollectionDiagnostics& diagnostics, const SegmentRecord& record,
                                            LearningStatus status) {
  DailyCollectionLoss loss;
  loss.cause = DailyCollectionCause::APPEND_REJECTED;
  loss.status = status;
  loss.epoch_s = record.end_epoch_s;
  loss.context_revision = record.context_revision;
  loss.collected_ms = static_cast<uint64_t>(record.duration_s) * 1000ULL;
  loss.charged_uncertainty_ws = diagnostics.last_completed_uncertainty_ws;
  daily_diagnostics_detail::remember_loss(diagnostics, loss);
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
