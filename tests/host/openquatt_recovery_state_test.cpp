#include <cassert>
#include <cstdint>
#include "components/openquatt_recovery/RecoveryState.h"
using esphome::openquatt_recovery::RecoveryState;
static RecoveryState::Events hold(uint32_t duration, bool wifi) {
  RecoveryState state;
  assert(!state.tick(0, true, wifi).opened);
  assert(!state.tick(20000, true, wifi).opened);
  state.tick(20001, false, wifi);
  state.tick(20002, true, wifi);
  assert(!state.tick(20002 + duration, true, wifi).opened);
  assert(state.generation() == 0);  // a held button never activates this process
  return state.tick(20002 + duration, false, wifi);
}
int main() {
  assert(!hold(4999, true).opened);
  assert(hold(5000, true).opened);
  assert(hold(9999, true).opened);
  auto wifi = hold(10000, true);
  assert(wifi.wifi_reset_requested && !wifi.opened);
  auto ethernet = hold(10000, false);
  assert(ethernet.opened && !ethernet.wifi_reset_requested);
  RecoveryState state;
  state.restore(100, 41);
  assert(state.generation() == 42 && state.remaining_ms(100) == 600000);
  assert(state.active(600099) && !state.active(600100));
  assert(state.tick(600100, false, true).expired);
  assert(!state.begin_job(600100, 42));
  RecoveryState busy;
  busy.restore(100, 0);
  assert(!busy.begin_job(100, 0));
  assert(busy.begin_job(100, 1));
  assert(!busy.begin_job(100, 1));
  assert(busy.active(700000) && !busy.tick(700000, false, true).expired);
  busy.job_failed();
  assert(busy.active(700000) && !busy.tick(700000, false, true).expired);
  assert(busy.begin_job(700001, 1));  // explicit retry, no automatic expiry after uncertain write
  RecoveryState wrap;
  wrap.tick(UINT32_MAX - 1000, false, true);
  wrap.tick(UINT32_MAX - 999, true, true);
  assert(!wrap.tick(4000, true, true).opened);
  assert(wrap.tick(4000, false, true).opened);
  wrap.restore(UINT32_MAX - 999, UINT32_MAX);
  assert(wrap.generation() == 0 && wrap.remaining_ms(0) == 599000);
  assert(!wrap.active(599000));
}
