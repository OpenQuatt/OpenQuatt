#include <cassert>
#include <cmath>
#include <cstring>
#include "oq_ph_daily_source_fixture.h"
#include "../../openquatt/includes/learning/oq_ph_learning_journal.h"
#include "../../openquatt/includes/learning/oq_ph_daily_record_quality.h"

namespace {
using namespace daily_source_fixture;
QualityConfig quality() {
  QualityConfig q;
  q.max_daily_missing_heat_w = 12230.56f;
  return q;
}
LearningSnapshot snapshot(uint32_t second, float q = 3000.0f) {
  LearningSnapshot s;
  s.monotonic_ms = 1000ULL + second * 1000ULL;
  s.epoch_s = kEpoch + second;
  s.context_revision = 1;
  s.room_c = s.setpoint_c = 20;
  s.outside_c = 5;
  s.mean_water_c = 30;
  s.heat_to_water_w = q;
  return s;
}
LearningSnapshot partial(uint32_t second, float q) {
  auto s = snapshot(second);
  s.room_c = s.heat_to_water_w = NAN;
  s.invalid_reasons = INVALID_ESSENTIAL_SOURCE;
  s.partial_heat_to_water_w = q;
  s.gap_heat_observed_abs_w = fabsf(q);
  return s;
}
void prefix(SegmentAccumulator& s, uint32_t stop) {
  for (uint32_t t = 0; t <= stop; t += 10) {
    const auto r = observe_snapshot(s, snapshot(t), quality());
    assert(r.status == LearningStatus::COLLECTING && !r.has_record);
  }
}
void measured_heat_survives_missing_room_but_missing_flow_is_explicit() {
  for (bool missing_flow : {false, true}) {
    SegmentAccumulator state;
    ObserveResult result;
    for (uint32_t second = 0; second <= 86400; second += 10) {
      auto raw = source(second);
      if (second != 0 && second % 3300 == 0) {
        if (missing_flow)
          raw.flow_lph.valid = false;
        else
          raw.room_c.valid = false;
      }
      auto input = production_tick(raw);
      if (second != 0 && second % 3300 == 0) {
        assert(isnan(input.batch_snapshot.heat_to_water_w));
        assert(missing_flow ? isnan(input.batch_snapshot.partial_heat_to_water_w)
                            : fabsf(input.batch_snapshot.partial_heat_to_water_w - 5000) < .1f);
      }
      result = observe_snapshot(state, input.batch_snapshot, quality(), input.may_bridge_daily_gap);
      assert(result.cause == DailyCollectionCause::NONE);
    }
    assert(result.has_record && result.record.duration_s == 86400);
    assert(fabsf(result.record.mean_heat_w - 5000) < .1f);
    const auto& q = result.record.daily_quality;
    assert(q.temperature_coverage_bp == 9939);
    assert(q.heat_coverage_bp == (missing_flow ? 9939 : 10000));
    assert(q.heat_uncertainty_mean_w == (missing_flow ? 104 : 0));
    assert(daily_record_fit_eligible(result.record, quality()) == !missing_flow);
    assert(result.record.start_epoch_s == kEpoch && state.start_epoch_s == kEpoch + 86400);
  }
}
void independent_signed_heat_splits_at_day_boundary() {
  SegmentAccumulator state;
  prefix(state, 86380);
  const double initial = state.heat_integral;
  observe_snapshot(state, partial(86390, 20000), quality(), true);
  observe_snapshot(state, partial(86400, -10000), quality(), true);
  observe_snapshot(state, partial(86410, 1000), quality(), true);
  observe_snapshot(state, partial(86420, 15000), quality(), true);
  observe_snapshot(state, partial(86430, -5000), quality(), true);
  const auto done = observe_snapshot(state, snapshot(86440, 4000), quality(), true);
  assert(done.has_record && done.record.daily_quality.heat_coverage_bp == 10000);
  assert(done.record.daily_quality.heat_uncertainty_mean_w == 0);
  assert(fabs(done.record.mean_heat_w - (initial + 165000) / 86400) < .001);
  assert(fabs(state.heat_integral - 80000) < .001 && state.heat_last_elapsed_ms == 40000);
  assert(state.heat_observed_ms == 40000 && state.temperature_observed_ms == 0);
  assert(state.missing_energy_uncertainty_ws == 0 && state.heat_last_w == 4000);
  observe_snapshot(state, snapshot(86450, 6000), quality());
  assert(fabs(state.heat_integral - 130000) < .001);
}
void checkpoint_preserves_future_bucket_and_charges_only_reboot_gap() {
  SegmentAccumulator before;
  prefix(before, 86380);
  const double initial = before.heat_integral;
  observe_snapshot(before, partial(86390, 20000), quality(), true);
  observe_snapshot(before, partial(86400, -10000), quality(), true);
  observe_snapshot(before, partial(86410, 1000), quality(), true);
  assert(valid_daily_checkpoint(before, quality()));
  uint8_t bytes[kLearningJournalMaxBytes];
  size_t size = 0;
  PassiveContextView context{kContext, sizeof(kContext), 1};
  assert(encode_learning_journal({nullptr, 0, context}, quality(), 1, kEpoch + 86410, bytes, sizeof(bytes), size,
                                 nullptr, 0, &before) == LearningJournalStatus::OK);
  auto meta = inspect_learning_journal({bytes, size}, kContext, sizeof(kContext), kEpoch + 86410, quality());
  assert(meta.status == LearningJournalStatus::OK);
  LearningJournalRecords view{{bytes, size}, meta.context_size, meta.record_count};
  SegmentAccumulator restored;
  assert(view.restore_daily(restored, context, quality()));
  assert(restored.heat_last_elapsed_ms == 86410000 && restored.heat_last_epoch_s == kEpoch + 86410);
  assert(restored.next_heat_integral_ws == -45000 && restored.next_heat_observed_ms == 10000);
  auto endpoint = snapshot(86440, 4000);
  endpoint.monotonic_ms = 5000;
  const auto done = observe_snapshot(restored, endpoint, quality());
  assert(done.has_record && fabs(done.record.mean_heat_w - (initial + 165000) / 86400) < .001);
  assert(fabs(restored.heat_integral - 30000) < .001 && restored.heat_observed_ms == 10000);
  assert(fabs(restored.missing_energy_uncertainty_ws - 675000) < .001);
  assert(restored.temperature_observed_ms == 0 && segment_elapsed_ms(restored) == 40000);
  assert(done.record.daily_quality.heat_uncertainty_mean_w == 0);
  assert(valid_daily_checkpoint(restored, quality()));
}
void restart_wait_retains_valid_heat_separately_from_temperature() {
  SegmentAccumulator state;
  prefix(state, 3600);
  state.carried_duration_ms = 3600000;
  state.start_monotonic_ms = state.last_monotonic_ms = 1;
  state.restart_pending = state.source_gap_pending = state.heat_gap_pending = true;
  state.last_gap_observation_ms = 1;
  assert(detail::observe_restart_heat_only(state, partial(3620, 1000), quality(), 300000));
  const auto debt = state.missing_energy_uncertainty_ws;
  assert(debt > 0 && state.heat_last_elapsed_ms == 3620000);
  assert(detail::observe_restart_heat_only(state, partial(3630, -1000), quality(), 300000));
  assert(state.missing_energy_uncertainty_ws == debt);
  auto endpoint = snapshot(3640, 5000);
  endpoint.monotonic_ms = 5000;
  const auto r = observe_snapshot(state, endpoint, quality());
  assert(r.status == LearningStatus::COLLECTING && state.missing_energy_uncertainty_ws == debt);
  assert(fabs(state.heat_integral - (3600.0 * 3000 + 20 * 2000 + 10 * 0 + 10 * 2000)) < .001);
  assert(state.heat_observed_ms == 3620000 && state.temperature_observed_ms == 3600000);
}
}  // namespace
int main() {
  measured_heat_survives_missing_room_but_missing_flow_is_explicit();
  independent_signed_heat_splits_at_day_boundary();
  checkpoint_preserves_future_bucket_and_charges_only_reboot_gap();
  restart_wait_retains_valid_heat_separately_from_temperature();
}
