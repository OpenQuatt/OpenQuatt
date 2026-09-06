#include <assert.h>

#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;

constexpr uint8_t kContext[] = {1, 7, 4, 9, 2, 6};

PassiveContextView context(uint32_t source = 1, uint32_t physical = 1, uint32_t control = 1) {
  return {kContext, sizeof(kContext), source, physical, control};
}

PassiveRuntimeConfig config() {
  PassiveRuntimeConfig value;
  value.thermal_model.initial_heat_loss_w_per_k = 150.0;
  return value;
}

LearningSnapshot snapshot(uint64_t monotonic_ms, uint32_t epoch_s) {
  LearningSnapshot value;
  value.monotonic_ms = monotonic_ms;
  value.epoch_s = epoch_s;
  value.source_generation = 1;
  value.physical_context_generation = 1;
  value.control_generation = 1;
  value.room_c = 20.0f;
  value.setpoint_c = 20.0f;
  value.outside_c = 5.0f;
  value.heat_to_water_w = 2200.0f;
  value.heat_uncertainty_w = 50.0f;
  value.mean_water_c = 30.0f;
  return value;
}

PassiveTickInput tick(uint64_t monotonic_ms, uint32_t epoch_s) {
  PassiveTickInput input;
  input.owner_token = 10;
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
  assert(initialize_passive_runtime(state, 10, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
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
  assert(initialize_passive_runtime(state, 10, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  constexpr uint32_t now_epoch = 20000U * 86400U;
  state.records[0].start_epoch_s = now_epoch - 4U * 3600U;
  state.records[0].end_epoch_s = now_epoch;
  state.records[0].duration_s = 4U * 3600U;
  state.records[0].source_generation = 1;
  state.records[0].physical_context_generation = 1;
  state.records[0].control_generation = 1;
  state.records[0].mean_room_c = 20.0f;
  state.records[0].mean_setpoint_c = 20.0f;
  state.records[0].mean_outside_c = 5.0f;
  state.records[0].mean_heat_w = 2200.0f;
  state.records[0].mean_heat_uncertainty_w = 50.0f;
  state.records[0].room_trend_k_per_h = 0.0f;
  state.records[0].room_range_k = 0.0f;
  state.records[0].setpoint_range_c = 0.0f;
  state.records[0].water_start_c = 30.0f;
  state.records[0].water_end_c = 30.0f;
  state.record_count = 1;
  state.batch_result.advice_ready = true;
  state.validation_result.cross_validated_advice_ready = true;
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

void test_owner_context_and_time_aba_fail_closed() {
  PassiveRuntimeStorage state;
  assert(initialize_passive_runtime(state, 10, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  auto input = tick(1000, 20000U * 86400U);
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::COLLECTING);

  input.now_monotonic_ms += 1000;
  ++input.now_epoch_s;
  input.owner_token = 11;
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::OWNER_CHANGED);
  assert(state.owner_token == 11 && state.record_count == 0 && state.thermal_state.accepted_samples == 0);

  input.now_monotonic_ms += 1000;
  ++input.now_epoch_s;
  input.context = context(2, 1, 1);
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::CONTEXT_CHANGED);
  assert(state.source_generation == 2 && !state.blocked);

  constexpr uint8_t changed_context[] = {1, 7, 4, 9, 2, 7};
  input.now_monotonic_ms += 1000;
  ++input.now_epoch_s;
  input.context = {changed_context, sizeof(changed_context), 2, 1, 1};
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::STALE_CONTEXT);
  assert(state.blocked);

  assert(initialize_passive_runtime(state, 20, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  input = tick(5000, 20000U * 86400U + 5U);
  input.owner_token = 19;
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::STALE_OWNER);
  assert(state.blocked);

  assert(initialize_passive_runtime(state, 20, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  input = tick(6000, 20000U * 86400U + 6U);
  input.owner_token = 20;
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::COLLECTING);
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::TIME_DISCONTINUITY);
  assert(state.blocked);
}

void test_active_or_reference_change_revokes_bound_fit_result() {
  PassiveRuntimeStorage state;
  assert(initialize_passive_runtime(state, 10, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  state.fit_inputs_bound = true;
  state.fit_active_line = {150.0f, 15.0f};
  state.fit_reference_room_c = 20.0f;
  state.fit_reference_setpoint_c = 20.0f;
  state.batch_result.status = LearningStatus::ADVICE_READY;
  state.batch_result.candidate_available = true;
  state.batch_result.advice_ready = true;
  state.validation_result.cross_validated_advice_ready = true;
  auto input = tick(1000, 20000U * 86400U);
  input.reference_room_c = 20.1f;
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::COLLECTING);
  assert(!state.fit_inputs_bound);
  assert(!state.batch_result.advice_ready);
  assert(!state.validation_result.cross_validated_advice_ready);
}

void test_missing_utc_pauses_without_blocking_owner() {
  PassiveRuntimeStorage state;
  assert(initialize_passive_runtime(state, 10, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  auto input = tick(1000, 0);
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::PAUSED);
  assert(!state.blocked && state.owner_token == 10);
  input = tick(2000, 20000U * 86400U);
  assert(tick_passive_runtime(state, input) == PassiveRuntimeStatus::COLLECTING);
}

void test_summary_never_exposes_readiness_without_live_valid_context() {
  PassiveRuntimeStorage state;
  state.batch_result.advice_ready = true;
  state.validation_result.cross_validated_advice_ready = true;
  state.thermal_state.recent_data_valid = true;
  auto summary = passive_runtime_summary(state, 1000);
  assert(summary.status == PassiveRuntimeStatus::INVALID_CONFIGURATION);
  assert(!summary.batch_advice_ready && !summary.thermal_model_ready && !summary.cross_validated_advice_ready);

  assert(initialize_passive_runtime(state, 10, context(), config(), true) == PassiveRuntimeStatus::COLLECTING);
  state.batch_result.advice_ready = true;
  state.current_observation_valid = false;
  summary = passive_runtime_summary(state, 1000);
  assert(!summary.batch_advice_ready && !summary.thermal_model_ready && !summary.cross_validated_advice_ready);
}

void test_boot_reconfirmation_preserves_history_only_once() {
  CalorimetryReconfirmationState lifecycle;
  assert(!calorimetry_confirmation_invalidates(lifecycle, true));
  assert(calorimetry_confirmation_invalidates(lifecycle, false));
  assert(calorimetry_confirmation_invalidates(lifecycle, true));

  CalorimetryReconfirmationState rebooted;
  assert(!calorimetry_confirmation_invalidates(rebooted, true));
  CalorimetryReconfirmationState revoked_before_confirmation;
  assert(calorimetry_confirmation_invalidates(revoked_before_confirmation, false));
  assert(calorimetry_confirmation_invalidates(revoked_before_confirmation, true));
}

}  // namespace

int main() {
  test_collects_fixed_records_and_fit_is_resumable();
  test_pause_revokes_ready_state_without_discarding_records();
  test_owner_context_and_time_aba_fail_closed();
  test_active_or_reference_change_revokes_bound_fit_result();
  test_missing_utc_pauses_without_blocking_owner();
  test_summary_never_exposes_readiness_without_live_valid_context();
  test_boot_reconfirmation_preserves_history_only_once();
}
