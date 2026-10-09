#include <assert.h>
#include <math.h>

#define OQ_PH_LEARNING_HOST_TEST 1
#include "../../openquatt/includes/learning/oq_ph_learning_journal.h"
#include "../../openquatt/includes/learning/oq_ph_learning_live_logic.h"
#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house::learning;
constexpr uint32_t kEpoch = 20000U * 86400U;

LearningSnapshot snapshot(uint32_t second, uint64_t uptime_ms = 0) {
  LearningSnapshot value;
  value.monotonic_ms = uptime_ms == 0 ? 1000ULL + second * 1000ULL : uptime_ms;
  value.epoch_s = kEpoch + second;
  value.context_revision = 1;
  value.room_c = value.setpoint_c = 20.0f;
  value.outside_c = 5.0f;
  value.heat_to_water_w = 2000.0f;
  value.mean_water_c = 30.0f;
  return value;
}

void collect(SegmentAccumulator& state, uint32_t first, uint32_t last, uint64_t first_uptime = 0) {
  for (uint32_t second = first; second <= last; second += 10U) {
    auto value = snapshot(second, first_uptime == 0 ? 0 : first_uptime + (second - first) * 1000ULL);
    const auto result = observe_snapshot(state, value, QualityConfig{});
    assert(!result.has_record);
    assert(result.status == LearningStatus::COLLECTING);
  }
}

// Mirrors the journal's clock-free decode. Full encode/decode is tested separately.
void boot(SegmentAccumulator& state) {
  assert(valid_daily_checkpoint(state, QualityConfig{}));
  state.carried_duration_ms = static_cast<uint64_t>(llround(state.integrated_duration_s * 1000.0));
  state.start_monotonic_ms = state.last_monotonic_ms = 1;
  state.source_gap_pending = state.restart_pending = true;
  state.last_gap_observation_ms = 1;
}

void test_restart_keeps_eighteen_hours_and_finishes_same_day() {
  SegmentAccumulator state;
  collect(state, 0, 18U * 3600U);
  const double saved_heat = state.heat_integral;
  boot(state);
  const uint32_t resumed_second = 18U * 3600U + 90U;
  const auto resumed = observe_snapshot(state, snapshot(resumed_second, 30000), QualityConfig{});
  assert(resumed.status == LearningStatus::COLLECTING && !resumed.has_record);
  assert(!state.restart_pending && !state.source_gap_pending);
  assert(state.start_epoch_s == kEpoch && segment_elapsed_ms(state) == resumed_second * 1000ULL);
  assert(state.last_monotonic_ms == 30000 && state.start_monotonic_ms == 30000);
  assert(state.heat_integral == saved_heat + 90.0 * 2000.0);
  assert(state.missing_energy_uncertainty_ws == 90.0 * 52000.0);
  collect(state, resumed_second + 10U, 86400U - 10U, 40000);
  const auto completed =
      observe_snapshot(state, snapshot(86400, 30000ULL + (86400U - resumed_second) * 1000ULL), QualityConfig{});
  assert(completed.has_record && completed.record.duration_s == 86400U);
  assert(completed.record.start_epoch_s == kEpoch && completed.record.end_epoch_s == kEpoch + 86400U);
  assert(fabs(completed.record.mean_heat_w - 2000.0f) < 0.001);
  for (int16_t point : completed.record.effective_outside_profile_centi) assert(point == 500);
  assert(segment_elapsed_ms(state) == 0 && state.integrated_duration_s == 0.0);
}

void test_two_restarts_preserve_sums_and_charge_each_gap_once() {
  SegmentAccumulator state;
  collect(state, 0, 3600);
  boot(state);
  observe_snapshot(state, snapshot(3660, 10000), QualityConfig{});
  collect(state, 3670, 7200, 20000);
  assert(state.integrated_duration_s == 7200.0 && state.heat_integral == 7200.0 * 2000.0);
  boot(state);
  observe_snapshot(state, snapshot(7260, 5000), QualityConfig{});
  assert(state.start_epoch_s == kEpoch && state.integrated_duration_s == 7260.0);
  assert(state.missing_energy_uncertainty_ws == 120.0 * 52000.0);
  assert(segment_elapsed_ms(state) == 7260000ULL);
}

void test_restart_crossing_day_boundary_keeps_remainder_and_budget() {
  SegmentAccumulator state;
  collect(state, 0, 86340);
  boot(state);
  const auto result = observe_snapshot(state, snapshot(86430, 30000), QualityConfig{});
  assert(result.has_record && result.record.duration_s == 86400U);
  assert(result.record.end_epoch_s == kEpoch + 86400U);
  assert(state.start_epoch_s == kEpoch + 86400U && state.integrated_duration_s == 30.0);
  assert(state.heat_integral == 30.0 * 2000.0);
  assert(state.missing_energy_uncertainty_ws == 30.0 * 52000.0);
  assert(state.carried_duration_ms == 30000 && state.start_monotonic_ms == 30000);
  const auto next = observe_snapshot(state, snapshot(86440, 40000), QualityConfig{});
  assert(next.status == LearningStatus::COLLECTING && state.integrated_duration_s == 40.0);
}

void test_restart_rejects_unknown_state_and_long_or_backward_time() {
  for (int scenario = 0; scenario < 5; ++scenario) {
    SegmentAccumulator state;
    collect(state, 0, 3600);
    boot(state);
    auto value = snapshot(3660, 30000);
    if (scenario == 0) value.epoch_s = kEpoch + 3721U;
    if (scenario == 1) value.epoch_s = kEpoch + 3599U;
    if (scenario == 2) value.invalid_reasons = INVALID_SOURCE_UNCERTAIN;
    if (scenario == 3) value.invalid_reasons = INVALID_BOILER_HEAT;
    if (scenario == 4) value.context_revision = 2;
    const auto result = observe_snapshot(state, value, QualityConfig{});
    assert(!result.has_record);
    assert(!state.active || state.start_epoch_s == value.epoch_s);
    assert(state.integrated_duration_s == 0.0 && state.missing_energy_uncertainty_ws == 0.0);
  }
}

void test_restart_does_not_reset_existing_uncertainty_or_hide_omitted_ticks() {
  SegmentAccumulator state;
  collect(state, 0, 3600);
  state.missing_energy_uncertainty_ws = 8.0e6;
  boot(state);
  const auto exhausted = observe_snapshot(state, snapshot(3660, 30000), QualityConfig{});
  assert(exhausted.status == LearningStatus::SEGMENT_INELIGIBLE && !exhausted.has_record);
  assert(state.start_epoch_s == kEpoch + 3660U && segment_elapsed_ms(state) == 0);

  collect(state, 3670, 3700, 40000);
  const auto omitted = observe_snapshot(state, snapshot(3770, 140000), QualityConfig{}, true);
  assert(omitted.status == LearningStatus::TIME_DISCONTINUITY);
  assert(state.start_epoch_s == kEpoch + 3770U);
}

void test_corrupt_checkpoint_is_not_resumed() {
  SegmentAccumulator state;
  collect(state, 0, 3600);
  boot(state);
  state.heat_integral = NAN;
  const auto result = observe_snapshot(state, snapshot(3660, 30000), QualityConfig{});
  assert(result.status == LearningStatus::TIME_DISCONTINUITY);
  assert(state.start_epoch_s == kEpoch + 3660U && state.integrated_duration_s == 0.0);
}
void test_restore_waits_for_real_clock_and_fresh_sources() {
  assert(daily_resume_decision(true, false, 0, kEpoch) == DailyResumeDecision::WAIT);
  assert(daily_resume_decision(true, false, kEpoch + 60, kEpoch, true) == DailyResumeDecision::WAIT);
  assert(daily_resume_decision(true, true, kEpoch + 120, kEpoch) == DailyResumeDecision::RESTORE);
  assert(daily_resume_decision(true, false, kEpoch + 121, kEpoch) == DailyResumeDecision::DISCARD);
  assert(daily_resume_decision(true, true, kEpoch - 1, kEpoch) == DailyResumeDecision::DISCARD);
  assert(daily_resume_decision(false, true, kEpoch + 60, kEpoch) == DailyResumeDecision::DISCARD);
  assert(daily_resume_decision(true, true, kEpoch + 60, 0) == DailyResumeDecision::DISCARD);
}

void test_boot_waits_for_initial_operating_receipts() {
  LearningSourceInput input;
  input.monotonic_ms = 30000;
  input.epoch_s = kEpoch + 60;
  input.context_revision = 1;
  input.topology = HydronicTopology::SINGLE;
  input.hp1.present = true;
  input.operation.captured_monotonic_ms = input.monotonic_ms;
  input.operation.captured_context_revision = input.context_revision;
  input.operation.control_mode_valid = input.operation.service_or_ota_valid = true;
  input.operation.control_mode = LearningControlMode::HEATING;
  const auto batch = build_learning_snapshot(input, QualityConfig{});
  assert(batch.status == SnapshotSourceStatus::MISSING_MEASUREMENT);
  assert(!batch.may_bridge_daily_gap);  // A running day still cannot bridge unknown operation.
  assert(!known_daily_restart_interruption(input, false));
  const bool boot_pending = daily_boot_sources_pending(input, batch);
  assert(boot_pending);
  assert(daily_resume_decision(true, false, input.epoch_s, kEpoch, boot_pending) == DailyResumeDecision::WAIT);
  assert(daily_resume_decision(true, false, kEpoch + 121, kEpoch, boot_pending) == DailyResumeDecision::DISCARD);
  assert(daily_resume_decision(false, false, input.epoch_s, kEpoch, boot_pending) == DailyResumeDecision::DISCARD);
}

void test_observed_invalid_operation_never_waits_for_recovery() {
  // A missing scalar with known operating state may wait; boiler/cooling or
  // unknown operating state must discard even if the next value recovers.
  assert(daily_resume_decision(true, false, kEpoch + 60, kEpoch, true) == DailyResumeDecision::WAIT);
  assert(daily_resume_decision(true, false, kEpoch + 60, kEpoch, false) == DailyResumeDecision::DISCARD);
  assert(ota_notification_current(1000, 121000));
  assert(!ota_notification_current(1000, 121001));
  assert(ota_notification_current(1ULL << 32U, (1ULL << 32U) + 120000));
  assert(!ota_notification_current(1000, 999));
  int web = 0, native = 0;
  OtaCollectionPause pause;
  assert(!pause.active(1000));
  pause.started(&web, 1000);
  assert(pause.active(121000));
  assert(!pause.active(121001));  // TCP disconnect without ERROR.
  pause.started(&web, 1000);
  pause.progress(&web, 2000);
  assert(pause.active(122000));
  pause.started(&native, 21000);  // Another protocol retries the stale upload.
  pause.failed(&web);             // A late error belongs to the old protocol.
  assert(pause.active(22000));
  pause.progress(&web, 50000);  // Old progress must not extend the new lease.
  assert(!pause.active(141001));
  pause.started(&native, 150000);
  pause.failed(&native);
  assert(!pause.active(150000));
  assert(daily_resume_succeeded(LearningStatus::OK));
  assert(!daily_resume_succeeded(LearningStatus::SEGMENT_INELIGIBLE));
}

void test_runtime_journal_restart_preserves_day_history_and_thermal_only_after_valid_recovery() {
  constexpr uint8_t context_bytes[] = {1, 7, 4, 9};
  constexpr uint32_t checkpoint_second = 18U * 3600U;
  constexpr uint32_t resumed_second = checkpoint_second + 90U;
  auto context = [&](uint32_t revision) { return PassiveContextView{context_bytes, sizeof(context_bytes), revision}; };
  auto tick = [&](uint32_t second, uint64_t uptime_ms, uint32_t revision) {
    PassiveTickInput input;
    input.context = context(revision);
    input.now_epoch_s = kEpoch + second;
    input.now_monotonic_ms = uptime_ms;
    input.opted_in = input.context_valid = input.active_line_valid = true;
    input.active_line = {150.0f, 15.0f};
    input.batch_snapshot_available = input.dynamic_snapshot_available = true;
    input.batch_snapshot = snapshot(second, uptime_ms);
    input.batch_snapshot.context_revision = revision;
    input.dynamic_snapshot = input.batch_snapshot;
    assert(input.batch_snapshot.epoch_s == input.now_epoch_s &&
           input.batch_snapshot.monotonic_ms == input.now_monotonic_ms &&
           input.batch_snapshot.context_revision == input.context.context_revision);
    return input;
  };
  PassiveRuntimeStorage state;
  const PassiveRuntimeConfig config;
  assert(initialize_passive_runtime(state, context(1), config, true) == PassiveRuntimeStatus::COLLECTING);
  SegmentRecord history;
  history.start_epoch_s = kEpoch - 86400U;
  history.end_epoch_s = kEpoch;
  history.duration_s = 86400U;
  history.context_revision = 1;
  history.mean_room_c = history.mean_setpoint_c = 20;
  history.mean_outside_c = 5;
  history.mean_heat_w = 2000;
  history.room_trend_k_per_h = 0;
  for (int16_t& value : history.effective_outside_profile_centi) value = 500;
  assert(validate_segment_record(history, config.quality) == LearningStatus::OK);
  state.records[0] = history;
  state.record_count = 1;
  for (uint32_t second = 0; second <= checkpoint_second; second += 10U)
    tick_passive_runtime(state, tick(second, 1000ULL + second * 1000ULL, 1));
  assert(state.record_count == 1 && state.batch_accumulator.integrated_duration_s == checkpoint_second);
  assert(state.thermal_state.accepted_samples > 0);
  const double saved_heat = state.batch_accumulator.heat_integral;
  const auto saved_thermal = state.thermal_state;
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  assert(encode_learning_journal(passive_runtime_dataset(state), config.quality, 1, kEpoch + checkpoint_second, bytes,
                                 sizeof(bytes), size, &state.thermal_state, kEpoch + checkpoint_second,
                                 &state.batch_accumulator) == LearningJournalStatus::OK);
  const auto metadata = inspect_learning_journal({bytes, size}, context_bytes, sizeof(context_bytes),
                                                 kEpoch + resumed_second, config.quality);
  assert(metadata.status == LearningJournalStatus::OK);
  const LearningJournalRecords journal{{bytes, size}, metadata.context_size, metadata.record_count};
  assert(journal.daily_checkpoint_elapsed_s() == checkpoint_second);

  // All branches use the same immutable flash image; only a valid recovery
  // consumes its accumulator. Explicit unsafe operation permanently discards it.
  for (int scenario = 0; scenario < 3; ++scenario) {
    reset_passive_runtime(state);
    assert(!state.initialized && state.record_count == 0 && !state.batch_accumulator.active);
    assert(initialize_passive_runtime(state, context(7), config, true) == PassiveRuntimeStatus::COLLECTING);
    assert(restore_passive_records(state, journal, kEpoch + checkpoint_second));
    uint32_t thermal_epoch = 0;
    assert(journal.restore_thermal(state.thermal_state, config.thermal_model, 1000, 7, thermal_epoch));
    assert(thermal_epoch == kEpoch + checkpoint_second);
    assert(state.thermal_state.accepted_samples == saved_thermal.accepted_samples);
    assert(state.thermal_state.theta_loss_scaled == saved_thermal.theta_loss_scaled);
    assert(state.thermal_state.theta_heat_scaled == saved_thermal.theta_heat_scaled);
    assert(!state.thermal_state.recent_data_valid);
    bool pending = true;
    auto first = tick(checkpoint_second + 60U, 20000, 7);
    first.batch_snapshot.invalid_reasons = scenario == 0   ? INVALID_ESSENTIAL_SOURCE
                                           : scenario == 1 ? INVALID_BOILER_HEAT
                                                           : INVALID_COOLING;
    if (scenario == 0) first.batch_snapshot.room_c = NAN;
    first.dynamic_snapshot = first.batch_snapshot;
    first.may_bridge_daily_gap = scenario == 0;
    assert(validate_snapshot(first.batch_snapshot, config.quality) != LearningStatus::OK);
    const auto decision = daily_resume_decision(true, false, first.now_epoch_s, journal.daily_checkpoint_epoch(),
                                                first.may_bridge_daily_gap);
    assert(decision == (scenario == 0 ? DailyResumeDecision::WAIT : DailyResumeDecision::DISCARD));
    if (decision == DailyResumeDecision::DISCARD) pending = false;
    tick_passive_runtime(state, first);
    assert(!state.batch_accumulator.active && state.record_count == 1);
    assert(state.thermal_state.accepted_samples == saved_thermal.accepted_samples);

    if (scenario == 0) {
      oq_sources::ResolvedLearningSource sources[4]{};
      uint32_t source_revisions[4]{};
      uint32_t valid_source_revisions[4]{};
      for (size_t index = 0; index < 4; ++index) {
        sources[index].configuration_generation = 1;
        sources[index].valid = index != 0;
      }
      assert(source_configuration_available(sources));
      assert(observe_source_revisions(source_revisions, sources));
      assert(!observe_valid_source_revisions(valid_source_revisions, sources));
      sources[0].valid = true;
      sources[0].configuration_generation = 2;  // First route resolution during boot.
      assert(observe_source_revisions(source_revisions, sources));
      assert(!observe_valid_source_revisions(valid_source_revisions, sources));
      assert(state.context_revision == 7);
    }
    const auto recovered = tick(resumed_second, 50000, 7);
    if (pending) {
      assert(daily_resume_decision(
                 true, validate_snapshot(recovered.batch_snapshot, config.quality) == LearningStatus::OK,
                 recovered.now_epoch_s, journal.daily_checkpoint_epoch()) == DailyResumeDecision::RESTORE);
      assert(journal.restore_daily(state.batch_accumulator, context(7), config.quality));
      pending = false;
    }
    tick_passive_runtime(state, recovered);
    assert(!pending && !state.blocked && state.batch_accumulator.active && state.record_count == 1);
    assert(state.thermal_state.accepted_samples == saved_thermal.accepted_samples);
    assert(state.thermal_state.theta_loss_scaled == saved_thermal.theta_loss_scaled);
    if (scenario == 0) {
      assert(daily_resume_succeeded(state.diagnostics.last_batch_status));
      assert(state.batch_accumulator.start_epoch_s == kEpoch);
      assert(segment_elapsed_ms(state.batch_accumulator) == resumed_second * 1000ULL);
      assert(state.batch_accumulator.heat_integral == saved_heat + 90.0 * 2000.0);
      assert(state.batch_accumulator.missing_energy_uncertainty_ws == 90.0 * 52000.0);
    } else {
      assert(state.batch_accumulator.start_epoch_s == recovered.now_epoch_s);
      assert(segment_elapsed_ms(state.batch_accumulator) == 0);
      assert(state.batch_accumulator.missing_energy_uncertainty_ws == 0);
    }
    for (uint32_t second = resumed_second + 10U; second <= 86400U; second += 10U)
      tick_passive_runtime(state, tick(second, 50000ULL + (second - resumed_second) * 1000ULL, 7));
    assert(state.records[0].start_epoch_s == history.start_epoch_s &&
           state.records[0].end_epoch_s == history.end_epoch_s);
    assert(state.records[0].mean_heat_w == history.mean_heat_w && state.records[0].context_revision == 7);
    assert(state.thermal_state.accepted_samples >= saved_thermal.accepted_samples);
    assert(state.thermal_state.reset_count == saved_thermal.reset_count);
    if (scenario == 0) {
      assert(state.record_count == 2 && state.diagnostics.accepted_batch_records == 1);
      assert(state.records[1].start_epoch_s == kEpoch && state.records[1].end_epoch_s == kEpoch + 86400U);
      assert(state.records[1].duration_s == 86400U && fabs(state.records[1].mean_heat_w - 2000.0f) < 0.001);
      for (int16_t value : state.records[1].effective_outside_profile_centi) assert(value == 500);
    } else {
      assert(state.record_count == 1 && state.diagnostics.accepted_batch_records == 0);
      assert(state.batch_accumulator.start_epoch_s == recovered.now_epoch_s);
    }
  }
}

}  // namespace

int main() {
  test_runtime_journal_restart_preserves_day_history_and_thermal_only_after_valid_recovery();
  test_restart_keeps_eighteen_hours_and_finishes_same_day();
  test_two_restarts_preserve_sums_and_charge_each_gap_once();
  test_restart_crossing_day_boundary_keeps_remainder_and_budget();
  test_restart_rejects_unknown_state_and_long_or_backward_time();
  test_restart_does_not_reset_existing_uncertainty_or_hide_omitted_ticks();
  test_corrupt_checkpoint_is_not_resumed();
  test_restore_waits_for_real_clock_and_fresh_sources();
  test_boot_waits_for_initial_operating_receipts();
  test_observed_invalid_operation_never_waits_for_recovery();
}
