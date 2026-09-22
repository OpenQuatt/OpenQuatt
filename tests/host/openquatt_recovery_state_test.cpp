#include <cassert>
#include <cstdint>

#include "components/openquatt_recovery/RecoveryState.h"

using esphome::openquatt_recovery::RecoveryState;

static void test_button_thresholds() {
  RecoveryState state;
  assert(!state.tick(0, true, true).opened);
  assert(!state.tick(20000, true, true).opened);  // stuck during boot
  state.tick(20001, false, true);
  state.tick(20002, true, true);
  assert(!state.tick(25001, true, true).opened);  // 4999
  assert(state.tick(25002, true, true).opened);   // 5000
  const uint32_t generation = state.generation();
  assert(!state.tick(30001, true, true).wifi_reset_requested);  // 9999
  assert(state.tick(30002, true, true).wifi_reset_requested);   // 10000
  assert(!state.tick(40000, true, true).wifi_reset_requested);
  assert(state.generation() == generation);
  state.tick(40001, false, true);
  state.tick(40002, true, true);
  state.tick(44900, false, true);  // short release resets the hold
  state.tick(44901, true, true);
  assert(!state.tick(45002, true, true).opened);
}

static void test_timeout_and_jobs() {
  RecoveryState state;
  state.open(100);
  const uint32_t first = state.generation();
  assert(state.remaining_ms(100) == 600000);
  assert(state.active(600099));
  assert(!state.active(600100));
  assert(!state.begin_job(600100, first));
  assert(state.tick(600100, false, true).expired);
  state.open(700000);
  assert(!state.begin_job(700000, first));
  assert(state.begin_job(700000, state.generation()));
  assert(!state.begin_job(700000, state.generation()));  // UI + physical press
  assert(!state.end());
  assert(!state.open(700001));
  assert(!state.tick(1400000, false, true).expired);
  assert(state.active(1400000));  // keep guard while job is finishing/rebooting
  state.job_failed();
  assert(!state.active(1400000));
  state.open(1500000);
  assert(state.begin_job(1500000, state.generation()));
  state.job_failed();
  assert(state.begin_job(1500001, state.generation()));  // explicit retry after failure
}

static void test_ethernet_and_wrap() {
  RecoveryState ethernet;
  ethernet.tick(0, false, false);
  ethernet.tick(1, true, false);
  const auto event = ethernet.tick(10001, true, false);  // a delayed main-loop iteration
  assert(event.opened && !event.wifi_reset_requested);

  RecoveryState state;
  state.tick(UINT32_MAX - 1000, false, true);
  state.tick(UINT32_MAX - 999, true, true);
  assert(state.tick(4000, true, true).opened);  // exactly 5000 elapsed across wrap
  assert(state.tick(9000, true, true).wifi_reset_requested);
  state.open(UINT32_MAX - 999);
  assert(state.remaining_ms(0) == 599000);
  assert(!state.active(599000));
}

int main() {
  test_button_thresholds();
  test_timeout_and_jobs();
  test_ethernet_and_wrap();
}
