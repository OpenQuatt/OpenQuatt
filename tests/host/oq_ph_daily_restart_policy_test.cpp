#include <assert.h>
#include <cmath>
#include <cstring>

#define OQ_PH_LEARNING_HOST_TEST 1
#include "../../openquatt/includes/learning/oq_ph_learning_persistence_logic.h"
#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house::learning;
constexpr uint32_t kEpoch = 20000U * 86400U;
constexpr uint8_t kContext[]{1, 7, 4, 9};
const PassiveContextView context{kContext, sizeof(kContext), 1};

LearningSnapshot sample(uint32_t seconds, uint64_t mono = 0) {
  LearningSnapshot s;
  s.monotonic_ms = mono ? mono : 1000ULL + seconds * 1000ULL;
  s.epoch_s = kEpoch + seconds;
  s.context_revision = 1;
  s.room_c = s.setpoint_c = 20.0f;
  s.outside_c = 5.0f;
  s.heat_to_water_w = 0.0f;
  s.mean_water_c = 30.0f;
  return s;
}

SegmentAccumulator collect(const QualityConfig& quality = {}) {
  SegmentAccumulator s;
  for (uint32_t t = 0; t <= 7200; t += 60)
    assert(observe_snapshot(s, sample(t), quality).status == LearningStatus::COLLECTING);
  return s;
}

struct Saved {
  LearningJournalStore store;
  LearningJournalRecords records;
  DailyRestartTicket ticket{};
  Saved(const SegmentAccumulator& s, const QualityConfig& quality = {}, uint32_t sequence = 7) {
    size_t size = 0;
    assert(encode_learning_journal({nullptr, 0, context}, quality, sequence, s.last_epoch_s, store.bytes[0],
                                   sizeof(store.bytes[0]), size, nullptr, 0, &s) == LearningJournalStatus::OK);
    const auto metadata =
        inspect_learning_journal({store.bytes[0], size}, kContext, sizeof(kContext), s.last_epoch_s, quality);
    assert(metadata.status == LearningJournalStatus::OK);
    store.available = store.loaded = true;
    store.active_slot = 0;
    store.sequence = sequence;
    store.persisted_daily_checkpoint_epoch = s.last_epoch_s;
    records = {{store.bytes[0], size}, sizeof(kContext), 0};
    ticket.arm(store, true);
  }
};

void test_only_matching_verified_one_use_grant_selects_boot_policy() {
  Saved saved(collect());
  const auto verified = DailyRestartPolicy::VERIFIED_SOFTWARE_RESTART;
  const auto standard = DailyRestartPolicy::STANDARD;
  assert(saved.ticket.recovery_policy(7, saved.records, true) == verified);
  assert(saved.ticket.recovery_policy(7, saved.records, false) == standard);
  assert(saved.ticket.recovery_policy(8, saved.records, true) == standard);
  auto wrong = saved.ticket;
  ++wrong.checkpoint_epoch;
  assert(wrong.recovery_policy(7, saved.records, true) == standard);
  wrong = saved.ticket;
  wrong.journal_crc ^= 1;
  assert(wrong.recovery_policy(7, saved.records, true) == standard);
  const auto boot_ticket = saved.ticket.consume(true);
  assert(boot_ticket.recovery_policy(7, saved.records, true) == verified);
  assert(saved.ticket.consume(true).recovery_policy(7, saved.records, true) == standard);
  saved.ticket.arm(saved.store, true);
  assert(saved.ticket.consume(false).recovery_policy(7, saved.records, true) == standard);
  saved.ticket.arm(saved.store, false);
  assert(saved.ticket.recovery_policy(7, saved.records, true) == standard);
  static_assert(sizeof(DailyRestartTicket) == 16U);
}

void test_restart_ceiling_does_not_change_live_or_ota_lease_limits() {
  for (const auto policy : {DailyRestartPolicy::STANDARD, DailyRestartPolicy::VERIFIED_SOFTWARE_RESTART}) {
    const uint32_t limit = daily_restart_gap_limit_ms(policy) / 1000U;
    assert(daily_resume_decision(true, true, kEpoch + limit, kEpoch, true, policy) == DailyResumeDecision::RESTORE);
    assert(daily_resume_decision(true, true, kEpoch + limit + 1, kEpoch, true, policy) == DailyResumeDecision::DISCARD);
    assert(daily_resume_decision(true, false, kEpoch + limit, kEpoch, true, policy) == DailyResumeDecision::WAIT);
    assert(daily_resume_decision(true, false, kEpoch + limit + 1, kEpoch, true, policy) ==
           DailyResumeDecision::DISCARD);
    assert(daily_resume_decision(true, false, kEpoch + 60, kEpoch, false, policy) == DailyResumeDecision::DISCARD);
    assert(daily_resume_decision(true, true, kEpoch - 1, kEpoch, true, policy) == DailyResumeDecision::DISCARD);
    assert(daily_resume_decision(false, true, kEpoch + 60, kEpoch, true, policy) == DailyResumeDecision::DISCARD);
  }
  assert(daily_resume_decision(true, true, kEpoch + 125, kEpoch) == DailyResumeDecision::DISCARD);
  assert(daily_restart_gap_limit_ms(static_cast<DailyRestartPolicy>(255)) == kDailyMaximumGapMs);
  assert(ota_notification_current(1000, 121000));
  assert(!ota_notification_current(1000, 121001));
  static_assert(kDailyMaximumGapMs == 120000);
  static_assert(kDailyMaximumRestartGapMs == 300000);
}

void test_verified_125_second_boot_keeps_prefix_and_debt() {
  auto original = collect();
  original.missing_energy_uncertainty_ws = 289.578 * 3600.0;
  Saved saved(original);
  SegmentAccumulator restored;
  assert(saved.records.restore_daily(restored, context, QualityConfig{}));
  assert(restored.restart_policy == DailyRestartPolicy::STANDARD);
  restored.restart_policy = saved.ticket.recovery_policy(7, saved.records, true);
  const auto result = observe_snapshot(restored, sample(7325, 90000), QualityConfig{});
  assert(result.status == LearningStatus::COLLECTING && !result.has_record);
  assert(restored.start_epoch_s == original.start_epoch_s && restored.integrated_duration_s == 7325);
  assert(fabs(restored.missing_energy_uncertainty_ws / 3600.0 - 2025.689111111) < 0.000001);
  assert(restored.restart_policy == DailyRestartPolicy::STANDARD && !restored.restart_pending);
  // A subsequent ordinary scalar gap still has its original120s ceiling.
  auto missing = sample(7385, 150000);
  missing.invalid_reasons = INVALID_SOURCE_STALE;
  observe_snapshot(restored, missing, QualityConfig{}, true);
  missing = sample(7445, 210000);
  missing.invalid_reasons = INVALID_SOURCE_STALE;
  observe_snapshot(restored, missing, QualityConfig{}, true);
  const auto expired = observe_snapshot(restored, sample(7446, 211000), QualityConfig{}, true);
  assert(expired.status == LearningStatus::TIME_DISCONTINUITY && expired.cause == DailyCollectionCause::GAP_TOO_LONG);
  assert(restored.integrated_duration_s == 0);
}

void test_quality_debt_is_retained_while_snapshot_checks_still_reject_restart() {
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    auto original = collect();
    if (scenario == 0) original.missing_energy_uncertainty_ws = 2000.0 * 3600.0;
    Saved saved(original);
    SegmentAccumulator restored;
    assert(saved.records.restore_daily(restored, context, QualityConfig{}));
    restored.restart_policy = saved.ticket.recovery_policy(7, saved.records, true);
    auto next = sample(7325, 90000);
    if (scenario == 1) next.invalid_reasons = INVALID_BOILER_HEAT;
    if (scenario == 2) next.context_revision = 2;
    if (scenario == 3) next.epoch_s = original.last_epoch_s - 1;
    const auto result = observe_snapshot(restored, next, QualityConfig{});
    assert(!result.has_record);
    assert(restored.restart_policy == DailyRestartPolicy::STANDARD);
    if (scenario == 0) {
      assert(result.status == LearningStatus::COLLECTING && result.cause == DailyCollectionCause::NONE);
      assert(restored.start_epoch_s == original.start_epoch_s && restored.integrated_duration_s == 7325);
      assert(restored.missing_energy_uncertainty_ws == 2000.0 * 3600.0 + 125.0 * 50000.0);
      assert(restored.heat_integral == original.heat_integral);
    } else {
      assert(restored.integrated_duration_s == 0 && restored.missing_energy_uncertainty_ws == 0.0);
      assert(result.cause ==
             (scenario == 2 ? DailyCollectionCause::CONTEXT_CHANGED : DailyCollectionCause::RESTART_DISCARDED));
    }
  }
}

void test_repeated_restarts_keep_cumulative_debt_and_do_not_persist_permission() {
  auto state = collect();
  for (uint32_t attempt = 1; attempt <= 3; ++attempt) {
    Saved saved(state, QualityConfig{}, attempt);
    auto standard = state;
    state.restart_policy = DailyRestartPolicy::VERIFIED_SOFTWARE_RESTART;
    Saved marked(state, QualityConfig{}, attempt);
    assert(saved.records.slot.size == marked.records.slot.size);
    assert(memcmp(saved.records.slot.bytes, marked.records.slot.bytes, saved.records.slot.size) == 0);
    assert(saved.records.restore_daily(state, context, QualityConfig{}));
    assert(state.restart_policy == DailyRestartPolicy::STANDARD);
    state.restart_policy = saved.ticket.recovery_policy(attempt, saved.records, true);
    const uint32_t t = standard.last_epoch_s - kEpoch + 60;
    const auto result = observe_snapshot(state, sample(t, 30000), QualityConfig{});
    assert(result.status == LearningStatus::COLLECTING && result.cause == DailyCollectionCause::NONE);
    assert(fabs(state.missing_energy_uncertainty_ws - attempt * 3000000.0) < 0.000001);
    assert(state.start_epoch_s == kEpoch && state.integrated_duration_s == 7200 + attempt * 60);
    assert(state.heat_integral == standard.heat_integral);
    assert(!state.restart_pending && state.restart_policy == DailyRestartPolicy::STANDARD);
  }
}

void test_full_restart_path_obeys_own_ceiling_even_with_small_energy_bound() {
  QualityConfig q;
  q.max_abs_heat_w = 1000;
  const auto original = collect(q);
  for (uint32_t gap : {300U, 301U}) {
    Saved saved(original, q);
    SegmentAccumulator restored;
    assert(saved.records.restore_daily(restored, context, q));
    restored.restart_policy = saved.ticket.recovery_policy(7, saved.records, true);
    const auto result = observe_snapshot(restored, sample(7200 + gap, 90000), q);
    assert((result.status == LearningStatus::COLLECTING) == (gap == 300));
    assert(restored.integrated_duration_s == (gap == 300 ? 7500 : 0));
  }
}
}  // namespace

int main() {
  test_only_matching_verified_one_use_grant_selects_boot_policy();
  test_restart_ceiling_does_not_change_live_or_ota_lease_limits();
  test_verified_125_second_boot_keeps_prefix_and_debt();
  test_quality_debt_is_retained_while_snapshot_checks_still_reject_restart();
  test_repeated_restarts_keep_cumulative_debt_and_do_not_persist_permission();
  test_full_restart_path_obeys_own_ceiling_even_with_small_energy_bound();
}
