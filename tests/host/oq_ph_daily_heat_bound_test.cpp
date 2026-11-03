#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

struct FakeGenerationSelect {
  bool has_state() const { return false; }
  std::string current_option() const { return ""; }
} hp_generation;
#define id(value) value
#include "../../openquatt/includes/performance/hp_perf_limits.h"
#undef id
#define OQ_PH_LEARNING_HOST_TEST 1
#include "../../openquatt/includes/learning/oq_ph_daily_collection_diagnostics.h"
#include "../../openquatt/includes/learning/oq_ph_learning_persistence_logic.h"
#include "oq_ph_daily_source_fixture.h"

namespace {
using namespace oq_power_house::learning;
using oq_perf::ReferenceGeneration;
constexpr uint32_t kEpoch = 20000U * 86400U;
constexpr uint8_t kContext[]{1, 7, 4, 9};
const PassiveContextView context{kContext, sizeof(kContext), 1};

LearningSnapshot sample(uint32_t second, float heat = 0.0f) {
  LearningSnapshot s;
  s.monotonic_ms = 1000ULL + second * 1000ULL;
  s.epoch_s = kEpoch + second;
  s.context_revision = 1;
  s.room_c = s.setpoint_c = 20.0f;
  s.outside_c = 5.0f;
  s.heat_to_water_w = heat;
  s.mean_water_c = 30.0f;
  return s;
}

QualityConfig quality(ReferenceGeneration generation, bool duo) {
  QualityConfig q;
  q.max_daily_missing_heat_w = oq_perf::reference_maximum_heating_w(generation, duo);
  assert(valid_quality_config(q));
  assert(q.max_abs_heat_w == 50000.0f);
  return q;
}

void assert_same_journal_except_saved_floor(const uint8_t* first, const uint8_t* second, size_t size) {
  assert(size >= kLearningJournalDailyBytes + kLearningJournalCrcBytes);
  const size_t floor_offset =
      size - kLearningJournalCrcBytes - kLearningJournalDailyBytes + kLearningJournalDailyV7Bytes;
  assert(std::memcmp(first, second, floor_offset) == 0);
  assert(std::memcmp(first + floor_offset, second + floor_offset, sizeof(float)) != 0);
  const size_t suffix_offset = floor_offset + sizeof(float);
  assert(std::memcmp(first + suffix_offset, second + suffix_offset, size - kLearningJournalCrcBytes - suffix_offset) ==
         0);
}

void test_envelopes_cover_model_outputs_and_unknown_is_not_zero() {
  assert(std::fabs(oq_perf::kV1ReferenceMaximumW - 6115.28f) < 0.001f);
  assert(std::fabs(oq_perf::kV2ReferenceMaximumW - 9172.15f) < 0.001f);
  for (int a = 0; a < oq_perf_v1::N_AMB; ++a)
    for (int s = 0; s < oq_perf_v1::N_SUP; ++s)
      for (int l = 0; l < oq_perf_v1::N_LVL; ++l) {
        const float power = oq_perf_v1::P_th_W[a][s][l];
        if (std::isfinite(power)) assert(power <= oq_perf::kV1ReferenceMaximumW);
      }
  bool saw_raw_surface_above_native_output_cap = false;
  for (const double water : oq_v2_model::data::SUPPLY_C)
    for (const double outside : oq_v2_model::data::AMBIENT_C)
      for (const double hz : oq_v2_model::data::FREQUENCY_HZ) {
        const auto value = oq_v2_model::predict_heating(hz, outside, water);
        if (value.valid_running_point()) assert(value.pth_w <= oq_perf::kV2ReferenceMaximumW);
      }
  for (const auto& supply : oq_v2_model::data::PTH_W)
    for (const auto& outside : supply)
      for (const float cell : outside) saw_raw_surface_above_native_output_cap |= cell > oq_perf::kV2ReferenceMaximumW;
  assert(saw_raw_surface_above_native_output_cap);
  for (const auto generation : {ReferenceGeneration::V1, ReferenceGeneration::V1_5, ReferenceGeneration::V2}) {
    const float single = oq_perf::reference_maximum_heating_w(generation, false);
    assert(single > 0.0f && oq_perf::reference_maximum_heating_w(generation, true) == 2.0f * single);
  }
  for (const auto unknown : {ReferenceGeneration::UNKNOWN, static_cast<ReferenceGeneration>(255)}) {
    for (const bool duo : {false, true}) {
      const auto q = quality(unknown, duo);
      assert(configured_daily_missing_heat_bound_w(q) == 50000.0f);
      assert(daily_missing_heat_bound_w(q, 0, 0) == 50000.0);
    }
  }
}

void test_each_generation_and_topology_cumulatively_charges_its_own_bound() {
  for (const auto generation :
       {ReferenceGeneration::V1, ReferenceGeneration::V1_5, ReferenceGeneration::V2, ReferenceGeneration::UNKNOWN}) {
    for (const bool duo : {false, true}) {
      const auto q = quality(generation, duo);
      SegmentAccumulator state;
      DailyCollectionDiagnostics diagnostics;
      observe_daily_with_diagnostics(diagnostics, state, sample(0), q, false, SnapshotSourceStatus::OK);
      const double minute_wh = configured_daily_missing_heat_bound_w(q) / 60.0;
      const unsigned first_above_advice_budget = static_cast<unsigned>(2400.0 / minute_wh) + 1U;
      for (unsigned i = 1; i <= first_above_advice_budget + 1U; ++i) {
        auto missing = sample(i * 60 - 30);
        missing.invalid_reasons = INVALID_ESSENTIAL_SOURCE;
        observe_daily_with_diagnostics(diagnostics, state, missing, q, true, SnapshotSourceStatus::MISSING_MEASUREMENT);
        const auto result =
            observe_daily_with_diagnostics(diagnostics, state, sample(i * 60), q, true, SnapshotSourceStatus::OK);
        assert(result.status == LearningStatus::COLLECTING && result.cause == DailyCollectionCause::NONE);
        assert(state.start_epoch_s == kEpoch && state.integrated_duration_s == i * 60);
        assert(std::fabs(state.missing_energy_uncertainty_ws / 3600.0 - i * minute_wh) < 0.00001);
        assert(state.heat_observed_ms == 0 && state.temperature_observed_ms == 0);
        assert(diagnostics.interrupted_periods == 0 && diagnostics.last_loss.cause == DailyCollectionCause::NONE);
      }
      assert(state.missing_energy_uncertainty_ws > 2400.0 * 3600.0);
    }
  }
}

void test_measured_signed_endpoints_raise_bound_without_clamping_or_relaxing_validation() {
  const auto q = quality(ReferenceGeneration::V1, false);
  for (const float heat : {-20000.0f, 20000.0f}) {
    assert(validate_snapshot(sample(0, heat), q) == LearningStatus::OK);
    assert(daily_missing_heat_bound_w(q, heat, 0.0f) == 20000.0);
    SegmentAccumulator state;
    observe_snapshot(state, sample(0, heat), q);
    auto missing = sample(30);
    missing.invalid_reasons = INVALID_ESSENTIAL_SOURCE;
    observe_snapshot(state, missing, q, true);
    const auto result = observe_snapshot(state, sample(60, -heat), q, true);
    assert(result.status == LearningStatus::COLLECTING);
    // Bound20kW*60s + two interpolation triangles600kWs =500Wh, signed area0.
    assert(std::fabs(state.missing_energy_uncertainty_ws / 3600.0 - 500.0) < 0.00001);
    assert(std::fabs(state.heat_integral) < 0.00001);
  }
  assert(validate_snapshot(sample(0, 50001), q) == LearningStatus::INVALID_MEASUREMENT);
  assert(std::isnan(daily_missing_heat_bound_w(q, 50001, 0)));
  assert(std::isnan(daily_missing_heat_bound_w(q, NAN, 0)));
  for (const float bad : {-1.0f, INFINITY, NAN, 50001.0f}) {
    auto invalid = q;
    invalid.max_daily_missing_heat_w = bad;
    assert(!valid_quality_config(invalid));
  }
  for (const float bad : {-1.0f, INFINITY, NAN, 50001.0f}) assert(std::isnan(daily_missing_heat_bound_w(q, 0, 0, bad)));
}

void test_existing_checkpoint_debt_and_bytes_are_not_rewritten_by_new_policy() {
  SegmentAccumulator prefix;
  observe_snapshot(prefix, sample(0), QualityConfig{});
  observe_snapshot(prefix, sample(60), QualityConfig{});
  prefix.missing_energy_uncertainty_ws = 1000.0 * 3600.0;
  LearningJournalStore old_store, new_store;
  size_t old_size = 0, new_size = 0;
  const auto q = quality(ReferenceGeneration::V1, true);
  assert(encode_learning_journal({nullptr, 0, context}, QualityConfig{}, 7, kEpoch + 60, old_store.bytes[0],
                                 sizeof(old_store.bytes[0]), old_size, nullptr, 0,
                                 &prefix) == LearningJournalStatus::OK);
  assert(encode_learning_journal({nullptr, 0, context}, q, 7, kEpoch + 60, new_store.bytes[0],
                                 sizeof(new_store.bytes[0]), new_size, nullptr, 0,
                                 &prefix) == LearningJournalStatus::OK);
  assert(old_size == new_size);
  // Schema9 appends independent-Q state after the schema8 floor. Only the
  // policy floor and its CRC differ; saved measurements and debt stay exact.
  assert_same_journal_except_saved_floor(old_store.bytes[0], new_store.bytes[0], old_size);
  const auto metadata =
      inspect_learning_journal({old_store.bytes[0], old_size}, kContext, sizeof(kContext), kEpoch + 120, q);
  assert(metadata.status == LearningJournalStatus::OK);
  LearningJournalRecords records{{old_store.bytes[0], old_size}, sizeof(kContext), 0};
  SegmentAccumulator resumed;
  assert(records.restore_daily(resumed, context, q));
  assert(resumed.missing_energy_uncertainty_ws == 1000.0 * 3600.0);
  auto next = sample(120);
  next.monotonic_ms = 30000;
  assert(observe_snapshot(resumed, next, q).status == LearningStatus::COLLECTING);
  assert(resumed.start_epoch_s == kEpoch);
  const double expected_wh = 1000.0 + q.max_abs_heat_w / 60.0;
  assert(resumed.gap_heat_bound_floor_w == 0.0f);
  assert(std::fabs(resumed.missing_energy_uncertainty_ws / 3600.0 - expected_wh) < 0.00001);
}

LearningSourceInput single_source(uint32_t second, float heat) {
  auto raw = daily_source_fixture::source(second, 0.0f);
  raw.topology = HydronicTopology::SINGLE;
  raw.hp2 = {};
  raw.hp1.water_in_c.value = 30.0f;
  raw.hp1.water_out_c.value = 30.0f + heat * 3600.0f / (1000.0f * 4180.0f);
  return raw;
}

SnapshotBuildResult source_snapshot(uint32_t second, float heat, unsigned missing_field, const QualityConfig& q) {
  auto raw = single_source(second, heat);
  if (missing_field == 1) raw.room_c.valid = false;
  if (missing_field == 2) raw.setpoint_c.valid = false;
  if (missing_field == 3) raw.outside_c.valid = false;
  const auto cal = evaluate_calorimetry(raw, q);
  assert(cal.valid);
  return build_learning_snapshot(raw, q, SnapshotPurpose::STRUCTURAL_BATCH, &cal);
}

void test_real_source_builder_preserves_signed_heat_while_temperatures_are_missing() {
  const auto q = quality(ReferenceGeneration::V1, false);
  for (const float peak : {-20000.0f, 20000.0f}) {
    for (unsigned field : {1U, 2U, 3U}) {
      SegmentAccumulator state;
      DailyCollectionDiagnostics diagnostics;
      auto start = source_snapshot(0, 0, 0, q);
      observe_daily_with_diagnostics(diagnostics, state, start.snapshot, q, start.may_bridge_daily_gap, start.status);
      state.missing_energy_uncertainty_ws = 2200.0 * 3600.0;
      double measured_peak = 0.0;
      for (unsigned t = 10; t <= 50; t += 10) {
        const auto incomplete = source_snapshot(t, t == 10 ? peak : 0.0f, field, q);
        assert(incomplete.may_bridge_daily_gap && !incomplete.measurement_valid);
        assert(std::isnan(incomplete.snapshot.heat_to_water_w));
        assert(std::fabs(incomplete.snapshot.partial_heat_to_water_w - (t == 10 ? peak : 0.0f)) < 0.01f);
        if (t == 10) measured_peak = incomplete.snapshot.partial_heat_to_water_w;
        assert(std::fabs(incomplete.snapshot.gap_heat_observed_abs_w - (t == 10 ? std::fabs(peak) : 0.0f)) < 0.01f);
        observe_daily_with_diagnostics(diagnostics, state, incomplete.snapshot, q, incomplete.may_bridge_daily_gap,
                                       incomplete.status);
        assert(state.gap_heat_bound_floor_w >= 19999.9f);
        const double expected_signed_heat = (t == 10 ? 5.0 : 10.0) * measured_peak;
        assert(std::fabs(state.heat_integral - expected_signed_heat) < 0.000001);
        assert(state.integrated_duration_s == 0.0);
        assert(state.heat_observed_ms == t * 1000U && state.temperature_observed_ms == 0);
        assert(state.missing_energy_uncertainty_ws == 2200.0 * 3600.0);
        assert(state.last_heat_w == 0.0f && state.last_epoch_s == kEpoch);
      }
      const auto end = source_snapshot(60, 0, 0, q);
      const auto resumed =
          observe_daily_with_diagnostics(diagnostics, state, end.snapshot, q, end.may_bridge_daily_gap, end.status);
      assert(resumed.status == LearningStatus::COLLECTING && resumed.cause == DailyCollectionCause::NONE);
      assert(state.start_epoch_s == kEpoch && state.integrated_duration_s == 60.0);
      assert(std::fabs(state.heat_integral - 10.0 * measured_peak) < 0.000001);
      assert(state.missing_energy_uncertainty_ws == 2200.0 * 3600.0);
      assert(state.heat_observed_ms == 60000 && state.temperature_observed_ms == 0);
      assert(diagnostics.interrupted_periods == 0 && diagnostics.last_loss.cause == DailyCollectionCause::NONE);
      assert(state.gap_heat_bound_floor_w == 0.0f);
    }
  }
}

void test_open_peak_survives_with_saved_envelope_floor() {
  const auto q = quality(ReferenceGeneration::V1, false);
  SegmentAccumulator state;
  const auto start = source_snapshot(0, 0, 0, q);
  observe_snapshot(state, start.snapshot, q);
  state.missing_energy_uncertainty_ws = 1000.0 * 3600.0;
  const auto incomplete = source_snapshot(30, 20000, 1, q);
  observe_snapshot(state, incomplete.snapshot, q, incomplete.may_bridge_daily_gap);
  assert(state.source_gap_pending && state.gap_heat_bound_floor_w > 19999.9f);
  auto without_peak_metadata = state;
  without_peak_metadata.gap_heat_bound_floor_w = 0.0f;
  LearningJournalStore with_peak, without_peak;
  size_t with_size = 0, without_size = 0;
  assert(encode_learning_journal({nullptr, 0, context}, q, 7, kEpoch + 30, with_peak.bytes[0],
                                 sizeof(with_peak.bytes[0]), with_size, nullptr, 0,
                                 &state) == LearningJournalStatus::OK);
  assert(encode_learning_journal({nullptr, 0, context}, q, 7, kEpoch + 30, without_peak.bytes[0],
                                 sizeof(without_peak.bytes[0]), without_size, nullptr, 0,
                                 &without_peak_metadata) == LearningJournalStatus::OK);
  assert(with_size == without_size);
  assert_same_journal_except_saved_floor(with_peak.bytes[0], without_peak.bytes[0], with_size);
  LearningJournalRecords records{{with_peak.bytes[0], with_size}, sizeof(kContext), 0};
  SegmentAccumulator restored;
  assert(records.restore_daily(restored, context, q));
  assert(restored.missing_energy_uncertainty_ws == state.missing_energy_uncertainty_ws);
  assert(restored.gap_heat_bound_floor_w >= 19999.9f && restored.gap_heat_bound_floor_w <= 20000.01f);
  auto end = sample(60);
  end.monotonic_ms = 30000;
  const auto result = observe_snapshot(restored, end, q);
  assert(result.status == LearningStatus::COLLECTING);
  // The first30s were measured despite missing room data. Only the remaining
  // 30s boot interval has bound20kW plus the20kW-to-zero interpolation triangle.
  assert(std::fabs(restored.missing_energy_uncertainty_ws / 3600.0 - 1250.0) < 0.001);
  assert(std::fabs(restored.heat_integral - 30.0 * incomplete.snapshot.partial_heat_to_water_w) < 0.000001);
  assert(restored.heat_observed_ms == 30000 && restored.temperature_observed_ms == 0);
  assert(restored.gap_heat_bound_floor_w == 0.0f);
  // Next ordinary gap returns to the table, without reducing the saved debt.
  const double previous = restored.missing_energy_uncertainty_ws;
  auto missing = sample(90);
  missing.monotonic_ms = 60000;
  missing.invalid_reasons = INVALID_ESSENTIAL_SOURCE;
  observe_snapshot(restored, missing, q, true);
  auto next = sample(120);
  next.monotonic_ms = 90000;
  assert(observe_snapshot(restored, next, q, true).status == LearningStatus::COLLECTING);
  assert(std::fabs((restored.missing_energy_uncertainty_ws - previous) / 3600.0 -
                   configured_daily_missing_heat_bound_w(q) / 60.0) < 0.00001);
}

void test_new_checkpoint_restores_table_basis_and_saved_floor_cannot_be_reduced() {
  const auto q = quality(ReferenceGeneration::V1, false);
  SegmentAccumulator prefix;
  observe_snapshot(prefix, sample(0), q);
  observe_snapshot(prefix, sample(60), q);
  prefix.missing_energy_uncertainty_ws = 1000.0 * 3600.0;
  LearningJournalStore store;
  size_t size = 0;
  assert(encode_learning_journal({nullptr, 0, context}, q, 7, kEpoch + 60, store.bytes[0], sizeof(store.bytes[0]), size,
                                 nullptr, 0, &prefix) == LearningJournalStatus::OK);
  LearningJournalRecords records{{store.bytes[0], size}, sizeof(kContext), 0};
  auto smaller_policy = q;
  smaller_policy.max_daily_missing_heat_w = 1000.0f;
  SegmentAccumulator restored;
  assert(records.restore_daily(restored, context, smaller_policy));
  assert(restored.gap_heat_bound_floor_w == q.max_daily_missing_heat_w);
  assert(restored.missing_energy_uncertainty_ws == prefix.missing_energy_uncertainty_ws);
  auto next = sample(120);
  next.monotonic_ms = 30000;
  assert(observe_snapshot(restored, next, smaller_policy).status == LearningStatus::COLLECTING);
  assert(std::fabs(restored.missing_energy_uncertainty_ws / 3600.0 - (1000 + q.max_daily_missing_heat_w / 60.0)) <
         0.00001);
  assert(restored.gap_heat_bound_floor_w == 0.0f);
}

void test_verified_restart_keeps_large_table_bound_debt_within_time_ceiling() {
  const auto q = quality(ReferenceGeneration::V2, true);
  const float heat = configured_daily_missing_heat_bound_w(q);
  SegmentAccumulator state;
  observe_snapshot(state, sample(0, heat), q);
  observe_snapshot(state, sample(60, heat), q);
  state.restart_pending = true;
  state.heat_gap_pending = true;  // Match the independent heat cursor after journal decode.
  state.restart_policy = DailyRestartPolicy::VERIFIED_SOFTWARE_RESTART;
  auto next = sample(360, heat);
  next.monotonic_ms = 30000;
  const auto result = observe_snapshot(state, next, q);
  assert(result.cause == DailyCollectionCause::NONE && result.status == LearningStatus::COLLECTING);
  assert(state.start_epoch_s == kEpoch && state.integrated_duration_s == 360.0);
  assert(std::fabs(state.missing_energy_uncertainty_ws - 2.0 * heat * 300.0) < 0.001);
  assert(state.missing_energy_uncertainty_ws > 2400.0 * 3600.0);
  assert(std::fabs(state.heat_integral - 360.0 * heat) < 0.001);
  assert(state.heat_observed_ms == 60000 && state.temperature_observed_ms == 60000);
  assert(state.restart_policy == DailyRestartPolicy::STANDARD && !state.restart_pending);
}

void test_day_boundary_uses_same_policy_for_collection_and_diagnostics() {
  const auto q = quality(ReferenceGeneration::V2, true);
  SegmentAccumulator state;
  DailyCollectionDiagnostics diagnostics;
  for (unsigned t = 0; t <= 86340; t += 60)
    observe_daily_with_diagnostics(diagnostics, state, sample(t), q, false, SnapshotSourceStatus::OK);
  for (unsigned t : {86370U, 86400U, 86430U}) {
    auto gap = sample(t);
    gap.invalid_reasons = INVALID_ESSENTIAL_SOURCE;
    observe_daily_with_diagnostics(diagnostics, state, gap, q, true, SnapshotSourceStatus::MISSING_MEASUREMENT);
  }
  const auto done =
      observe_daily_with_diagnostics(diagnostics, state, sample(86460), q, true, SnapshotSourceStatus::OK);
  assert(done.has_record && done.record.duration_s == 86400);
  const double minute_wh = configured_daily_missing_heat_bound_w(q) / 60.0;
  assert(std::fabs(diagnostics.last_completed_uncertainty_ws / 3600.0 - minute_wh) < 0.00001);
  assert(std::fabs(state.missing_energy_uncertainty_ws / 3600.0 - minute_wh) < 0.00001);
}
}  // namespace

int main() {
  test_envelopes_cover_model_outputs_and_unknown_is_not_zero();
  test_each_generation_and_topology_cumulatively_charges_its_own_bound();
  test_measured_signed_endpoints_raise_bound_without_clamping_or_relaxing_validation();
  test_existing_checkpoint_debt_and_bytes_are_not_rewritten_by_new_policy();
  test_real_source_builder_preserves_signed_heat_while_temperatures_are_missing();
  test_open_peak_survives_with_saved_envelope_floor();
  test_new_checkpoint_restores_table_basis_and_saved_floor_cannot_be_reduced();
  test_verified_restart_keeps_large_table_bound_debt_within_time_ceiling();
  test_day_boundary_uses_same_policy_for_collection_and_diagnostics();
}
