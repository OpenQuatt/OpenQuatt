#include <cassert>
#include <cmath>
#include <cstring>

#define OQ_PH_LEARNING_HOST_TEST 1
#include "../../../openquatt/includes/control/oq_ph_daily_collection_state_json.h"
#include "../../../openquatt/includes/learning/oq_ph_learning_live_logic.h"
#include "../../../openquatt/includes/learning/oq_ph_learning_persistence_logic.h"
#include "../../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

#define ESP_LOGI(...) ((void)0)

using namespace oq_power_house::learning;
using namespace oq_ph_learning;
constexpr uint32_t kEpoch = 20000U * 86400U;
constexpr uint8_t kContext[]{1, 7, 4, 9};
const PassiveContextView context{kContext, sizeof(kContext), 1};
DailyRestartTicket daily_restart_ticket;

struct RuntimeStorage {
  PassiveRuntimeStorage learner;
  PassiveRuntimeConfig config;
  LearningJournalStore journal;
  LearningJournalRecords pending_daily;
  SegmentAccumulator pending_daily_heat;
  bool pending_daily_heat_initialized = false;
  uint64_t daily_resume_last_observation_ms = 0;
  DailyRestartTicket boot_restart_ticket{};
  DailyRestartDiagnostics daily_restart;
  bool daily_restore_pending = true, daily_restore_allowed = true, restart_prepared = false;
  bool restart_checkpoint_verified = true;
  const char* daily_resume_status = "waiting_for_sources";
  const char* daily_resume_reason = "none";
};

void production_recovery(RuntimeStorage& state, uint32_t epoch, bool valid_daily_observation,
                         bool source_may_wait = true, bool enabled = true, bool reset_processed = false,
                         unsigned clock_failure = 0, float partial_heat = NAN) {
  const uint64_t now_ms = epoch > kEpoch ? static_cast<uint64_t>(epoch - kEpoch) * 1000 : 1;
  LearningSourceInput input;
  input.operation.control_mode_valid = true;
  input.operation.control_mode = LearningControlMode::HEATING;
  if (!source_may_wait || clock_failure == 1) {
    input.operation.service_or_ota_valid = true;
    input.operation.service_or_ota = true;
  }
  if (clock_failure == 2) {
    input.room_c.received_monotonic_ms = 1000;
    input.room_c.provenance = MeasurementProvenance::PHYSICAL_RECEIPT;
  }
  const auto& in = input;
  const bool boiler_heat_observed = false;
  // Run the real preceding sticky veto as well as the pending-recovery owner.
#include "production_restart_interruption.inc"
  SnapshotBuildResult batch;
  batch.may_bridge_daily_gap = source_may_wait;
  if (isfinite(partial_heat)) {
    batch.has_snapshot = true;
    batch.snapshot.epoch_s = epoch;
    batch.snapshot.context_revision = 1;
    batch.snapshot.partial_heat_to_water_w = partial_heat;
    batch.snapshot.gap_heat_observed_abs_w = fabsf(partial_heat);
    batch.snapshot.invalid_reasons = INVALID_ESSENTIAL_SOURCE;
  }
  batch.status = valid_daily_observation ? SnapshotSourceStatus::OK : SnapshotSourceStatus::MISSING_MEASUREMENT;
#include "production_daily_restart.inc"
}

void production_prepared(RuntimeStorage& state, uint32_t epoch, uint64_t now_ms, bool valid_daily_observation) {
  const bool enabled = true;
  SnapshotBuildResult batch;
  batch.may_bridge_daily_gap = true;
  batch.has_snapshot = true;
  batch.snapshot.epoch_s = epoch;
  batch.snapshot.partial_heat_to_water_w = 1000;
  batch.snapshot.invalid_reasons = INVALID_ESSENTIAL_SOURCE;
#include "production_prepared_restart.inc"
}

void production_retry_prepare(RuntimeStorage& state) {
#include "production_retry_prepare.inc"
}

LearningSnapshot sample(uint32_t second, uint64_t mono) {
  LearningSnapshot s;
  s.monotonic_ms = mono;
  s.epoch_s = kEpoch + second;
  s.context_revision = 1;
  s.room_c = s.setpoint_c = 20.0f;
  s.outside_c = 5.0f;
  s.heat_to_water_w = 0.0f;
  s.mean_water_c = 30.0f;
  return s;
}

void prepare(RuntimeStorage& state) {
  SegmentAccumulator prefix;
  for (uint32_t t = 0; t <= 7200; t += 60)
    assert(observe_snapshot(prefix, sample(t, 1000ULL + t * 1000ULL), QualityConfig{}).status ==
           LearningStatus::COLLECTING);
  prefix.missing_energy_uncertainty_ws = 289.578 * 3600.0;
  size_t size = 0;
  assert(encode_learning_journal({nullptr, 0, context}, QualityConfig{}, 7, kEpoch + 7200, state.journal.bytes[0],
                                 sizeof(state.journal.bytes[0]), size, nullptr, 0,
                                 &prefix) == LearningJournalStatus::OK);
  const auto metadata = inspect_learning_journal({state.journal.bytes[0], size}, kContext, sizeof(kContext),
                                                 kEpoch + 7200, QualityConfig{});
  assert(metadata.status == LearningJournalStatus::OK);
  state.journal.available = state.journal.loaded = true;
  state.journal.active_slot = 0;
  state.journal.sequence = 7;
  state.journal.persisted_daily_checkpoint_epoch = kEpoch + 7200;
  state.pending_daily = {{state.journal.bytes[0], size}, sizeof(kContext), 0};
  DailyRestartTicket rtc{};
  rtc.arm(state.journal, true);
  state.boot_restart_ticket = rtc.consume(true);
  assert(!rtc.matches(7, state.pending_daily));
  state.learner.record_count = 17;
  for (size_t i = 0; i < 17; ++i) state.learner.records[i].mean_heat_w = 1000 + static_cast<float>(i);
  state.learner.thermal_state.accepted_samples = 1516;
}

void retained_data_unchanged(const RuntimeStorage& state) {
  assert(state.learner.record_count == 17 && state.learner.thermal_state.accepted_samples == 1516);
  for (size_t i = 0; i < 17; ++i) assert(state.learner.records[i].mean_heat_w == 1000 + static_cast<float>(i));
}

void test_actual_pending_owner_wait_and_125_second_restore() {
  RuntimeStorage state;
  prepare(state);
  for (uint32_t gap : {65U, 75U, 85U, 95U, 105U, 115U}) {
    production_recovery(state, kEpoch + 7200 + gap, false);
    assert(state.daily_restore_pending && !state.learner.batch_accumulator.active);
    assert(strcmp(state.daily_restart.time_decision, "wait") == 0);
    assert(state.daily_restart.verified_ticket && state.daily_restart.limit_ms == 300000);
    retained_data_unchanged(state);
  }
  production_recovery(state, kEpoch + 7325, true);
  assert(!state.daily_restore_pending && strcmp(state.daily_resume_status, "resuming") == 0);
  assert(state.learner.batch_accumulator.restart_policy == DailyRestartPolicy::VERIFIED_SOFTWARE_RESTART);
  const auto result = observe_snapshot(state.learner.batch_accumulator, sample(7325, 90000), state.config.quality);
  assert(result.status == LearningStatus::COLLECTING);
  assert(state.learner.batch_accumulator.integrated_duration_s == 7325);
  retained_data_unchanged(state);
}

void test_negative_owner_boundaries_do_not_resume_or_change_history() {
  for (unsigned failure = 0; failure < 8; ++failure) {
    RuntimeStorage state;
    prepare(state);
    uint32_t epoch = kEpoch + 7325;
    bool valid = true, wait = true, enabled = true, reset = false;
    if (failure == 0) state.boot_restart_ticket.magic = 0;
    if (failure == 1) ++state.journal.sequence;
    if (failure == 2) state.boot_restart_ticket.journal_crc ^= 1;
    if (failure == 3) state.daily_restore_allowed = false;
    if (failure == 4) epoch = kEpoch + 7199;
    if (failure == 5) epoch = kEpoch + 7501;
    if (failure == 6) {
      valid = false;
      wait = false;
    }
    if (failure == 7) {
      enabled = false;
      reset = true;
    }
    production_recovery(state, epoch, valid, wait, enabled, reset);
    assert(!state.daily_restore_pending && !state.learner.batch_accumulator.active);
    assert(strcmp(state.daily_resume_status, "discarded") == 0);
    retained_data_unchanged(state);
  }
}

void test_known_interruption_with_unknown_clock_stays_revoked() {
  for (unsigned failure : {1U, 2U}) {
    RuntimeStorage state;
    prepare(state);
    production_recovery(state, 0, false, true, true, false, failure);
    assert(!state.daily_restore_allowed && !state.daily_restore_pending);
    assert(strcmp(state.daily_resume_status, "discarded") == 0);
    assert(strcmp(state.daily_resume_reason, failure == 1 ? "service_or_ota" : "invalid_measurement") == 0);
    production_recovery(state, kEpoch + 7325, true);
    assert(!state.learner.batch_accumulator.active);
    retained_data_unchanged(state);
  }
}

void test_actual_wait_owner_keeps_independent_heat_then_restores_once() {
  RuntimeStorage state;
  prepare(state);
  production_recovery(state, kEpoch + 7220, false, true, true, false, 0, 1000);
  assert(state.pending_daily_heat_initialized && !state.learner.batch_accumulator.active);
  const auto first_debt = state.pending_daily_heat.missing_energy_uncertainty_ws;
  production_recovery(state, kEpoch + 7230, false, true, true, false, 0, -1000);
  assert(state.pending_daily_heat.missing_energy_uncertainty_ws == first_debt);
  production_recovery(state, kEpoch + 7240, true);
  assert(!state.pending_daily_heat_initialized && !state.daily_restore_pending);
  auto endpoint = sample(7240, 40000);
  endpoint.heat_to_water_w = 5000;
  auto& accumulator = state.learner.batch_accumulator;
  assert(observe_snapshot(accumulator, endpoint, QualityConfig{}).status == LearningStatus::COLLECTING);
  assert(accumulator.missing_energy_uncertainty_ws == first_debt);
  assert(accumulator.heat_integral == 20.0 * 500 + 10.0 * 0 + 10.0 * 2000);
  assert(accumulator.heat_observed_ms == 7220000 && accumulator.temperature_observed_ms == 7200000);
  retained_data_unchanged(state);
}

void test_regular_wait_nan_ticks_charge_heat_gap_without_discard() {
  RuntimeStorage state;
  prepare(state);
  production_recovery(state, kEpoch + 7220, false, true, true, false, 0, 1000);
  const double debt = state.pending_daily_heat.missing_energy_uncertainty_ws;
  for (uint32_t t = 7230; t <= 7300; t += 10) {
    production_recovery(state, kEpoch + t, false);
    assert(state.daily_restore_pending && state.daily_restore_allowed);
  }
  production_recovery(state, kEpoch + 7310, false, true, true, false, 0, -1000);
  assert(state.daily_restore_pending && state.pending_daily_heat.missing_energy_uncertainty_ws > debt);
  const double charged = state.pending_daily_heat.missing_energy_uncertainty_ws;
  production_recovery(state, kEpoch + 7320, true);
  auto endpoint = sample(7320, 90000);
  endpoint.heat_to_water_w = 5000;
  auto& a = state.learner.batch_accumulator;
  assert(observe_snapshot(a, endpoint, QualityConfig{}).status == LearningStatus::COLLECTING);
  assert(a.heat_observed_ms == 7210000 && a.temperature_observed_ms == 7200000);
  assert(a.missing_energy_uncertainty_ws == charged);
  retained_data_unchanged(state);
}
void test_same_boot_prepared_wait_keeps_the_same_tick_boundary() {
  RuntimeStorage state;
  prepare(state);
  assert(state.pending_daily.restore_daily(state.learner.batch_accumulator, context, QualityConfig{}));
  state.learner.batch_accumulator.restart_pending = false;
  state.daily_restore_pending = false;
  state.restart_prepared = true;
  production_prepared(state, kEpoch + 7220, 20000, false);
  assert(state.restart_prepared && state.daily_resume_last_observation_ms == 20000);
  production_prepared(state, kEpoch + 7310, 110000, false);
  assert(!state.restart_prepared && !state.learner.batch_accumulator.active);
  assert(!state.daily_restore_allowed && state.daily_resume_last_observation_ms == 0);
  retained_data_unchanged(state);
}

void test_retry_prepare_marks_a_new_gap_without_losing_measured_ram_heat() {
  RuntimeStorage state;
  prepare(state);
  assert(state.pending_daily.restore_daily(state.learner.batch_accumulator, context, QualityConfig{}));
  auto& a = state.learner.batch_accumulator;
  a.restart_pending = false;
  state.daily_restore_pending = false;
  state.restart_prepared = true;
  production_prepared(state, kEpoch + 7220, 20000, false);
  assert(!a.heat_gap_pending);
  const double debt = a.missing_energy_uncertainty_ws, heat = a.heat_integral;
  const uint32_t observed = a.heat_observed_ms, sequence = state.journal.sequence;
  production_retry_prepare(state);
  assert(a.heat_gap_pending && a.heat_integral == heat && a.heat_observed_ms == observed);
  assert(state.daily_resume_last_observation_ms == 20000 && state.journal.sequence == sequence);
  production_prepared(state, kEpoch + 7240, 40000, false);
  assert(!a.heat_gap_pending && a.heat_observed_ms == observed);
  assert(a.missing_energy_uncertainty_ws == debt + 20.0 * 51000.0);
  retained_data_unchanged(state);
}

void test_wait_observed_then_missed_tick_never_becomes_measured_heat() {
  for (bool missing_heat : {false, true}) {
    RuntimeStorage state;
    prepare(state);
    production_recovery(state, kEpoch + 7220, false, true, true, false, 0, 1000);
    const auto heat = state.pending_daily_heat.heat_integral;
    production_recovery(state, kEpoch + 7310, false, true, true, false, 0, missing_heat ? NAN : -1000);
    assert(!state.daily_restore_allowed && !state.daily_restore_pending);
    assert(!state.learner.batch_accumulator.active);
    assert(strcmp(state.daily_resume_reason, "checkpoint_tick_discontinuity") == 0);
    assert(state.pending_daily_heat.heat_integral == heat);
    production_recovery(state, kEpoch + 7320, true);
    assert(!state.learner.batch_accumulator.active);
    retained_data_unchanged(state);
  }
  assert(coherent_daily_restart_tick(1000, 61000, QualityConfig{}));
  assert(!coherent_daily_restart_tick(1000, 61001, QualityConfig{}));
  assert(!coherent_daily_restart_tick(1000, 1000, QualityConfig{}));
}

int main() {
  test_retry_prepare_marks_a_new_gap_without_losing_measured_ram_heat();
  test_regular_wait_nan_ticks_charge_heat_gap_without_discard();
  test_same_boot_prepared_wait_keeps_the_same_tick_boundary();
  test_wait_observed_then_missed_tick_never_becomes_measured_heat();
  test_actual_wait_owner_keeps_independent_heat_then_restores_once();
  test_actual_pending_owner_wait_and_125_second_restore();
  test_negative_owner_boundaries_do_not_resume_or_change_history();
  test_known_interruption_with_unknown_clock_stays_revoked();
}
