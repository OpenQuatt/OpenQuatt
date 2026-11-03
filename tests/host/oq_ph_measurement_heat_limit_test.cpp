#include <cassert>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>

struct FakeGenerationSelect {
  bool has_state() const { return false; }
  std::string current_option() const { return ""; }
} hp_generation;
#define id(value) value
#include "../../openquatt/includes/performance/hp_perf_limits.h"
#undef id
#define OQ_PH_LEARNING_HOST_TEST 1
#include "../../openquatt/includes/learning/oq_ph_learning_journal.h"
#include "oq_ph_daily_source_fixture.h"

namespace {
using namespace daily_source_fixture;
using oq_perf::ReferenceGeneration;
const PassiveContextView context{kContext, sizeof(kContext), 1};

QualityConfig quality(ReferenceGeneration generation = ReferenceGeneration::V1, bool duo = true) {
  QualityConfig q;
  q.max_daily_missing_heat_w = oq_perf::configured_reference_maximum_heating_w(generation, duo);
  q.max_observed_heat_w = oq_perf::measurement_maximum_heating_w(generation, duo);
  assert(valid_quality_config(q));
  return q;
}

// Use the production builders with the actual quality; the shared fixture's
// production_tick intentionally uses the legacy/default QualityConfig instead.
PassiveTickInput tick(const LearningSourceInput& raw, const QualityConfig& q) {
  const auto calorimetry = evaluate_calorimetry(raw, q);
  const auto batch = build_learning_snapshot(raw, q, SnapshotPurpose::STRUCTURAL_BATCH, &calorimetry);
  const auto dynamic = build_learning_snapshot(raw, q, SnapshotPurpose::THERMAL_DYNAMIC, &calorimetry);
  auto input = production_tick(raw);
  input.batch_snapshot_available = batch.has_snapshot;
  input.may_bridge_daily_gap = batch.may_bridge_daily_gap;
  input.batch_source_status = batch.status;
  input.batch_snapshot = batch.snapshot;
  input.dynamic_snapshot_available = dynamic.has_snapshot;
  input.dynamic_snapshot = dynamic.snapshot;
  return input;
}

void configured_limits_and_exact_live_boundary() {
  assert(configured_measurement_heat_limit_w(QualityConfig{}) == 50000.0f);
  for (const auto generation : {ReferenceGeneration::V1, ReferenceGeneration::V1_5, ReferenceGeneration::V2,
                                ReferenceGeneration::UNKNOWN, static_cast<ReferenceGeneration>(255)}) {
    for (const bool duo : {false, true}) {
      const auto q = quality(generation, duo);
      const float reference = oq_perf::configured_reference_maximum_heating_w(generation, duo);
      const float expected = generation == ReferenceGeneration::V1 || generation == ReferenceGeneration::V1_5
                                 ? oq_perf::kV1ReferenceMaximumW
                                 : oq_perf::kV2ReferenceMaximumW;
      assert(reference == expected * (duo ? 2.0f : 1.0f));
      assert(q.max_abs_heat_w == 50000.0f && q.max_observed_heat_w == reference * 1.20f);
      auto sample = production_tick(source(0)).batch_snapshot;
      for (const float sign : {-1.0f, 1.0f}) {
        sample.heat_to_water_w = sign * q.max_observed_heat_w;
        assert(validate_snapshot(sample, q) == LearningStatus::OK);
        sample.heat_to_water_w = sign * std::nextafter(q.max_observed_heat_w, INFINITY);
        assert(validate_snapshot(sample, q) == LearningStatus::INVALID_MEASUREMENT);
        assert(validate_snapshot_structure(sample, q) == LearningStatus::OK);
      }
      sample.heat_to_water_w = 0;
      assert(validate_snapshot(sample, q) == LearningStatus::OK);
    }
  }
  // Pin the actual float calorimetry output, avoiding water-temperature rounding
  // when testing the inclusive boundary through the complete source pipeline.
  for (const float heat : {-7000.0f, 7000.0f}) {
    const auto raw = source(0, heat);
    const float measured = std::fabs(evaluate_calorimetry(raw, QualityConfig{}).heat_to_water_w);
    QualityConfig q;
    q.max_observed_heat_w = measured;
    assert(tick(raw, q).batch_source_status == SnapshotSourceStatus::OK);
    q.max_observed_heat_w = std::nextafter(measured, 0.0f);
    assert(tick(raw, q).batch_source_status == SnapshotSourceStatus::HEAT_OUT_OF_RANGE);
  }
  assert(tick(source(0, 0), quality()).batch_source_status == SnapshotSourceStatus::OK);
  assert(tick(source(0, 51000), quality()).batch_source_status == SnapshotSourceStatus::INVALID_VALUE);
}

void short_outliers_retain_real_day_but_never_integrate_the_outlier() {
  const auto q = quality();
  for (const float heat : {-20000.0f, 20000.0f}) {
    auto runtime = std::make_unique<PassiveRuntimeStorage>();
    PassiveRuntimeConfig config;
    config.quality = q;
    assert(initialize_passive_runtime(*runtime, context, config, true) == PassiveRuntimeStatus::COLLECTING);
    const auto first = tick(source(0), q);
    tick_passive_runtime(*runtime, first);
    const auto raw = source(60, heat);
    const float rejected = std::fabs(evaluate_calorimetry(raw, QualityConfig{}).heat_to_water_w);
    const auto invalid = tick(raw, q);
    assert(invalid.may_bridge_daily_gap && invalid.batch_source_status == SnapshotSourceStatus::HEAT_OUT_OF_RANGE);
    assert(std::isnan(invalid.batch_snapshot.heat_to_water_w));
    assert(std::isnan(invalid.batch_snapshot.partial_heat_to_water_w));
    tick_passive_runtime(*runtime, invalid);
    const auto& state = runtime->batch_accumulator;
    assert(state.active && state.source_gap_pending && state.start_epoch_s == kEpoch);
    assert(state.heat_integral == 0 && state.heat_observed_ms == 0 && state.temperature_observed_ms == 0);
    assert(state.gap_heat_bound_floor_w >= rejected);
    assert(!runtime->thermal_accumulator.active && runtime->thermal_accumulator.heat_integral_ms == 0);
    assert(runtime->thermal_state.accepted_samples == 0);
    tick_passive_runtime(*runtime, tick(source(120), q));
    const double normal_heat = first.batch_snapshot.heat_to_water_w;
    assert(state.active && state.start_epoch_s == kEpoch && !state.source_gap_pending);
    assert(std::fabs(state.heat_integral - normal_heat * 120.0) < 0.01);
    assert(std::fabs(state.missing_energy_uncertainty_ws - (rejected + std::fabs(normal_heat)) * 120.0) < 1.0);
    assert(state.heat_observed_ms == 0 && state.temperature_observed_ms == 0);
    assert(!passive_runtime_summary(*runtime, source(120).monotonic_ms).auto_apply_allowed);
    SegmentAccumulator expired;
    observe_snapshot(expired, first.batch_snapshot, q);
    for (const uint32_t second : {60U, 120U}) {
      const auto bad = tick(source(second, heat), q);
      observe_snapshot(expired, bad.batch_snapshot, q, bad.may_bridge_daily_gap);
    }
    const auto result = observe_snapshot(expired, tick(source(150), q).batch_snapshot, q);
    assert(result.cause == DailyCollectionCause::GAP_TOO_LONG && !result.has_record);
    assert(expired.start_epoch_s == kEpoch + 150);
  }
}

void hard_failures_win_over_the_soft_heat_status() {
  const auto q = quality();
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto raw = source(60, 20000);
    if (fault == 0) raw.boiler_heat.value = BoilerHeatState::HEAT_ACTIVE;
    if (fault == 1) raw.hp1.water_in_c.source.unit = PhysicalUnit::HP2;
    if (fault == 2) raw.flow_lph.timing.max_age_ms = 0;
    if (fault == 3) raw.hp1.water_in_c.received_monotonic_ms = raw.monotonic_ms + 1;
    const auto bad = tick(raw, q);
    assert(!bad.may_bridge_daily_gap && bad.batch_source_status != SnapshotSourceStatus::HEAT_OUT_OF_RANGE);
    SegmentAccumulator state;
    observe_snapshot(state, tick(source(0), q).batch_snapshot, q);
    observe_snapshot(state, bad.batch_snapshot, q, bad.may_bridge_daily_gap);
    assert(!state.active);
  }
}

void lower_live_limit_preserves_old_high_records_checkpoint_and_rls() {
  const auto q = quality();
  auto old_quality = q;
  old_quality.max_observed_heat_w = 0;
  for (const float heat : {20000.0f, 30000.0f}) {
    SegmentRecord record;
    record.start_epoch_s = kEpoch - 86400;
    record.end_epoch_s = kEpoch;
    record.duration_s = 86400;
    record.context_revision = 1;
    record.mean_room_c = record.mean_setpoint_c = 20;
    record.mean_outside_c = 5;
    record.mean_heat_w = heat;
    record.room_trend_k_per_h = 0;
    for (auto& value : record.effective_outside_profile_centi) value = 500;
    SegmentAccumulator checkpoint;
    for (const uint32_t second : {0U, 60U})
      observe_snapshot(checkpoint, tick(source(second, heat), old_quality).batch_snapshot, old_quality);
    checkpoint.gap_heat_bound_floor_w = 40000;
    checkpoint.missing_energy_uncertainty_ws = 12345678;
    assert(valid_daily_checkpoint(checkpoint, q));
    ThermalModelConfig thermal_config;
    ThermalModelState thermal;
    assert(initialize_thermal_model(thermal, thermal_config));
    ThermalInterval interval;
    interval.start_monotonic_ms = 1000;
    interval.end_monotonic_ms = 1801000;
    interval.context_revision = 1;
    interval.complete = interval.inputs_fresh = interval.generations_consistent = true;
    interval.operational_gates_passed = interval.hidden_heat_exclusion_valid = interval.hidden_heat_excluded = true;
    interval.indoor_start_c = 20;
    interval.indoor_end_c = 20.1;
    interval.mean_indoor_c = 20.05;
    interval.mean_outside_c = 5;
    interval.mean_heat_w = 4200;
    assert(observe_thermal_interval(thermal, interval, thermal_config).accepted);
    uint8_t bytes[kLearningJournalMaxBytes];
    size_t size = 0;
    assert(encode_learning_journal({&record, 1, context}, old_quality, 1, kEpoch + 60, bytes, sizeof(bytes), size,
                                   &thermal, kEpoch - 30, &checkpoint) == LearningJournalStatus::OK);
    const auto metadata = inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), kEpoch + 60, q);
    assert(metadata.status == LearningJournalStatus::OK);
    LearningJournalRecords records{{bytes, size}, metadata.context_size, metadata.record_count};
    assert(records[0].mean_heat_w == heat && records[0].start_epoch_s == record.start_epoch_s);
    SegmentAccumulator restored;
    assert(records.restore_daily(restored, context, q));
    assert(restored.last_heat_w == checkpoint.last_heat_w && restored.heat_last_w == checkpoint.heat_last_w);
    assert(restored.gap_heat_bound_floor_w == 40000 && restored.missing_energy_uncertainty_ws == 12345678);
    ThermalModelState restored_thermal;
    uint32_t accepted_epoch = 0;
    assert(records.restore_thermal(restored_thermal, thermal_config, 2001000, 1, accepted_epoch));
    assert(accepted_epoch == kEpoch - 30 && restored_thermal.accepted_samples == thermal.accepted_samples);
    assert(restored_thermal.theta_loss_scaled == thermal.theta_loss_scaled);
    assert(restored_thermal.theta_heat_scaled == thermal.theta_heat_scaled);
    assert(restored_thermal.covariance_00 == thermal.covariance_00);
    assert(restored_thermal.covariance_01 == thermal.covariance_01);
    assert(restored_thermal.covariance_11 == thermal.covariance_11);
  }
}
void malformed_live_limits_fail_closed() {
  const auto q = quality();
  const auto raw = source(0, 3000);
  const auto snapshot = build_learning_snapshot(raw, q).snapshot;
  for (const float limit : {-1.0f, NAN, INFINITY, 999.0f, 50001.0f}) {
    auto malformed = q;
    malformed.max_observed_heat_w = limit;
    assert(!valid_quality_config(malformed));
    assert(validate_snapshot(snapshot, malformed) == LearningStatus::INVALID_CONFIGURATION);
    assert(!evaluate_calorimetry(raw, malformed).valid);
  }
}
}  // namespace

int main() {
  configured_limits_and_exact_live_boundary();
  malformed_live_limits_fail_closed();
  short_outliers_retain_real_day_but_never_integrate_the_outlier();
  hard_failures_win_over_the_soft_heat_status();
  lower_live_limit_preserves_old_high_records_checkpoint_and_rls();
}
