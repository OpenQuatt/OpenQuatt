#include <assert.h>

#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;

constexpr uint8_t kContext[] = {1, 7, 4, 9, 2, 6};

PassiveContextView context(uint32_t revision = 1) { return {kContext, sizeof(kContext), revision}; }

PassiveRuntimeConfig config() {
  PassiveRuntimeConfig value;
  value.thermal_model.initial_heat_loss_w_per_k = 150.0;
  return value;
}

LearningSnapshot snapshot(uint64_t monotonic_ms, uint32_t epoch_s) {
  LearningSnapshot value;
  value.monotonic_ms = monotonic_ms;
  value.epoch_s = epoch_s;
  value.context_revision = 1;
  value.room_c = 20.0f;
  value.setpoint_c = 20.0f;
  value.outside_c = 5.0f;
  value.heat_to_water_w = 2200.0f;
  value.mean_water_c = 30.0f;
  return value;
}

PassiveTickInput tick(uint64_t monotonic_ms, uint32_t epoch_s) {
  PassiveTickInput input;
  input.context = context();
  input.now_monotonic_ms = monotonic_ms;
  input.now_epoch_s = epoch_s;
  input.opted_in = true;
  input.context_valid = true;
  input.active_line_valid = true;
  input.active_line = {150.0f, 15.0f};
  input.reference_context_valid = true;
  input.reference_room_c = 20.0f;
  input.reference_setpoint_c = 20.0f;
  input.batch_snapshot_available = true;
  input.batch_snapshot = snapshot(monotonic_ms, epoch_s);
  input.dynamic_snapshot_available = true;
  input.dynamic_snapshot = input.batch_snapshot;
  return input;
}

void test_collects_fixed_records_and_fit_is_resumable() {
  PassiveRuntimeStorage state;
  assert(initialize_passive_runtime(state, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  constexpr uint32_t start_epoch = 20000U * 86400U + 3600U;
  constexpr uint64_t start_ms = 1000;
  for (uint32_t minute = 0; minute <= 240; ++minute) {
    const auto input = tick(start_ms + minute * 60000ULL, start_epoch + minute * 60U);
    tick_passive_runtime(state, input);
  }
  assert(state.record_count == 1);
  assert(state.diagnostics.accepted_batch_records == 1);
  assert(!state.fit_running);
  assert(state.batch_result.status == LearningStatus::INSUFFICIENT_DATA);
  assert(!passive_runtime_summary(state, state.last_monotonic_ms).auto_apply_allowed);
  static_assert(sizeof(PassiveRuntimeStorage) < 32U * 1024U);
}

void test_pause_revokes_ready_state_without_discarding_records() {
  PassiveRuntimeStorage state;
  assert(initialize_passive_runtime(state, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  constexpr uint32_t now_epoch = 20000U * 86400U;
  state.records[0].start_epoch_s = now_epoch - 4U * 3600U;
  state.records[0].end_epoch_s = now_epoch;
  state.records[0].duration_s = 4U * 3600U;
  state.records[0].context_revision = 1;
  state.records[0].mean_room_c = 20.0f;
  state.records[0].mean_setpoint_c = 20.0f;
  state.records[0].mean_outside_c = 5.0f;
  state.records[0].mean_heat_w = 2200.0f;
  state.records[0].room_trend_k_per_h = 0.0f;
  state.records[0].room_range_k = 0.0f;
  state.records[0].setpoint_range_c = 0.0f;
  state.records[0].water_start_c = 30.0f;
  state.records[0].water_end_c = 30.0f;
  state.record_count = 1;
  state.batch_result.advice_ready = true;
  auto input = tick(1000, now_epoch);
  input.batch_snapshot_available = false;
  input.dynamic_snapshot_available = false;
  input.context_valid = false;
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::PAUSED);
  const auto summary = passive_runtime_summary(state, input.now_monotonic_ms);
  assert(state.record_count == 1);
  assert(!summary.batch_advice_ready);
  assert(!summary.cross_validated_advice_ready);
  assert(!summary.auto_apply_allowed);
}

void test_context_change_restarts_both_models_and_rejects_old_input() {
  PassiveRuntimeStorage state;
  assert(initialize_passive_runtime(state, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  auto input = tick(1000, 20000U * 86400U);
  tick_passive_runtime(state, input);
  state.record_count = 1;
  state.thermal_state.accepted_samples = 10;
  input.now_monotonic_ms += 1000;
  ++input.now_epoch_s;
  input.context = context(2);
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::CONTEXT_CHANGED);
  assert(state.context_revision == 2 && state.record_count == 0 && state.thermal_state.accepted_samples == 0);
  input.context = context();
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::STALE_CONTEXT);
  assert(state.blocked);

  reset_passive_runtime(state);
  assert(!state.blocked && !state.opted_in && state.record_count == 0);
  initialize_passive_runtime(state, context(2), config(), true);
  input = tick(6000, 20000U * 86400U + 6U);
  input.context = context(2);
  input.batch_snapshot.context_revision = 2;
  input.dynamic_snapshot = input.batch_snapshot;
  tick_passive_runtime(state, input);
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::TIME_DISCONTINUITY);
}

void test_direct_pause_breaks_continuity_and_revokes_advice() {
  PassiveRuntimeStorage state;
  initialize_passive_runtime(state, context(), config(), true);
  tick_passive_runtime(state, tick(1000, 20000U * 86400U));
  assert(state.batch_accumulator.active && state.thermal_accumulator.active);
  state.batch_result.advice_ready = true;
  pause_passive_runtime(state, 2000);
  assert(!state.batch_accumulator.active && !state.thermal_accumulator.active);
  const auto paused = passive_runtime_summary(state, 2000);
  assert(!paused.batch_advice_ready && !paused.thermal_model_ready && !paused.auto_apply_allowed);
  tick_passive_runtime(state, tick(3000, 20000U * 86400U + 2U));
  assert(state.batch_accumulator.integrated_duration_s == 0);
}

void test_evaluation_refresh_keeps_physical_evidence() {
  PassiveRuntimeStorage state;
  initialize_passive_runtime(state, context(), config(), true);
  tick_passive_runtime(state, tick(1000, 20000U * 86400U));
  state.records[0] = {};
  state.records[0].context_revision = 1;
  state.record_count = 1;
  state.thermal_state.accepted_samples = 12;
  state.fit_running = true;
  state.fit_pending = false;
  state.fit_inputs_bound = true;
  state.batch_result.advice_ready = true;

  auto refreshed_config = config();
  refreshed_config.thermal_model.max_residual_rms_k_per_h = 0.75f;
  refresh_passive_evaluation(state, refreshed_config);

  assert(state.record_count == 1);
  assert(state.thermal_state.accepted_samples == 12);
  assert(state.config.thermal_model.max_residual_rms_k_per_h == 0.75f);
  assert(!state.batch_accumulator.active && !state.thermal_accumulator.active);
  assert(!state.fit_running && state.fit_pending && !state.fit_inputs_bound);
  assert(!state.batch_result.advice_ready);
}

void test_invalid_evaluation_refresh_stays_paused_until_corrected() {
  PassiveRuntimeStorage state;
  initialize_passive_runtime(state, context(), config(), true);
  auto invalid_config = config();
  invalid_config.thermal_model.initial_heat_loss_w_per_k = NAN;
  refresh_passive_evaluation(state, invalid_config);
  assert(state.status == PassiveRuntimeStatus::INVALID_CONFIGURATION);
  assert(tick_passive_runtime(state, tick(1000, 20000U * 86400U)) == PassiveRuntimeStatus::INVALID_CONFIGURATION);

  refresh_passive_evaluation(state, config());
  assert(tick_passive_runtime(state, tick(2000, 20000U * 86400U + 1U)) == PassiveRuntimeStatus::COLLECTING);
}

void test_active_or_reference_change_revokes_bound_fit_result() {
  PassiveRuntimeStorage state;
  assert(initialize_passive_runtime(state, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  state.fit_inputs_bound = true;
  state.fit_active_line = {150.0f, 15.0f};
  state.fit_reference_room_c = 20.0f;
  state.fit_reference_setpoint_c = 20.0f;
  state.batch_result.status = LearningStatus::ADVICE_READY;
  state.batch_result.candidate_available = true;
  state.batch_result.advice_ready = true;
  auto input = tick(1000, 20000U * 86400U);
  input.reference_room_c = 20.1f;
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::COLLECTING);
  assert(!state.fit_inputs_bound);
  assert(!state.batch_result.advice_ready);
}

void test_missing_utc_pauses_without_blocking_owner() {
  PassiveRuntimeStorage state;
  assert(initialize_passive_runtime(state, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  auto input = tick(1000, 0);
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::PAUSED);
  input = tick(2000, 20000U * 86400U);
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::COLLECTING);
}

void test_summary_never_exposes_readiness_without_live_valid_context() {
  PassiveRuntimeStorage state;
  state.batch_result.advice_ready = true;
  state.thermal_state.recent_data_valid = true;
  auto summary = passive_runtime_summary(state, 1000);
  assert(summary.status == PassiveRuntimeStatus::INVALID_CONFIGURATION);
  assert(!summary.batch_advice_ready && !summary.thermal_model_ready && !summary.cross_validated_advice_ready);

  assert(initialize_passive_runtime(state, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  state.batch_result.advice_ready = true;
  state.current_observation_valid = false;
  summary = passive_runtime_summary(state, 1000);
  assert(!summary.batch_advice_ready && !summary.thermal_model_ready && !summary.cross_validated_advice_ready);
}

}  // namespace

int main() {
  test_collects_fixed_records_and_fit_is_resumable();
  test_pause_revokes_ready_state_without_discarding_records();
  test_context_change_restarts_both_models_and_rejects_old_input();
  test_direct_pause_breaks_continuity_and_revokes_advice();
  test_evaluation_refresh_keeps_physical_evidence();
  test_invalid_evaluation_refresh_stays_paused_until_corrected();
  test_active_or_reference_change_revokes_bound_fit_result();
  test_missing_utc_pauses_without_blocking_owner();
  test_summary_never_exposes_readiness_without_live_valid_context();
}
