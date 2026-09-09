#include <assert.h>

#include <cstdint>

#include "../../openquatt/includes/control/oq_hp_restart_guard.h"

namespace {

constexpr uint32_t MINIMUM_OFF_MS = 240000;
constexpr uint32_t FRESHNESS_MS = 10000;

oq_hp_restart_guard::Policy make_policy() {
  oq_hp_restart_guard::Policy policy;
  assert(policy.configure(MINIMUM_OFF_MS, FRESHNESS_MS));
  return policy;
}

void test_requires_fresh_stopped_evidence() {
  auto policy = make_policy();
  assert(!policy.can_start(0));
  assert(policy.remaining_ms(0) == MINIMUM_OFF_MS);
  assert(policy.snapshot_credit_ms(0) == 0);

  assert(policy.restore_credit(MINIMUM_OFF_MS, true));
  assert(!policy.can_start(5000));
  assert(policy.remaining_ms(5000) == MINIMUM_OFF_MS);

  policy.observe_stopped(5000);
  assert(policy.can_start(5000));
}

void test_restored_credit_is_clamped_and_reboot_time_is_not_counted() {
  auto partial = make_policy();
  assert(partial.restore_credit(150000, true));
  assert(!partial.can_start(90000));
  partial.observe_stopped(90000);
  assert(partial.remaining_ms(90000) == 90000);
  for (uint32_t now_ms = 100000; now_ms <= 170000; now_ms += FRESHNESS_MS) partial.observe_stopped(now_ms);
  assert(!partial.can_start(179999));
  assert(partial.can_start(180000));

  auto full = make_policy();
  assert(full.restore_credit(MINIMUM_OFF_MS + 1, true));
  full.observe_stopped(1000);
  assert(full.remaining_ms(1000) == 0);
  assert(full.can_start(1000));

  auto invalid = make_policy();
  assert(invalid.restore_credit(MINIMUM_OFF_MS, false));
  invalid.observe_stopped(1000);
  assert(invalid.remaining_ms(1000) == MINIMUM_OFF_MS);
}

void test_repeated_stopped_observations_do_not_restart_timer() {
  auto policy = make_policy();
  policy.observe_stopped(1000);
  policy.observe_stopped(6000);
  policy.observe_stopped(11000);
  assert(policy.remaining_ms(11000) == 230000);
  assert(policy.remaining_ms(16000) == 225000);
  assert(policy.snapshot_credit_ms(16000) == 10000);
}

void test_gap_and_active_observation_invalidate_evidence() {
  auto gap = make_policy();
  assert(gap.restore_credit(150000, true));
  gap.observe_stopped(1000);
  assert(gap.remaining_ms(11000) == 80000);
  assert(!gap.can_start(11001));
  assert(gap.remaining_ms(11001) == MINIMUM_OFF_MS);
  assert(gap.snapshot_credit_ms(11001) == 0);
  gap.observe_stopped(11001);
  assert(gap.remaining_ms(11001) == MINIMUM_OFF_MS);
  assert(gap.snapshot_credit_ms(11001) == 0);

  auto active = make_policy();
  assert(active.restore_credit(MINIMUM_OFF_MS, true));
  active.observe_stopped(1000);
  assert(active.can_start(1000));
  active.invalidate();
  assert(!active.can_start(1000));
  assert(active.remaining_ms(1000) == MINIMUM_OFF_MS);
  assert(!active.restore_credit(MINIMUM_OFF_MS, true));
}

void test_policies_are_independent_per_heat_pump() {
  auto hp1 = make_policy();
  auto hp2 = make_policy();
  assert(hp1.restore_credit(MINIMUM_OFF_MS, true));
  assert(hp2.restore_credit(60000, true));
  hp1.observe_stopped(1000);
  hp2.observe_stopped(1000);
  assert(hp1.can_start(1000));
  assert(!hp2.can_start(1000));
  assert(hp2.remaining_ms(1000) == 180000);

  hp1.invalidate();
  assert(!hp1.can_start(1000));
  assert(hp2.remaining_ms(1000) == 180000);
}

void test_snapshot_only_contains_observed_safe_time_across_restarts() {
  auto first_boot = make_policy();
  assert(first_boot.restore_credit(150000, true));
  first_boot.observe_stopped(1000);
  assert(first_boot.snapshot_credit_ms(9000) == 150000);
  first_boot.observe_stopped(9000);
  assert(first_boot.snapshot_credit_ms(9000) == 158000);

  auto second_boot = make_policy();
  assert(second_boot.restore_credit(first_boot.snapshot_credit_ms(9000), true));
  assert(second_boot.snapshot_credit_ms(50000) == 0);
  second_boot.observe_stopped(50000);
  assert(second_boot.remaining_ms(50000) == 82000);
  assert(second_boot.snapshot_credit_ms(55000) == 158000);
  second_boot.observe_stopped(55000);
  assert(second_boot.snapshot_credit_ms(55000) == 163000);
}

void test_millis_zero_and_wraparound() {
  auto at_zero = make_policy();
  at_zero.observe_stopped(0);
  assert(!at_zero.can_start(0));
  at_zero.observe_stopped(10000);
  assert(at_zero.remaining_ms(10000) == 230000);

  oq_hp_restart_guard::Policy wrapping;
  assert(wrapping.configure(3000, 2000));
  constexpr uint32_t before_wrap = UINT32_MAX - 1000;
  wrapping.observe_stopped(before_wrap);
  wrapping.observe_stopped(500);
  assert(wrapping.remaining_ms(500) == 1499);
  assert(!wrapping.can_start(1998));
  assert(wrapping.can_start(1999));
}

void test_configuration_boundaries() {
  oq_hp_restart_guard::Policy policy;
  assert(policy.configure(0, FRESHNESS_MS));
  assert(!policy.can_start(0));
  assert(policy.remaining_ms(0) == 0);
  policy.observe_stopped(0);
  assert(policy.can_start(0));
  assert(policy.remaining_ms(0) == 0);

  assert(!policy.configure(MINIMUM_OFF_MS, 0));
  assert(!policy.restore_credit(MINIMUM_OFF_MS, true));
  assert(!policy.can_start(0));

  assert(!policy.configure(0x80000000U, FRESHNESS_MS));
  assert(!policy.can_start(0));
  assert(policy.remaining_ms(0) == UINT32_MAX);

  assert(!policy.configure(MINIMUM_OFF_MS, 0x80000000U));
  assert(!policy.can_start(0));
}

}  // namespace

int main() {
  test_requires_fresh_stopped_evidence();
  test_restored_credit_is_clamped_and_reboot_time_is_not_counted();
  test_repeated_stopped_observations_do_not_restart_timer();
  test_gap_and_active_observation_invalidate_evidence();
  test_policies_are_independent_per_heat_pump();
  test_snapshot_only_contains_observed_safe_time_across_restarts();
  test_millis_zero_and_wraparound();
  test_configuration_boundaries();
  return 0;
}
