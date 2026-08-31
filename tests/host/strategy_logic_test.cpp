#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_strategy_logic.h"

namespace {
using namespace oq_strategy;

LocalOutsideInput baseline() {
  LocalOutsideInput in;
  in.now_ms = 100000U;
  in.stale_ms = 60000U;
  in.dual = true;
  in.hp1_outside_c = 5.0f;
  in.hp2_outside_c = 7.0f;
  in.hp1_mode = 2.0f;
  in.hp2_mode = 2.0f;
  in.hp1_last_change_ms = 90000U;
  in.hp2_last_change_ms = 90000U;
  in.hp1_activity_ms = 95000U;
  in.hp2_activity_ms = 95000U;
  return in;
}

void test_active_and_idle_aggregation() {
  auto in = baseline();
  assert(aggregate_local_outside(in) == 5.0f);
  in.hp1_mode = 0.0f;
  assert(aggregate_local_outside(in) == 7.0f);
  in.hp2_mode = 0.0f;
  assert(aggregate_local_outside(in) == 6.0f);
  in.dual = false;
  assert(aggregate_local_outside(in) == 5.0f);
  in.hp1_outside_c = NAN;
  assert(isnan(aggregate_local_outside(in)));
}

void test_stale_running_sensor_and_rollover() {
  auto in = baseline();
  in.hp1_last_change_ms = 10000U;
  in.hp1_activity_ms = 95000U;
  assert(!trusted_local_outside(in.hp1_outside_c, in.now_ms, in.stale_ms, in.hp1_last_change_ms, in.hp1_activity_ms,
                                true));
  in.hp1_activity_ms = 5000U;
  assert(
      trusted_local_outside(in.hp1_outside_c, in.now_ms, in.stale_ms, in.hp1_last_change_ms, in.hp1_activity_ms, true));
  const uint32_t now = 2000U;
  const uint32_t changed = UINT32_MAX - 70000U;
  const uint32_t active = UINT32_MAX - 1000U;
  assert(!trusted_local_outside(5.0f, now, 60000U, changed, active, true));
  assert(!thermal_working_mode(NAN));
  assert(!thermal_working_mode(0.0f));
  assert(thermal_working_mode(1.0f));
  assert(thermal_working_mode(2.0f));
}
}  // namespace

int main() {
  test_active_and_idle_aggregation();
  test_stale_running_sensor_and_rollover();
  return 0;
}
