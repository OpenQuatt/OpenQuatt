#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_heat_intent_logic.h"

namespace {
using namespace oq_heat_intent;

Input input(uint32_t now_ms, float room_c, float setpoint_c) {
  return {now_ms, true, true, true, true, true, false, 1, room_c, setpoint_c, 0.1f, 0.2f, 0};
}

void test_thermostat_examples() {
  auto state = evaluate(input(1000, 20.0f, 18.0f), {}).next;
  auto out = evaluate(input(2000, 20.0f, 19.0f), state);
  assert(!out.active && !out.setpoint_raise_edge);

  state = evaluate(input(1000, 20.0f, 20.0f), {}).next;
  out = evaluate(input(2000, 20.0f, 20.5f), state);
  assert(out.active && out.setpoint_raise_edge && out.reason == SETPOINT_RAISE);

  state = evaluate(input(1000, 20.0f, 20.0f), {}).next;
  out = evaluate(input(2000, 20.0f, 20.1f), state);
  assert(!out.setpoint_raise_edge && out.active && out.reason == ROOM_DEMAND);

  state = evaluate(input(1000, 20.0f, 20.0f), {}).next;
  out = evaluate(input(2000, 20.0f, 20.5f), state);
  out = evaluate(input(3000, 20.0f, 20.0f), out.next);
  assert(!out.active && out.setpoint_raise_cancelled);

  state = evaluate(input(1000, 20.0f, 20.0f), {}).next;
  out = evaluate(input(2000, 20.0f, 20.5f), state);
  auto running = input(3000, 20.0f, 20.5f);
  running.compressor_active = true;
  out = evaluate(running, out.next);
  assert(out.active && out.reason == ROOM_RECOVERY && out.room_recovery_active && !out.next.setpoint_raise_active);
}

void test_source_freshness_and_enable_fail_closed() {
  auto state = evaluate(input(1000, 20.0f, 20.0f), {}).next;
  auto changed = input(2000, 20.0f, 20.5f);
  changed.setpoint_source = 2;
  auto out = evaluate(changed, state);
  assert(!out.setpoint_raise_edge && out.active && out.reason == ROOM_DEMAND);

  auto invalid = input(3000, 20.0f, 21.0f);
  invalid.setpoint_fresh = false;
  out = evaluate(invalid, out.next);
  assert(!out.active && !out.next.initialized);

  invalid = input(3000, 20.0f, 21.0f);
  invalid.heating_enabled = false;
  out = evaluate(invalid, state);
  assert(!out.active && !out.next.initialized);
}

void test_power_house_room_confirmation() {
  auto cold = input(1000, 19.0f, 20.0f);
  cold.room_confirm_ms = 10000;
  auto out = evaluate(cold, {});
  assert(!out.active && out.next.room_confirm_since_ms == 1000);
  cold.now_ms = 10999;
  out = evaluate(cold, out.next);
  assert(!out.active);
  cold.now_ms = 11000;
  out = evaluate(cold, out.next);
  assert(out.active && out.reason == ROOM_DEMAND);
  cold.room_c = 20.0f;
  cold.now_ms = 12000;
  out = evaluate(cold, out.next);
  assert(!out.active && out.next.room_confirm_since_ms == 0);

  cold = input(UINT32_MAX - 4999U, 19.0f, 20.0f);
  cold.room_confirm_ms = 10000;
  out = evaluate(cold, {});
  cold.now_ms = 5000U;
  out = evaluate(cold, out.next);
  assert(out.active && out.reason == ROOM_DEMAND);
}

void test_room_recovery_requires_a_real_room_demand_start_and_has_hysteresis() {
  auto cold = input(1000, 19.8f, 20.0f);
  cold.room_confirm_ms = 10000;
  auto out = evaluate(cold, {});
  cold.now_ms = 11000;
  out = evaluate(cold, out.next);
  assert(out.active && out.fast_start && out.reason == ROOM_DEMAND);

  cold.now_ms = 12000;
  cold.compressor_active = true;
  out = evaluate(cold, out.next);
  assert(out.active && !out.fast_start && out.room_recovery_active && out.reason == ROOM_RECOVERY);

  // The recovery holds across the original restart boundary (19.9 C), then
  // releases halfway through the 0.1 K restart band (19.95 C).
  cold.now_ms = 13000;
  cold.room_c = 19.92f;
  out = evaluate(cold, out.next);
  assert(out.room_recovery_active && out.reason == ROOM_RECOVERY);
  cold.now_ms = 14000;
  cold.room_c = 19.95f;
  out = evaluate(cold, out.next);
  assert(!out.room_recovery_active && !out.fast_start && out.reason == NONE);

  // A room sample may move above the restart boundary while the compressor is
  // starting. An already armed start must still recover until 19.95 C.
  auto start_edge = input(1000, 19.89f, 20.0f);
  out = evaluate(start_edge, {});
  assert(out.next.room_start_armed);
  start_edge.now_ms = 2000;
  start_edge.room_c = 19.92f;
  start_edge.compressor_active = true;
  out = evaluate(start_edge, out.next);
  assert(out.room_recovery_active && out.reason == ROOM_RECOVERY);

  // A setpoint reduction cancels an in-flight recovery rather than preserving
  // output against a lower comfort target.
  cold.now_ms = 15000;
  cold.room_c = 19.8f;
  cold.setpoint_c = 20.1f;
  cold.compressor_active = false;
  out = evaluate(cold, out.next);
  cold.now_ms = 25000;
  out = evaluate(cold, out.next);
  cold.compressor_active = true;
  cold.now_ms = 26000;
  out = evaluate(cold, out.next);
  assert(out.room_recovery_active);
  cold.setpoint_c = 20.0f;
  cold.now_ms = 27000;
  out = evaluate(cold, out.next);
  assert(!out.room_recovery_active && !out.next.room_start_armed);
}

void test_room_recovery_does_not_attach_to_an_existing_run_and_fails_closed() {
  auto cold = input(1000, 19.8f, 20.0f);
  cold.compressor_active = true;
  auto out = evaluate(cold, {});
  assert(out.active && !out.room_recovery_active && !out.next.room_start_armed);
  out = evaluate(cold, out.next);
  assert(!out.room_recovery_active && !out.next.room_start_armed);

  cold.compressor_active = false;
  out = evaluate(cold, {});
  cold.compressor_active = true;
  cold.now_ms = 2000;
  out = evaluate(cold, out.next);
  assert(out.room_recovery_active);

  cold.room_fresh = false;
  cold.now_ms = 3000;
  out = evaluate(cold, out.next);
  assert(!out.active && !out.room_recovery_active && !out.next.initialized);
}
}  // namespace

int main() {
  test_thermostat_examples();
  test_source_freshness_and_enable_fail_closed();
  test_power_house_room_confirmation();
  test_room_recovery_requires_a_real_room_demand_start_and_has_hysteresis();
  test_room_recovery_does_not_attach_to_an_existing_run_and_fails_closed();
  return 0;
}
