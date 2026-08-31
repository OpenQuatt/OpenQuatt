#include <assert.h>
#include <float.h>
#include "../../openquatt/includes/control/oq_cooling_demand_logic.h"
using namespace oq_cooling;
DemandInput active_input(uint32_t now_ms = 0) {
  DemandInput in;
  in.now_ms = now_ms, in.control_active = in.sensor_valid = true;
  in.guard_mode = GUARD_USER_RESPONSIBILITY;
  in.supply_c = 20, in.target_c = 19;
  in.demand_max = 4, in.kp = 3;
  return in;
}
void test_timing() {
  DemandState timing;
  assert(demand_loop_dt_s(0, timing) == 5 && timing.loop_seen && timing.last_loop_ms == 0);
  timing.last_loop_ms = UINT32_MAX - 1999U;
  assert(demand_loop_dt_s(2000U, timing) == 4);
  DemandTuning tuning;
  DemandState filter;
  filter.filter = {true, true, 0, 0, 0, UINT32_MAX - 29999U};
  assert(update_gap_filter(1, 5, 30000U, tuning, filter));
  assert(fabsf(filter.filter.filtered_gap_c - 1.0f / 13) < .00001f &&
         fabsf(filter.filter.rate_c_per_min - 1.0f / 13) < .00001f && filter.filter.rate_reference_ms == 30000U);
  struct Case {
    int previous, candidate, allowed, expected;
    bool seen;
    uint32_t since, now;
  };
  const Case cases[] = {{1, 4, 4, 1, true, 0, 89999},
                        {1, 4, 4, 2, true, 0, 90000},
                        {1, 4, 4, 1, true, UINT32_MAX - 44999U, 44999},
                        {1, 4, 4, 2, true, UINT32_MAX - 44999U, 45000},
                        {4, 2, 4, 4, true, UINT32_MAX - 14999U, 14999},
                        {4, 2, 4, 2, true, UINT32_MAX - 14999U, 15000},
                        {4, 2, 4, 2, false, 0, 123},
                        {4, 4, 2, 2, true, 100, 101},
                        {0, 4, 4, 1, false, 0, 0}};
  for (const auto& c : cases) {
    DemandState state;
    state.limited_demand = c.previous;
    state.demand_change_seen = c.seen;
    state.last_demand_change_ms = c.since;
    const int actual = apply_demand_dwell(c.candidate, c.allowed, c.now, tuning, state);
    assert(actual == c.expected && state.demand_change_seen == (c.seen || actual != c.previous));
    assert(state.last_demand_change_ms == (actual == c.previous ? c.since : c.now));
  }
}
void test_pi() {
  struct Cap {
    float room;
    int allowed, base, demand, reason;
  };
  const Cap caps[] = {{.399f, 1, 1, 1, REASON_ROOM_CAP},  {.4f, 2, 2, 1, REASON_ROOM_CAP},
                      {.799f, 2, 2, 1, REASON_ROOM_CAP},  {.8f, 3, 3, 1, REASON_ROOM_CAP},
                      {1.199f, 3, 3, 1, REASON_ROOM_CAP}, {1.2f, 4, 3, 1, REASON_FULL}};
  for (const auto& c : caps) {
    DemandState state;
    auto in = active_input();
    in.room_error_valid = true;
    in.room_error_c = c.room;
    update_demand(in, {}, state);
    assert(state.allowed_max == c.allowed && state.base_demand == c.base && state.limited_demand == c.demand &&
           state.limiter_reason_code == c.reason);
  }
  struct Guard {
    int previous;
    float kp;
    int expected;
  };
  for (const Guard c : {Guard{4, 2, 2}, {4, 6, 5}, {0, 2, 1}}) {
    DemandState state;
    state.guard_seen = state.water_cycle.active = state.demand_change_seen = true;
    state.last_guard_mode = GUARD_DEW;
    state.limited_demand = c.previous;
    state.last_demand_change_ms = 1;
    auto in = active_input(100);
    in.kp = c.kp;
    in.demand_max = 10;
    const auto out = update_demand(in, {}, state);
    assert(out.guard_changed && state.limited_demand == c.expected && state.demand_change_seen &&
           state.last_demand_change_ms == 100);
  }
  DemandState state;
  auto in = active_input();
  in.kp = 10;
  in.ki = 1;
  update_demand(in, {}, state);
  assert(state.base_demand == 4 && state.integral == 0);
  state.integral = 19;
  in.now_ms = 5000;
  in.supply_c = 18;
  update_demand(in, {}, state);
  assert(state.integral == 14);
}
void test_invalid() {
  struct Invalid {
    float supply, target, dew;
    bool dew_mode, valid;
  };
  const Invalid cases[] = {{NAN, 19, NAN, false, true},       {INFINITY, 19, NAN, false, true},
                           {20, -INFINITY, NAN, false, true}, {20, 19, INFINITY, true, true},
                           {20, 19, NAN, false, false},       {FLT_MAX, -FLT_MAX, NAN, false, true}};
  for (const auto& c : cases) {
    DemandState state;
    state.water_cycle.active = true;
    auto in = active_input();
    in.cycle_was_active = true;
    in.supply_c = c.supply;
    in.target_c = c.target;
    in.dew_point_c = c.dew;
    in.dew_mode = c.dew_mode;
    in.guard_mode = c.dew_mode ? GUARD_DEW : GUARD_USER_RESPONSIBILITY;
    in.sensor_valid = c.valid;
    assert(!update_demand(in, {}, state).control_active && state.limited_demand == 0 &&
           state.water_cycle.stop_reason_code == WATER_STOP_FALLBACK);
  }
  DemandState state;
  state.water_cycle.active = true;
  auto in = active_input();
  in.cycle_was_active = true;
  in.demand_max = in.kp = in.ki = in.kd = INFINITY;
  assert(!update_demand(in, {}, state).control_active && state.limited_demand == 0 && state.integral == 0);
  state.filter = {true, true, -.75f * FLT_MAX, 0, 0, 0};
  in = active_input(5000);
  in.supply_c = .75f * FLT_MAX;
  assert(!update_demand(in, {}, state).control_active && state.limited_demand == 0 &&
         isfinite(state.filter.filtered_gap_c));
  in = active_input();
  in.guard_mode = GUARD_NONE;
  assert(!update_demand(in, {}, state).control_active && state.limited_demand == 0);
  struct Arithmetic {
    float supply, last;
  };
  for (const Arithmetic c : {Arithmetic{0.5f * FLT_MAX, 0}, {0.75f * FLT_MAX, -0.75f * FLT_MAX}}) {
    DemandState arithmetic;
    arithmetic.last_error_c = c.last;
    in = active_input();
    in.supply_c = c.supply;
    in.target_c = 0;
    assert(!update_demand(in, {}, arithmetic).control_active && arithmetic.limited_demand == 0);
  }
  DemandState projected;
  projected.water_cycle = {false, 0, WATER_STOP_LIMITER};
  projected.filter = {true, true, 0, -0.5f * FLT_MAX, 0, 0};
  in = active_input(60000);
  in.restart_by_minimum_off_time = in.minimum_off_wait_active = true;
  const auto overflow_stop = update_demand(in, {}, projected);
  assert(!overflow_stop.control_active && overflow_stop.arm_minimum_off_stop && projected.limited_demand == 0);
}
void test_stops() {
  DemandState stopped;
  stopped.guard_seen = true;
  stopped.last_guard_mode = GUARD_DEW;
  stopped.water_cycle = {false, 0, WATER_STOP_LIMITER};
  auto in = active_input(1000);
  in.restart_by_minimum_off_time = in.minimum_off_wait_active = true;
  auto out = update_demand(in, {}, stopped);
  assert(out.guard_changed && out.arm_minimum_off_stop && !stopped.water_cycle.active &&
         stopped.limiter_reason_code == REASON_RESTART_WAIT);
  in.minimum_off_wait_active = false;
  in.minimum_off_stop_pending = true;
  update_demand(in, {}, stopped);
  assert(!stopped.water_cycle.active);
  in.minimum_off_stop_pending = false;
  update_demand(in, {}, stopped);
  assert(stopped.water_cycle.active && stopped.limited_demand == 1);
  in.guard_mode = GUARD_FALLBACK;
  in.fallback_mode = true;
  in.supply_c = in.target_c;
  in.cooling_hp_applied = true;
  out = update_demand(in, {}, stopped);
  assert(out.guard_changed && out.arm_minimum_off_stop && !stopped.water_cycle.active &&
         stopped.limiter_reason_code == REASON_FALLBACK_FLOOR);
  DemandState simmer;
  in = active_input();
  in.supply_c = 18.95f;
  in.kp = 0;
  update_demand(in, {}, simmer);
  assert(simmer.limited_demand == 1 && simmer.limiter_reason_code == REASON_SIMMER);
  DemandState oil;
  in = active_input(100);
  in.oil_return_active = true;
  update_demand(in, {}, oil);
  assert(oil.limited_demand == 1 && oil.limiter_reason_code == REASON_OIL_RETURN_HOLD);
  DemandState zero;
  zero.guard_seen = zero.water_cycle.active = zero.demand_change_seen = true;
  zero.last_guard_mode = GUARD_USER_RESPONSIBILITY;
  zero.limited_demand = 1;
  in = active_input(30000);
  in.kp = 0;
  in.restart_by_minimum_off_time = in.cooling_hp_applied = true;
  out = update_demand(in, {}, zero);
  assert(out.pi_zero_stop && out.arm_minimum_off_stop && !zero.water_cycle.active && zero.limited_demand == 0 &&
         zero.water_cycle.stop_reason_code == WATER_STOP_LIMITER && zero.limiter_reason_code == REASON_BUFFER_STOP);
}
int main() { return (test_timing(), test_pi(), test_invalid(), test_stops(), 0); }
