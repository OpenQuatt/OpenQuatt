#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <math.h>
#include <new>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "oq_ph_learning_aggregate.h"
#include "oq_ph_learning_fit.h"
#include "oq_ph_model_validation.h"
#include "oq_ph_thermal_aggregate.h"
#include "oq_ph_thermal_model_logic.h"

namespace oq_power_house::learning {

struct PassiveRuntimeConfig {
  QualityConfig quality;
  FitConfig fit;
  ThermalWindowConfig thermal_window;
  ThermalModelConfig thermal_model;
  ModelValidationConfig validation;
};

// The physical calorimetry proof is intentionally ALWAYS_OFF after reboot.
// Its first positive confirmation only rehydrates that proof; every later
// confirmation transition is a real context change and invalidates history.
struct CalorimetryReconfirmationState {
  bool initial_positive_confirmation_available = true;
};

inline bool calorimetry_confirmation_invalidates(CalorimetryReconfirmationState& state, bool confirmed) {
  if (confirmed && state.initial_positive_confirmation_available) {
    state.initial_positive_confirmation_available = false;
    return false;
  }
  state.initial_positive_confirmation_available = false;
  return true;
}

enum class PassiveRuntimeStatus : uint8_t {
  COLLECTING = 0,
  FIT_IN_PROGRESS,
  ADVICE_READY,
  PAUSED,
  CONTEXT_CHANGED,
  INVALID_CONFIGURATION,
  INVALID_INPUT,
  STALE_CONTEXT,
  TIME_DISCONTINUITY,
  BLOCKED,
};

struct PassiveRuntimeDiagnostics {
  uint32_t tick_count = 0;
  uint32_t accepted_batch_records = 0;
  uint32_t rejected_batch_observations = 0;
  uint32_t accepted_thermal_intervals = 0;
  uint32_t rejected_thermal_observations = 0;
  uint32_t evidence_reset_count = 0;
  uint32_t fit_restart_count = 0;
  LearningStatus last_batch_status = LearningStatus::COLLECTING;
  ThermalWindowStatus last_thermal_window_status = ThermalWindowStatus::COLLECTING;
  ThermalUpdateStatus last_thermal_update_status = ThermalUpdateStatus::INVALID_CONFIGURATION;
};

struct PassiveRuntimeSummary {
  PassiveRuntimeStatus status = PassiveRuntimeStatus::INVALID_CONFIGURATION;
  bool initialized = false;
  bool blocked = false;
  bool opted_in = false;
  bool restored_records = false;
  bool current_observation_valid = false;
  bool fit_running = false;
  bool batch_advice_ready = false;
  bool thermal_model_ready = false;
  bool cross_validated_advice_ready = false;
  // This passive owner has no actuator API. Keep the field explicit for UI and
  // firmware assertions; it can never authorize applying a model.
  bool auto_apply_allowed = false;
  size_t record_count = 0;
  uint32_t source_generation = 0;
  uint32_t physical_context_generation = 0;
  uint32_t control_generation = 0;
  uint32_t last_epoch_s = 0;
  uint64_t last_monotonic_ms = 0;
  AdviceResult batch;
  ThermalModelEstimate thermal;
  ModelValidationResult validation;
  PassiveRuntimeDiagnostics diagnostics;
};

struct PassiveTickInput {
  PassiveContextView context;
  uint64_t now_monotonic_ms = 0;
  uint32_t now_epoch_s = 0;
  bool opted_in = false;
  bool context_valid = false;
  bool active_line_valid = false;
  HouseLine active_line;
  bool reference_context_valid = false;
  float reference_room_c = NAN;
  float reference_setpoint_c = NAN;
  bool batch_snapshot_available = false;
  LearningSnapshot batch_snapshot;
  bool dynamic_snapshot_available = false;
  LearningSnapshot dynamic_snapshot;
};

// Allocate this entire object in caller-controlled PSRAM. The pure runtime does
// not allocate, retain callbacks or perform I/O. During a fit, records[] is
// immutable; a newly completed record first cancels the fit and then appends.
struct PassiveRuntimeStorage {
  bool initialized = false;
  bool blocked = false;
  bool opted_in = false;
  bool restored_records = false;
  bool current_observation_valid = false;
  uint32_t source_generation = 0;
  uint32_t physical_context_generation = 0;
  uint32_t control_generation = 0;
  uint32_t last_epoch_s = 0;
  uint64_t last_monotonic_ms = 0;
  uint8_t context_bytes[kMaxPassiveContextBytes]{};
  size_t context_size = 0;
  PassiveRuntimeConfig config;
  SegmentRecord records[kMaxSegmentRecords]{};
  size_t record_count = 0;
  SegmentAccumulator batch_accumulator;
  ThermalWindowAccumulator thermal_accumulator;
  AdviceFitWorkspace fit_workspace;
  bool fit_running = false;
  bool fit_pending = false;
  bool fit_inputs_bound = false;
  HouseLine fit_active_line;
  float fit_reference_room_c = NAN;
  float fit_reference_setpoint_c = NAN;
  AdviceResult batch_result;
  ThermalModelState thermal_state;
  PassiveRuntimeDiagnostics diagnostics;
  PassiveRuntimeStatus status = PassiveRuntimeStatus::INVALID_CONFIGURATION;
};

static_assert(sizeof(PassiveRuntimeStorage) < 32U * 1024U,
              "Caller-owned passive learner state and fit workspace must stay within the PSRAM budget");

namespace passive_runtime_detail {

template <typename T>
inline void reset_in_place(T& value) {
  value.~T();
  new (&value) T();
}

inline bool valid_context(const PassiveContextView& context) {
  return context.bytes != nullptr && context.size > 0 && context.size <= kMaxPassiveContextBytes &&
         context.source_generation != 0 && context.physical_context_generation != 0 && context.control_generation != 0;
}

inline bool same_context_bytes(const PassiveRuntimeStorage& state, const PassiveContextView& context) {
  return state.context_size == context.size && memcmp(state.context_bytes, context.bytes, context.size) == 0;
}

inline bool valid_runtime_config(const PassiveRuntimeConfig& config) {
  FitConfig fit = config.fit;
  fit.reference_room_c = 0.5f * (config.quality.room_min_c + config.quality.room_max_c);
  fit.reference_setpoint_c = 0.5f * (config.quality.setpoint_min_c + config.quality.setpoint_max_c);
  return valid_quality_config(config.quality) && valid_fit_config(fit) &&
         thermal_window_detail::valid_config(config.thermal_window, config.quality) &&
         valid_thermal_model_config(config.thermal_model) &&
         isfinite(config.validation.max_heat_loss_difference_fraction) &&
         config.validation.max_heat_loss_difference_fraction >= 0.0 &&
         config.validation.max_heat_loss_difference_fraction <= 0.50 &&
         isfinite(config.validation.min_shared_outside_span_c) && config.validation.min_shared_outside_span_c > 0.0 &&
         config.validation.min_shared_outside_span_c <= 40.0 && isfinite(config.validation.max_storage_power_w) &&
         config.validation.max_storage_power_w >= 0.0 && config.validation.max_storage_power_w <= 5000.0 &&
         isfinite(config.validation.max_storage_fraction) && config.validation.max_storage_fraction >= 0.0 &&
         config.validation.max_storage_fraction <= 1.0;
}

inline void cancel_fit(PassiveRuntimeStorage& state) {
  reset_in_place(state.fit_workspace);
  state.fit_running = false;
  state.fit_pending = false;
  state.fit_inputs_bound = false;
  state.fit_active_line = {};
  state.fit_reference_room_c = NAN;
  state.fit_reference_setpoint_c = NAN;
  state.batch_result = {};
}

inline void clear_transient_collection(PassiveRuntimeStorage& state) {
  reset_segment(state.batch_accumulator);
  state.thermal_accumulator = {};
  cancel_fit(state);
  state.fit_pending = state.record_count > 0;
  state.current_observation_valid = false;
}

inline void clear_evidence(PassiveRuntimeStorage& state) {
  state.record_count = 0;
  state.restored_records = false;
  clear_transient_collection(state);
  initialize_thermal_model(state.thermal_state, state.config.thermal_model);
  if (state.diagnostics.evidence_reset_count != UINT32_MAX) ++state.diagnostics.evidence_reset_count;
}

inline void bind_context(PassiveRuntimeStorage& state, const PassiveContextView& context) {
  state.context_size = context.size;
  memcpy(state.context_bytes, context.bytes, context.size);
  state.source_generation = context.source_generation;
  state.physical_context_generation = context.physical_context_generation;
  state.control_generation = context.control_generation;
}

inline bool snapshot_matches_tick(const LearningSnapshot& snapshot, const PassiveTickInput& input) {
  return snapshot.monotonic_ms == input.now_monotonic_ms && snapshot.epoch_s == input.now_epoch_s &&
         snapshot.source_generation == input.context.source_generation &&
         snapshot.physical_context_generation == input.context.physical_context_generation &&
         snapshot.control_generation == input.context.control_generation;
}

inline void invalidate_dynamic(PassiveRuntimeStorage& state, uint64_t now_monotonic_ms) {
  state.thermal_accumulator = {};
  const ThermalUpdateResult update =
      invalidate_thermal_observation(state.thermal_state, now_monotonic_ms, state.config.thermal_model);
  state.diagnostics.last_thermal_update_status = update.status;
  if (state.diagnostics.rejected_thermal_observations != UINT32_MAX) ++state.diagnostics.rejected_thermal_observations;
}

inline void start_fit(PassiveRuntimeStorage& state, const PassiveTickInput& input) {
  cancel_fit(state);
  FitConfig fit = state.config.fit;
  fit.reference_room_c = input.reference_room_c;
  fit.reference_setpoint_c = input.reference_setpoint_c;
  const LearningStatus fit_status = begin_advice_fit(state.records, state.record_count, input.now_epoch_s,
                                                     input.active_line, state.config.quality, fit, state.fit_workspace);
  state.fit_running = fit_status == LearningStatus::FIT_IN_PROGRESS;
  state.fit_pending = false;
  state.fit_inputs_bound = true;
  state.fit_active_line = input.active_line;
  state.fit_reference_room_c = input.reference_room_c;
  state.fit_reference_setpoint_c = input.reference_setpoint_c;
  state.batch_result = state.fit_workspace.result;
  if (state.fit_running && state.diagnostics.fit_restart_count != UINT32_MAX) ++state.diagnostics.fit_restart_count;
}

}  // namespace passive_runtime_detail

inline PassiveRuntimeStatus initialize_passive_runtime(PassiveRuntimeStorage& state, const PassiveContextView& context,
                                                       const PassiveRuntimeConfig& config, bool opted_in) {
  passive_runtime_detail::reset_in_place(state);
  if (!passive_runtime_detail::valid_context(context) || !passive_runtime_detail::valid_runtime_config(config)) {
    state.status = PassiveRuntimeStatus::INVALID_CONFIGURATION;
    return state.status;
  }
  state.config = config;
  if (!initialize_thermal_model(state.thermal_state, config.thermal_model)) {
    state.status = PassiveRuntimeStatus::INVALID_CONFIGURATION;
    return state.status;
  }
  passive_runtime_detail::bind_context(state, context);
  state.initialized = true;
  state.opted_in = opted_in;
  state.status = opted_in ? PassiveRuntimeStatus::COLLECTING : PassiveRuntimeStatus::PAUSED;
  return state.status;
}

// These are the only entry points, besides tick(), that change learner state.
inline void pause_passive_runtime(PassiveRuntimeStorage& state, uint64_t now_ms) {
  passive_runtime_detail::clear_transient_collection(state);
  passive_runtime_detail::invalidate_dynamic(state, now_ms);
  state.opted_in = false;
  state.status = PassiveRuntimeStatus::PAUSED;
}

inline void reset_passive_runtime(PassiveRuntimeStorage& state) {
  passive_runtime_detail::reset_in_place(state);
  state.status = PassiveRuntimeStatus::PAUSED;
}

inline LearningDatasetView passive_runtime_dataset(const PassiveRuntimeStorage& state) {
  return {state.records,
          state.record_count,
          {state.context_bytes, state.context_size, state.source_generation, state.physical_context_generation,
           state.control_generation}};
}

// A validated immutable record view is read before its backing slot is reused.
// Restoring a batch never restores fit readiness or boot-local RLS evidence.
template <typename RecordView>
inline bool restore_passive_records(PassiveRuntimeStorage& state, const RecordView& records) {
  if (!state.initialized || state.blocked || records.record_count > kMaxSegmentRecords) return false;
  passive_runtime_detail::clear_evidence(state);
  for (size_t index = 0; index < records.record_count; ++index) {
    SegmentRecord record = records[index];
    record.source_generation = state.source_generation;
    record.physical_context_generation = state.physical_context_generation;
    record.control_generation = state.control_generation;
    state.records[index] = record;
  }
  state.record_count = records.record_count;
  state.restored_records = state.record_count > 0;
  state.fit_pending = state.record_count > 0;
  state.status = state.opted_in ? PassiveRuntimeStatus::COLLECTING : PassiveRuntimeStatus::PAUSED;
  return true;
}

inline void advance_passive_fit(PassiveRuntimeStorage& state) {
  if (!state.fit_running) return;
  const LearningStatus status = advance_advice_fit(state.fit_workspace);
  state.batch_result = state.fit_workspace.result;
  state.fit_running = status == LearningStatus::FIT_IN_PROGRESS;
}

// Offline analysis uses this same fit entry point, without inventing another
// measurement just to finish work or evaluate retained records at a later date.
inline void request_passive_fit(PassiveRuntimeStorage& state, const PassiveTickInput& input) {
  passive_runtime_detail::start_fit(state, input);
}

inline PassiveRuntimeStatus tick_passive_runtime(PassiveRuntimeStorage& state, const PassiveTickInput& input) {
  using namespace passive_runtime_detail;
  if (!state.initialized || !valid_runtime_config(state.config)) {
    state.status = PassiveRuntimeStatus::INVALID_CONFIGURATION;
    return state.status;
  }
  if (state.blocked) {
    state.status = PassiveRuntimeStatus::BLOCKED;
    return state.status;
  }
  if (state.diagnostics.tick_count != UINT32_MAX) ++state.diagnostics.tick_count;
  if (!valid_context(input.context)) {
    clear_evidence(state);
    state.blocked = true;
    state.status = PassiveRuntimeStatus::INVALID_INPUT;
    return state.status;
  }
  // UTC is commonly unavailable during boot. The firmware may defer calling
  // this function, but an early call still pauses safely instead of permanently
  // blocking the owner or assigning synthetic wall-clock time to records.
  if (input.now_monotonic_ms == 0 || input.now_epoch_s == 0) {
    clear_transient_collection(state);
    state.status = PassiveRuntimeStatus::PAUSED;
    return state.status;
  }
  const bool generation_decreased = input.context.source_generation < state.source_generation ||
                                    input.context.physical_context_generation < state.physical_context_generation ||
                                    input.context.control_generation < state.control_generation;
  if (generation_decreased) {
    clear_evidence(state);
    state.blocked = true;
    state.status = PassiveRuntimeStatus::STALE_CONTEXT;
    return state.status;
  }
  const bool generations_changed = input.context.source_generation != state.source_generation ||
                                   input.context.physical_context_generation != state.physical_context_generation ||
                                   input.context.control_generation != state.control_generation;
  if (!generations_changed && !same_context_bytes(state, input.context)) {
    clear_evidence(state);
    state.blocked = true;
    state.status = PassiveRuntimeStatus::STALE_CONTEXT;
    return state.status;
  }
  if (generations_changed) {
    clear_evidence(state);
    bind_context(state, input.context);
    state.last_epoch_s = input.now_epoch_s;
    state.last_monotonic_ms = input.now_monotonic_ms;
    state.opted_in = input.opted_in;
    state.status = PassiveRuntimeStatus::CONTEXT_CHANGED;
    return state.status;
  }
  if ((state.last_monotonic_ms != 0 && input.now_monotonic_ms <= state.last_monotonic_ms) ||
      (state.last_epoch_s != 0 && input.now_epoch_s < state.last_epoch_s)) {
    clear_evidence(state);
    state.blocked = true;
    state.status = PassiveRuntimeStatus::TIME_DISCONTINUITY;
    return state.status;
  }
  state.last_monotonic_ms = input.now_monotonic_ms;
  state.last_epoch_s = input.now_epoch_s;
  state.opted_in = input.opted_in;

  RecordBuffer buffer{state.records, state.record_count, kMaxSegmentRecords};
  const bool oldest_record_expired = state.record_count > 0 && state.records[0].end_epoch_s <= input.now_epoch_s &&
                                     input.now_epoch_s - state.records[0].end_epoch_s > kMaxRecordAgeS;
  if (oldest_record_expired) cancel_fit(state);
  const LearningStatus prune_status = prune_expired_records(buffer, input.now_epoch_s);
  state.record_count = buffer.count;
  if (prune_status != LearningStatus::OK) {
    clear_evidence(state);
    state.blocked = true;
    state.status = PassiveRuntimeStatus::TIME_DISCONTINUITY;
    return state.status;
  }
  if (oldest_record_expired) state.fit_pending = state.record_count > 0;
  if (!input.opted_in) {
    clear_transient_collection(state);
    invalidate_dynamic(state, input.now_monotonic_ms);
    state.status = PassiveRuntimeStatus::PAUSED;
    return state.status;
  }
  if (!input.context_valid || !input.active_line_valid || !valid_house_line(input.active_line) ||
      !input.reference_context_valid || !isfinite(input.reference_room_c) || !isfinite(input.reference_setpoint_c)) {
    clear_transient_collection(state);
    invalidate_dynamic(state, input.now_monotonic_ms);
    state.status = PassiveRuntimeStatus::PAUSED;
    return state.status;
  }
  if (state.fit_inputs_bound && (state.fit_active_line.heat_loss_w_per_k != input.active_line.heat_loss_w_per_k ||
                                 state.fit_active_line.zero_power_temp_c != input.active_line.zero_power_temp_c ||
                                 state.fit_reference_room_c != input.reference_room_c ||
                                 state.fit_reference_setpoint_c != input.reference_setpoint_c)) {
    cancel_fit(state);
    state.fit_pending = state.record_count > 0;
  }

  bool appended_record = false;
  const bool batch_current_valid = input.batch_snapshot_available &&
                                   snapshot_matches_tick(input.batch_snapshot, input) &&
                                   validate_snapshot(input.batch_snapshot, state.config.quality) == LearningStatus::OK;
  if (!input.batch_snapshot_available || !snapshot_matches_tick(input.batch_snapshot, input)) {
    reset_segment(state.batch_accumulator);
    state.diagnostics.last_batch_status = LearningStatus::INVALID_MEASUREMENT;
    if (state.diagnostics.rejected_batch_observations != UINT32_MAX) ++state.diagnostics.rejected_batch_observations;
  } else {
    const ObserveResult observed =
        observe_snapshot(state.batch_accumulator, input.batch_snapshot, state.config.quality);
    state.diagnostics.last_batch_status = observed.status;
    if (observed.has_record) {
      cancel_fit(state);
      buffer = {state.records, state.record_count, kMaxSegmentRecords};
      const LearningStatus append_status =
          append_record(buffer, observed.record, input.now_epoch_s, state.config.quality);
      state.record_count = buffer.count;
      state.diagnostics.last_batch_status = append_status;
      if (append_status == LearningStatus::OK) {
        appended_record = true;
        state.fit_pending = true;
        state.restored_records = false;
        if (state.diagnostics.accepted_batch_records != UINT32_MAX) ++state.diagnostics.accepted_batch_records;
      } else if (state.diagnostics.rejected_batch_observations != UINT32_MAX) {
        ++state.diagnostics.rejected_batch_observations;
      }
    } else if (observed.status != LearningStatus::COLLECTING &&
               state.diagnostics.rejected_batch_observations != UINT32_MAX) {
      ++state.diagnostics.rejected_batch_observations;
    }
  }

  const bool dynamic_current_valid =
      input.dynamic_snapshot_available && snapshot_matches_tick(input.dynamic_snapshot, input) &&
      validate_snapshot(input.dynamic_snapshot, state.config.quality) == LearningStatus::OK;
  state.current_observation_valid = batch_current_valid && dynamic_current_valid;
  if (!input.dynamic_snapshot_available || !snapshot_matches_tick(input.dynamic_snapshot, input)) {
    invalidate_dynamic(state, input.now_monotonic_ms);
    state.diagnostics.last_thermal_window_status = ThermalWindowStatus::INVALID_MEASUREMENT;
  } else {
    const ThermalWindowResult window = observe_thermal_snapshot(state.thermal_accumulator, input.dynamic_snapshot,
                                                                state.config.quality, state.config.thermal_window);
    state.diagnostics.last_thermal_window_status = window.status;
    if (window.has_interval) {
      const ThermalUpdateResult update =
          observe_thermal_interval(state.thermal_state, window.interval, state.config.thermal_model);
      state.diagnostics.last_thermal_update_status = update.status;
      if (update.accepted) {
        if (state.diagnostics.accepted_thermal_intervals != UINT32_MAX) ++state.diagnostics.accepted_thermal_intervals;
      } else if (state.diagnostics.rejected_thermal_observations != UINT32_MAX) {
        ++state.diagnostics.rejected_thermal_observations;
      }
    } else if (window.status != ThermalWindowStatus::COLLECTING) {
      invalidate_dynamic(state, input.now_monotonic_ms);
    }
  }

  if (appended_record || state.fit_pending)
    start_fit(state, input);
  else
    advance_passive_fit(state);
  state.status = state.fit_running ? PassiveRuntimeStatus::FIT_IN_PROGRESS : PassiveRuntimeStatus::COLLECTING;
  return state.status;
}

inline PassiveRuntimeSummary passive_runtime_summary(const PassiveRuntimeStorage& state, uint64_t now_monotonic_ms) {
  PassiveRuntimeSummary summary;
  summary.status = state.status;
  summary.initialized = state.initialized;
  summary.blocked = state.blocked;
  summary.opted_in = state.opted_in;
  summary.restored_records = state.restored_records;
  summary.current_observation_valid = state.current_observation_valid;
  summary.fit_running = state.fit_running;
  const bool live_ready_context =
      state.initialized && !state.blocked && state.opted_in && state.current_observation_valid;
  summary.batch_advice_ready = live_ready_context && state.batch_result.advice_ready;
  summary.record_count = state.record_count;
  summary.source_generation = state.source_generation;
  summary.physical_context_generation = state.physical_context_generation;
  summary.control_generation = state.control_generation;
  summary.last_epoch_s = state.last_epoch_s;
  summary.last_monotonic_ms = state.last_monotonic_ms;
  summary.batch = state.batch_result;
  summary.thermal = estimate_thermal_model(state.thermal_state, state.config.thermal_model, now_monotonic_ms);
  summary.thermal_model_ready = live_ready_context && summary.thermal.ready;
  if (live_ready_context)
    summary.validation = validate_house_models(
        state.batch_result, state.thermal_state, state.config.thermal_model, now_monotonic_ms, state.source_generation,
        state.physical_context_generation, state.control_generation, state.config.validation);
  summary.cross_validated_advice_ready = summary.validation.cross_validated_advice_ready;
  summary.auto_apply_allowed = false;
  if (!state.initialized)
    summary.status = PassiveRuntimeStatus::INVALID_CONFIGURATION;
  else if (state.blocked)
    summary.status = PassiveRuntimeStatus::BLOCKED;
  else if (!state.opted_in)
    summary.status = PassiveRuntimeStatus::PAUSED;
  else if (summary.cross_validated_advice_ready)
    summary.status = PassiveRuntimeStatus::ADVICE_READY;
  else if (state.fit_running)
    summary.status = PassiveRuntimeStatus::FIT_IN_PROGRESS;
  else
    summary.status = PassiveRuntimeStatus::COLLECTING;
  summary.diagnostics = state.diagnostics;
  return summary;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
