#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "openquatt/includes/control/oq_boiler_dispatch_logic.h"
#include "openquatt/includes/control/oq_power_house_demand_logic.h"
#include "openquatt/includes/control/oq_power_house_dispatch_logic.h"
#include "openquatt/includes/control/oq_power_house_run_extension_logic.h"

#include "openquatt/includes/control/oq_compressor_frequency_runtime.h"
#include "tests/fixtures/hp_generation_select.h"

#define id(value) value
#include "openquatt/includes/performance/hp_perf_frequency.h"
#undef id

namespace {
using namespace oq_power_house_dispatch;
static_assert(sizeof(DispatchState) <= 40);
HpInput hp(int previous = 0, bool available = true) {
  HpInput result;
  result.candidate = {previous, available, false, false};
  for (int level = 0; level <= 10; ++level)
    result.levels[level] = {true, true, true, 1000.0f * level, 100.0f * level, static_cast<uint8_t>(level * 9)};
  return result;
}
DispatchInput input(bool duo = false) { return {60000, 3000.0f, duo, true, true, hp(), hp()}; }
DispatchTuning tuning() { return {9000.0f, 10000.0f, 5.0f, 150.0f, 450.0f, 1930.5f, 1}; }
void test_cold_weather_request_reaches_dispatch_and_deficit() {
  const oq_power_house::DemandInput demand_input{60000U, -15, -10, 16, 7020, 20, 20, NAN, 1, false};
  const oq_power_house::DemandTuning demand_tuning{0.5f, 3000, 0.1f, 0.3f, 10, 5, NAN};
  const auto demand = oq_power_house::decide_demand(demand_input, demand_tuning, {});
  for (bool duo : {false, true}) {
    auto in = input(duo);
    in.requested_w = demand.requested_w;
    assert(fabsf(in.requested_w - 8370.0f) < 0.01f);
    auto out = decide_dispatch(in, tuning(), {});
    assert(out.output_valid && out.expected_w > 7020.0f && out.deficit_w == 0.0f);
    // An actual electrical constraint still limits the available candidates.
    auto limits = tuning();
    limits.soft_limit_w = limits.peak_limit_w = 700.0f;
    out = decide_dispatch(in, limits, {});
    assert(out.output_valid && out.capacity_w == 7000.0f && fabsf(out.deficit_w - 1370.0f) < 0.01f);
    const float deficit = oq_power_house_run_extension::compute_house_deficit_w(in.requested_w, out.capacity_w,
                                                                                out.output_valid, out.deficit_w);
    assert(fabsf(deficit - 1370.0f) < 0.01f);
  }
}
void expect_safe(const DispatchDecision& out, const DispatchInput& in, const DispatchTuning& cfg) {
  assert(out.hp1_level >= 0 && out.hp1_level <= kMaxLevel && out.hp2_level >= 0 && out.hp2_level <= kMaxLevel);
  assert((in.duo || out.hp2_level == 0) && out.owner_hp == owner(out.hp1_level, out.hp2_level));
  if (in.requested_w == 0.0f) assert(out.hp1_level == 0 && out.hp2_level == 0);
  if (in.hp1.candidate.must_stop) assert(out.hp1_level == 0);
  if (in.hp2.candidate.must_stop) assert(out.hp2_level == 0);
  if (!in.hp1.candidate.minimum_off_ready && in.hp1.candidate.previous_applied_level <= 0) assert(out.hp1_level == 0);
  if (!in.hp2.candidate.minimum_off_ready && in.hp2.candidate.previous_applied_level <= 0) assert(out.hp2_level == 0);
  if (out.output_valid) assert(isfinite(out.expected_w));
  if (!out.output_valid || out.hp1_level + out.hp2_level == 0) return;
  float electrical_w = 0.0f;
  for (const auto* selected : {&in.hp1, &in.hp2}) {
    const int level = selected == &in.hp1 ? out.hp1_level : out.hp2_level;
    if (level <= 0) continue;
    const auto& estimate = selected->levels[level];
    assert(estimate.allowed && estimate.thermal_valid && estimate.electrical_valid);
    assert(isfinite(estimate.thermal_w) && estimate.thermal_w >= 0.0f && isfinite(estimate.electrical_w) &&
           estimate.electrical_w >= 0.0f);
    electrical_w += estimate.electrical_w;
  }
  assert(isfinite(cfg.peak_limit_w) && electrical_w <= cfg.peak_limit_w);
}
void test_actuator_hold_constrains_owner_swap() {
  for (bool hp1_running : {false, true}) {
    auto in = input(true);
    in.requested_w = 2000.0f;
    in.hp1.levels = in.hp2.levels = {};
    auto& running = hp1_running ? in.hp1 : in.hp2;
    auto& idle = hp1_running ? in.hp2 : in.hp1;
    running.candidate.previous_applied_level = 1;
    running.levels[1] = {true, true, true, 2500.0f, 400.0f, 30};
    idle.levels[1] = {true, true, true, 2000.0f, 200.0f, 30};
    const int running_owner = hp1_running ? 1 : 2;

    // Reproduce the restart report: the apparently better idle HP replaces
    // a running owner that the downstream actuator will refuse to stop.
    assert(decide_dispatch(in, tuning(), {}).owner_hp != running_owner);
    running.stop_inhibited = true;
    auto out = decide_dispatch(in, tuning(), {});
    assert(out.owner_hp == running_owner && out.expected_w == 2500.0f);
    assert(topology(out.hp1_level, out.hp2_level) == 1);
    expect_safe(out, in, tuning());

    // A real capacity deficit can still start the peer while the owner runs.
    in.requested_w = 4500.0f;
    out = decide_dispatch(in, tuning(), {});
    assert(out.hp1_level == 1 && out.hp2_level == 1 && out.expected_w == 4500.0f);
    expect_safe(out, in, tuning());

    // A safety stop overrides the hold; removing demand never starts a unit.
    in.requested_w = 2000.0f;
    running.candidate.must_stop = true;
    out = decide_dispatch(in, tuning(), {});
    assert(out.owner_hp != running_owner && topology(out.hp1_level, out.hp2_level) == 1);
    expect_safe(out, in, tuning());
    running.candidate.must_stop = false;
    in.requested_w = 0.0f;
    out = decide_dispatch(in, tuning(), {});
    assert(out.hp1_level == 0 && out.hp2_level == 0);

    // Expiry restores the existing optimizer without a new timer or latch.
    in.requested_w = 2000.0f;
    running.stop_inhibited = false;
    assert(decide_dispatch(in, tuning(), {}).owner_hp != running_owner);

    // A stale hold flag cannot turn a stopped HP into a mandatory start.
    running.candidate.previous_applied_level = 0;
    running.stop_inhibited = true;
    out = decide_dispatch(in, tuning(), {});
    assert(out.owner_hp != running_owner && topology(out.hp1_level, out.hp2_level) == 1);
  }
}
void test_feedback_hold_prefers_upshift_before_starting_peer() {
  for (bool hp1_running : {false, true}) {
    auto in = input(true);
    auto& running = hp1_running ? in.hp1 : in.hp2;
    auto& idle = hp1_running ? in.hp2 : in.hp1;
    auto limits = tuning();
    PendingSinglePower pending_single;
    const auto check = [&](int running_level, int idle_level, const DispatchState& state = {}) {
      const auto out = decide_dispatch(in, limits, state, nullptr, pending_single);
      assert(out.output_valid);
      assert(out.hp1_level == (hp1_running ? running_level : idle_level));
      assert(out.hp2_level == (hp1_running ? idle_level : running_level));
      expect_safe(out, in, limits);
      return out;
    };
    in.hp1.levels = in.hp2.levels = {};
    running.candidate.previous_applied_level = 2;
    running.stop_inhibited = true;
    running.feedback_hold_level = 2;
    running.levels[2] = {true, true, true, 2000.0f, 250.0f, 26};
    running.levels[4] = {true, true, true, 3000.0f, 450.0f, 48};
    idle.levels[1] = {true, true, true, 1000.0f, 250.0f, 20};

    in.requested_w = 2000.0f;
    check(2, 0);
    in.requested_w = 3000.0f;
    check(4, 0);
    idle.levels[1].thermal_w = 500.0f;
    const auto raised = check(4, 0);
    assert(raised.capacity_w == 3000.0f && raised.deficit_w == 0.0f && !raised.saturated);
    idle.levels[1].thermal_w = 1000.0f;

    // The current hold caps the modelled Duo output at 3 kW. A 3.6 kW
    // request still needs the peer; compare against the full request.
    in.requested_w = 3600.0f;
    const auto shortfall = check(2, 1);
    assert(shortfall.saturated && shortfall.deficit_w == 600.0f);

    in.requested_w = 3000.0f;
    running.levels[4].thermal_w = 6000.0f;
    check(2, 1);
    running.levels[4].thermal_w = 3000.0f;
    running.levels[4].allowed = false;
    check(2, 1);
    running.levels[4].allowed = true;
    running.levels[4].electrical_w = 10500.0f;
    check(2, 1);
    limits.soft_limit_w = 500.0f;
    running.levels[4].electrical_w = 600.0f;
    check(2, 1);
    limits = tuning();
    running.levels[4].electrical_w = 450.0f;
    running.fixed_level = 2;
    check(2, 1);
    running.fixed_level = 0;
    running.candidate.available_for_start = false;
    check(2, 1);
    running.candidate.available_for_start = true;

    const auto oil_return = observe_protection({}, {in.now_ms, false, false, true, 2, 0});
    check(2, 1, oil_return);

    // A fresh receipt can still report the old frequency just after an upshift.
    running.candidate.previous_applied_level = running.feedback_hold_level = 4;
    running.levels[4].thermal_w = 2000.0f;
    running.levels[5] = {true, true, true, 3100.0f, 475.0f, 55};
    pending_single = {3000.0f, 450.0f};
    const auto waiting = check(4, 0);
    assert(waiting.expected_w == 2000.0f);
    in.requested_w = 2500.0f;
    idle.levels[1].thermal_w = 500.0f;
    check(4, 1);
    idle.levels[1].thermal_w = 1000.0f;
    in.requested_w = 3600.0f;
    check(4, 1);
    in.requested_w = 3000.0f;
    pending_single = {};
    check(5, 0);

    // A requested owner swap also starts the idle peer. The actuator may
    // keep the first HP running, so try the upshift on the incumbent first.
    running.candidate.previous_applied_level = running.feedback_hold_level = 2;
    running.stop_inhibited = false;
    running.levels[4] = {true, true, true, 3000.0f, 450.0f, 48};
    idle.levels[1] = {true, true, true, 3000.0f, 200.0f, 20};
    check(4, 0);
    in.requested_w = 2100.0f;
    idle.levels[1].thermal_w = 2100.0f;
    check(2, 0);
    in.requested_w = 100.0f;
    idle.levels[1].thermal_w = 1000.0f;
    check(0, 1);
  }
}
void test_timing_and_matrix() {
  auto state = observe_protection({}, {UINT32_MAX - 4U, true, false, false, 3, 0});
  state = observe_protection(state, {UINT32_MAX - 2U, false, false, false, 3, 0});
  assert(tail_active(state.hp1_defrost, 6U, 10U) && !tail_active(state.hp1_defrost, 7U, 10U));
  const uint32_t expired_ms = UINT32_MAX - 2U + kDefrostHoldMs;
  state = observe_protection(state, {expired_ms, false, false, false, 3, 0});
  assert(!state.hp1_defrost.armed);
  state = observe_protection(state, {expired_ms - 1U, false, false, false, 3, 0});
  assert(!tail_active(state.hp1_defrost, expired_ms - 1U, kDefrostHoldMs));
  state = observe_protection({}, {10U, false, false, false, 3, 0});
  state = observe_protection(state, {20U, false, false, true, 3, 3});
  assert(topology_hold_active(state, 20U + kTopologyHoldMs - 1U, kTopologyHoldMs));
  state = observe_protection(state, {20U + kTopologyHoldMs, false, false, false, 3, 3});
  assert(!state.topology_hold_armed);
  state = observe_protection(state, {19U + kTopologyHoldMs, false, false, false, 3, 3});
  assert(!topology_hold_active(state, 19U + kTopologyHoldMs, kTopologyHoldMs));
  state = observe_protection({}, {20U, false, false, true, 3, 3});
  state = observe_protection(state, {21U, false, false, false, 3, 3});
  assert(tail_active(state.oil_return, 25U, 5U));
  for (bool duo : {false, true})
    for (float demand_w : {0.0f, 0.1f, 2162.7f, 2187.0f, 7000.0f})
      for (int previous : {-1, 0, 3, 11})
        for (int stops = 0; stops < 4; ++stops)
          for (float cap : {0.0f, 299.0f, 300.0f, 10000.0f}) {
            auto in = input(duo);
            in.requested_w = demand_w;
            in.hp1.candidate = {previous, true, (stops & 1) != 0, true};
            in.hp2.candidate = {previous, true, (stops & 2) != 0, true};
            auto cfg = tuning();
            cfg.soft_limit_w = cfg.peak_limit_w = cap;
            expect_safe(decide_dispatch(in, cfg, {}), in, cfg);
          }
}
void test_extension_uses_only_active_units() {
  for (bool first_running : {false, true}) {
    auto in = input(true);
    auto& running = first_running ? in.hp1 : in.hp2;
    auto& idle = first_running ? in.hp2 : in.hp1;
    running.candidate.previous_applied_level = 3;
    running.stop_inhibited = true;
    running.levels[1].allowed = running.levels[2].allowed = false;
    auto minimum = minimum_active_candidate(in, tuning());
    assert(minimum.valid && minimum.thermal_w == 3000);
    assert((first_running ? minimum.hp1_level : minimum.hp2_level) == 3);
    assert((first_running ? minimum.hp2_level : minimum.hp1_level) == 0);
    idle.candidate.minimum_off_ready = false;
    assert(minimum_active_candidate(in, tuning()).thermal_w == 3000);
    running.stop_inhibited = false;
    assert(minimum_active_candidate(in, tuning()).thermal_w == 3000);
    running.fixed_level = 4;
    assert(minimum_active_candidate(in, tuning()).thermal_w == 4000);
    running.candidate.must_stop = true;
    assert(!minimum_active_candidate(in, tuning()).valid);
  }
  auto in = input(true);
  assert(!minimum_active_candidate(in, tuning()).valid);
  in.hp1.candidate.previous_applied_level = in.hp2.candidate.previous_applied_level = 3;
  in.hp1.levels[1].allowed = in.hp1.levels[2].allowed = false;
  in.hp1.stop_inhibited = in.hp2.stop_inhibited = true;
  assert(minimum_active_candidate(in, tuning()).thermal_w == 4000);
  in.hp1.stop_inhibited = false;
  assert(minimum_active_candidate(in, tuning()).thermal_w == 1000);
  in.hp2.stop_inhibited = false;
  in.hp1.candidate.link_suspect = true;
  assert(minimum_active_candidate(in, tuning()).thermal_w == 4000);
  in.hp1.candidate.must_stop = true;
  assert(minimum_active_candidate(in, tuning()).thermal_w == 1000);
  in.hp1.candidate.must_stop = in.hp1.candidate.link_suspect = false;
  in.hp2.stop_inhibited = true;
  in.hp1.stop_inhibited = true;
  in.pair_power = [](const void*, int first, int second) -> std::array<float, 2> {
    return {1000.0f * first + 800.0f * second, 100.0f * (first + second)};
  };
  assert(minimum_active_candidate(in, tuning()).thermal_w == 3800);
  assert(!minimum_active_candidate(in, {200, 300}).valid);
}
void test_performance_supply_fallback() {
  using oq_power_house_dispatch::PerformanceSupplyRoute;
  using oq_power_house_dispatch::select_performance_supply;

  // The shared system supply stays authoritative whenever it is valid.
  auto selected = select_performance_supply({30.0f, 31.0f, 32.0f, true, false});
  assert(selected.valid && selected.route == PerformanceSupplyRoute::SYSTEM && selected.supply_c == 30.0f);

  // Issue #713: if only HP1 can serve, its own fresh outlet may be used for
  // Power House performance estimation without changing the shared safety signal.
  selected = select_performance_supply({NAN, 31.0f, NAN, true, false});
  assert(selected.valid && selected.route == PerformanceSupplyRoute::HP1_OUTLET && selected.supply_c == 31.0f);

  // Mirror path for the one remaining HP2 candidate.
  selected = select_performance_supply({NAN, NAN, 32.0f, false, true});
  assert(selected.valid && selected.route == PerformanceSupplyRoute::HP2_OUTLET && selected.supply_c == 32.0f);

  // Never substitute an outlet when both HPs are serveable; normal Duo
  // optimization requires the shared system-supply measurement.
  selected = select_performance_supply({NAN, 31.0f, 32.0f, true, true});
  assert(!selected.valid && selected.route == PerformanceSupplyRoute::NONE && isnan(selected.supply_c));

  // Missing/stale/non-finite outlet data remains fail-closed for the model.
  assert(!select_performance_supply({NAN, NAN, NAN, true, false}).valid);
  assert(!select_performance_supply({NAN, 31.0f, 32.0f, false, false}).valid);
}

void test_single_and_failures() {
  auto cfg = tuning();
  auto in = input(true);
  in.performance_valid = false;
  in.hp1.candidate.previous_applied_level = 4;
  in.hp2.candidate.previous_applied_level = 3;
  auto out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level == 4 && out.hp2_level == 3 && out.reason == Reason::FALLBACK_DUO && !out.output_valid);
  assert(isnan(out.capacity_w) && isnan(out.deficit_w) && !out.saturated);
  in.hp2.candidate.previous_applied_level = 0;
  out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level == 4 && out.hp2_level == 0 && out.reason == Reason::FALLBACK_HP1);
  in.hp1.candidate.must_stop = true;
  assert(decide_dispatch(in, cfg, {}).reason == Reason::FALLBACK_IDLE);
  in.hp2.candidate = {};
  for (int previous : {-1, 21, 99}) {
    in.hp1.candidate = {previous, true, false, false};
    assert(decide_dispatch(in, cfg, {}).reason == Reason::FALLBACK_IDLE);
  }
  in.requested_w = NAN;
  in.performance_valid = true;
  out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level == 0 && out.hp2_level == 0 && !out.output_valid);
  in.requested_w = 0.0f;
  in.performance_valid = false;
  assert(!decide_dispatch(in, cfg, {}).output_valid);
  in = input(true);
  in.hp1.candidate = {21, false, false, true};
  out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level == 0 && out.hp2_level > 0);
  in = input(true);
  in.hp1.candidate.minimum_off_ready = false;
  out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level == 0 && out.hp2_level > 0);
  in.hp2.candidate.minimum_off_ready = false;
  out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level == 0 && out.hp2_level == 0 && out.reason == Reason::NO_CANDIDATE);
  cfg = tuning();
  in = input();
  assert(decide_dispatch(in, cfg, {}).hp1_level == 3);
  in.requested_w = 2500;
  in.hp1.candidate.previous_applied_level = 3;
  assert(decide_dispatch(in, cfg, {}).hp1_level == 3);
  in = input();
  cfg.soft_limit_w = cfg.peak_limit_w = 300;
  assert(decide_dispatch(in, cfg, {}).hp1_level == 3);
  cfg.soft_limit_w = cfg.peak_limit_w = 299;
  assert(decide_dispatch(in, cfg, {}).hp1_level <= 2);
  const float bad[] = {NAN, INFINITY, -INFINITY, -1.0f};
  for (float value : bad)
    for (int fault = 0; fault < 5; ++fault) {
      in = input();
      cfg = tuning();
      if (fault == 0) in.requested_w = value;
      if (fault == 1) cfg.soft_limit_w = value;
      if (fault == 2) cfg.peak_limit_w = value;
      for (int level = 1; level <= kMaxLevel && fault >= 3; ++level)
        (fault == 3 ? in.hp1.levels[level].thermal_w : in.hp1.levels[level].electrical_w) = value;
      const auto failed = decide_dispatch(in, cfg, {});
      expect_safe(failed, in, cfg);
      assert(failed.hp1_level == 0 && failed.hp2_level == 0 && !failed.output_valid);
    }
  in = input();
  for (int level = 4; level <= kMaxLevel; ++level) in.hp1.levels[level].electrical_valid = false;
  out = decide_dispatch(in, tuning(), {});
  assert(out.hp1_level == 3 && out.capacity_w == 3000 && out.output_valid);
  in = input(true);
  cfg = tuning();
  cfg.soft_limit_w = cfg.peak_limit_w = FLT_MAX;
  in.hp1.levels[1].electrical_w = in.hp2.levels[1].electrical_w = FLT_MAX;
  assert(!make_candidate(in, cfg, 1, 1, 1000.0f).valid);
  for (auto* selected : {&in.hp1, &in.hp2})
    for (int level = 1; level <= kMaxLevel; ++level) selected->levels[level].thermal_w = FLT_MAX;
  out = decide_dispatch(in, cfg, {});
  assert(isfinite(out.capacity_w));
}
constexpr uint32_t kMinuteMs = 60000U;
constexpr oq_power_house::DemandTuning kHouseDemandTuning{0.5f, 1000.0f, 0.1f, 0.3f, 8.0f, 3.0f, NAN};

DispatchDecision apply_dispatch_tick(DispatchInput& in, const DispatchTuning& cfg, DispatchState& state) {
  state = observe_protection(state, {in.now_ms, in.hp1.valve_defrost, in.hp2.valve_defrost, false,
                                     in.hp1.candidate.previous_applied_level, in.hp2.candidate.previous_applied_level});
  const auto out = decide_dispatch(in, cfg, state);
  expect_safe(out, in, cfg);
  assert(out.output_valid);
  // Normal operation: the requested levels are applied before the next tick.
  in.hp1.candidate.previous_applied_level = out.hp1_level;
  in.hp2.candidate.previous_applied_level = out.hp2_level;
  return out;
}

DispatchDecision apply_dispatch_tick(DispatchInput& in, const DispatchTuning& cfg, DispatchState& state,
                                     int requested_w) {
  in.requested_w = static_cast<float>(requested_w);
  return apply_dispatch_tick(in, cfg, state);
}

void test_duo_daily_demand_profile() {
  struct DemandPeriod {
    int minutes;
    int requested_w;
    int active_pumps;
  };
  // One pump can deliver 10 kW in this fixture.
  const DemandPeriod day[] = {
      // Overnight standby and morning warm-up.
      {360, 0, 0},
      {30, 3000, 1},
      {30, 6000, 1},
      {20, 10000, 1},
      // Fluctuations while heating with one pump.
      {4, 10475, 1},
      {4, 10700, 1},
      {4, 10550, 1},
      // Both pumps cover the higher demand.
      {30, 11000, 2},
      {180, 14000, 2},
      {60, 11000, 2},
      // The same fluctuations while heating with two pumps.
      {4, 10475, 2},
      {4, 10700, 2},
      {4, 10550, 2},
      // One pump covers the declining demand before standby.
      {120, 9000, 1},
      {180, 4000, 1},
      {120, 2000, 1},
      {286, 0, 0},
  };
  auto in = input(true);
  in.now_ms = 0;
  const auto cfg = tuning();
  DispatchState state;
  for (const auto& period : day) {
    for (int minute = 0; minute < period.minutes; ++minute) {
      in.now_ms += kMinuteMs;
      const auto out = apply_dispatch_tick(in, cfg, state, period.requested_w);
      assert(topology(out.hp1_level, out.hp2_level) == period.active_pumps);
      assert(fabsf(out.expected_w - in.requested_w) <= 1000.0f);
    }
  }
}

void test_runtime_lead_changes_between_heating_cycles() {
  const auto cfg = tuning();
  for (bool hp1_starts_first : {true, false}) {
    auto in = input(true);
    DispatchState state;
    in.lead_hp1 = hp1_starts_first;
    const int first_owner = hp1_starts_first ? 1 : 2;
    const int next_owner = hp1_starts_first ? 2 : 1;
    const auto first_cycle = apply_dispatch_tick(in, cfg, state, 3000);
    assert(first_cycle.owner_hp == first_owner);

    // The other pump is now preferred by runtime, while this cycle is still active.
    in.now_ms += 30 * kMinuteMs;
    in.lead_hp1 = !hp1_starts_first;
    const auto running = apply_dispatch_tick(in, cfg, state, 4000);
    assert(running.owner_hp == first_owner && running.expected_w == 4000.0f);

    in.now_ms += 30 * kMinuteMs;
    const auto standby = apply_dispatch_tick(in, cfg, state, 0);
    assert(standby.expected_w == 0.0f);
    in.now_ms += 30 * kMinuteMs;
    const auto next_cycle = apply_dispatch_tick(in, cfg, state, 3000);
    assert(next_cycle.owner_hp == next_owner);
  }
}

void test_duo_waits_for_minimum_off_after_demand_rebound() {
  constexpr uint32_t minimum_off_ms = 4 * kMinuteMs;
  auto in = input(true);
  const auto cfg = tuning();
  DispatchState state;
  const auto both = apply_dispatch_tick(in, cfg, state, 14000);
  assert(both.hp1_level > 0 && both.hp2_level > 0);

  in.now_ms += 20 * kMinuteMs;
  const auto single = apply_dispatch_tick(in, cfg, state, 9000);
  assert(single.owner_hp == 1);
  const uint32_t hp2_stopped_ms = in.now_ms;

  // The demand rises again before HP2 has been off long enough to restart.
  in.now_ms = hp2_stopped_ms + kMinuteMs;
  in.hp2.candidate.minimum_off_ready = oq_hp_candidate::minimum_off_ready(in.now_ms, hp2_stopped_ms, minimum_off_ms,
                                                                          in.hp2.candidate.previous_applied_level);
  const auto waiting = apply_dispatch_tick(in, cfg, state, 14000);
  assert(waiting.owner_hp == 1 && waiting.expected_w == 10000.0f);

  in.now_ms = hp2_stopped_ms + 5 * kMinuteMs;
  in.hp2.candidate.minimum_off_ready = oq_hp_candidate::minimum_off_ready(in.now_ms, hp2_stopped_ms, minimum_off_ms,
                                                                          in.hp2.candidate.previous_applied_level);
  const auto resumed = apply_dispatch_tick(in, cfg, state, 14000);
  assert(resumed.hp1_level > 0 && resumed.hp2_level > 0 && resumed.expected_w == 14000.0f);
}

void test_duo_heat_deficit_under_electrical_limit() {
  auto in = input(true);
  auto cfg = tuning();
  DispatchState state;
  const auto unrestricted = apply_dispatch_tick(in, cfg, state, 14000);
  assert(unrestricted.expected_w == 14000.0f);
  assert(unrestricted.deficit_w == 0.0f && !unrestricted.saturated);

  in.now_ms += 30 * kMinuteMs;
  cfg.soft_limit_w = cfg.peak_limit_w = 1200.0f;
  const auto limited = apply_dispatch_tick(in, cfg, state, 14000);
  assert(limited.capacity_w == 12000.0f && limited.expected_w == 12000.0f);
  assert(limited.deficit_w == 2000.0f && limited.saturated);

  in.now_ms += 30 * kMinuteMs;
  const auto lower_demand = apply_dispatch_tick(in, cfg, state, 12000);
  assert(lower_demand.capacity_w == 12000.0f && lower_demand.expected_w == 12000.0f);
  assert(lower_demand.deficit_w == 0.0f && !lower_demand.saturated);
}

void test_weather_drives_single_and_duo_dispatch() {
  struct WeatherStep {
    float outside_c;
    float requested_w;
    float single_deficit_w;
    int duo_active_pumps;
  };
  const WeatherStep weather[] = {
      {15.0f, 0.0f, 0.0f, 0},   {5.0f, 6000.0f, 0.0f, 1}, {-5.0f, 12000.0f, 2000.0f, 2},
      {0.0f, 9000.0f, 0.0f, 1}, {15.0f, 0.0f, 0.0f, 0},
  };
  const auto cfg = tuning();
  for (bool duo : {false, true}) {
    auto in = input(duo);
    DispatchState dispatch_state;
    oq_power_house::DemandState demand_state;
    oq_power_house::DemandInput demand_input{0, 15.0f, -10.0f, 15.0f, 15000.0f, 20.0f, 20.0f, NAN, 1.0f, false};
    for (const auto& step : weather) {
      // Leave enough simulated time between readings for the demand to settle.
      demand_input.now_ms += 6 * 60 * kMinuteMs;
      demand_input.outside_c = step.outside_c;
      const auto demand = oq_power_house::decide_demand(demand_input, kHouseDemandTuning, demand_state);
      assert(demand.valid && fabsf(demand.requested_w - step.requested_w) < 1.0f);
      demand_state = demand.next;
      in.now_ms = demand_input.now_ms;
      in.requested_w = demand.requested_w;

      const float deficit_w = duo ? 0.0f : step.single_deficit_w;
      const auto out = apply_dispatch_tick(in, cfg, dispatch_state);
      assert(out.capacity_w == (duo ? 20000.0f : 10000.0f));
      assert(fabsf(out.deficit_w - deficit_w) < 1.0f && out.saturated == (deficit_w > 0.0f));
      assert(fabsf(out.expected_w - (step.requested_w - deficit_w)) < 1.0f);
      if (duo) assert(topology(out.hp1_level, out.hp2_level) == step.duo_active_pumps);
    }
  }
}

void test_duo_recovers_after_water_temperature_limit() {
  struct WaterLimitStep {
    int minutes;
    float water_limit_factor;
    float min_requested_w;
    float max_requested_w;
    int active_pumps;
  };
  const WaterLimitStep heating[] = {
      {1, 1.0f, 11999.0f, 12001.0f, 2},
      {1, 0.5f, 5999.0f, 6001.0f, 1},
      // Heat demand ramps back up after the limit clears.
      {1, 1.0f, 6000.0f, 10000.0f, 1},
      {10, 1.0f, 11999.0f, 12001.0f, 2},
  };
  auto in = input(true);
  const auto cfg = tuning();
  DispatchState dispatch_state;
  oq_power_house::DemandState demand_state;
  oq_power_house::DemandInput demand_input{0, -5.0f, -10.0f, 15.0f, 15000.0f, 20.0f, 20.0f, NAN, 1.0f, false};
  for (const auto& step : heating) {
    demand_input.now_ms += step.minutes * kMinuteMs;
    demand_input.water_limit_factor = step.water_limit_factor;
    const auto demand = oq_power_house::decide_demand(demand_input, kHouseDemandTuning, demand_state);
    assert(demand.valid && demand.requested_w > step.min_requested_w && demand.requested_w < step.max_requested_w);
    demand_state = demand.next;
    in.now_ms = demand_input.now_ms;
    in.requested_w = demand.requested_w;

    const auto out = apply_dispatch_tick(in, cfg, dispatch_state);
    assert(topology(out.hp1_level, out.hp2_level) == step.active_pumps);
    assert(fabsf(out.expected_w - demand.requested_w) <= 1000.0f);
  }
}

void test_duo_switches_for_meaningful_power_savings() {
  struct LoadCase {
    int applied_level;
    int active_pumps;
    float electrical_w;
    Reason reason;
  };
  const LoadCase cases[] = {
      {5, 1, 660.0f, Reason::HOLD_ACTIVE},
      {6, 2, 680.0f, Reason::LESS_POWER},
  };
  const auto cfg = tuning();
  for (const auto& load : cases) {
    auto in = input(true);
    in.hp1.candidate.previous_applied_level = load.applied_level;
    // Fixed running power plus a rising cost at higher compressor levels.
    // Splitting the same load saves 80 W at 5 kW and 200 W at 6 kW.
    for (auto* pump : {&in.hp1, &in.hp2})
      for (int level = 1; level <= kMaxLevel; ++level)
        pump->levels[level].electrical_w = 160.0f + 20.0f * level * level;

    DispatchState state;
    const auto out = apply_dispatch_tick(in, cfg, state, load.applied_level * 1000);
    assert(out.expected_w == in.requested_w && !out.saturated);
    assert(topology(out.hp1_level, out.hp2_level) == load.active_pumps && out.reason == load.reason);
    assert(in.hp1.levels[out.hp1_level].electrical_w + in.hp2.levels[out.hp2_level].electrical_w == load.electrical_w);
  }
}

void test_defrost_holds_single_owner_until_recovery_tail_ends() {
  auto in = input(true);
  const auto cfg = tuning();
  DispatchState state;
  constexpr int requested_w = 4000;
  for (int level = 1; level <= kMaxLevel; ++level) {
    in.hp1.levels[level].electrical_w = 350.0f * level;
    in.hp2.levels[level].electrical_w = 300.0f * level;
  }
  // HP1 starts heating while HP2 is still in its minimum off-time.
  in.hp2.candidate.minimum_off_ready = false;
  const auto heating = apply_dispatch_tick(in, cfg, state, requested_w);
  assert(heating.owner_hp == 1 && heating.expected_w == requested_w);

  in.now_ms += 5 * kMinuteMs;
  in.hp2.candidate.minimum_off_ready = true;
  in.hp1.defrost = in.hp1.valve_defrost = true;
  const auto heating_levels = in.hp1.levels;
  for (auto& level : in.hp1.levels) level.thermal_w *= 0.55f;
  const auto defrosting = apply_dispatch_tick(in, cfg, state, requested_w);
  assert(defrosting.owner_hp == 1 && defrosting.reason == Reason::DEFROST_HOLD);

  in.now_ms += 5 * kMinuteMs;
  in.hp1.defrost = in.hp1.valve_defrost = false;
  in.hp1.levels = heating_levels;
  const auto defrost_ended = apply_dispatch_tick(in, cfg, state, requested_w);
  assert(defrost_ended.owner_hp == 1 && defrost_ended.reason == Reason::DEFROST_HOLD);

  in.now_ms += kMinuteMs;
  const auto recovering = apply_dispatch_tick(in, cfg, state, requested_w);
  assert(recovering.owner_hp == 1 && recovering.reason == Reason::DEFROST_HOLD);

  in.now_ms += 4 * kMinuteMs;
  const auto released = apply_dispatch_tick(in, cfg, state, requested_w);
  assert(released.owner_hp == 2 && released.reason == Reason::LESS_POWER && released.expected_w == requested_w);
}

void test_mild_weather_run_extension_drives_compressor_requests() {
  namespace extension = oq_power_house_run_extension;
  struct Step {
    float room_c;
    float house_requested_w;
    extension::Phase phase;
    float expected_w;
  };
  const Step steps[] = {
      {20.8f, 2000.0f, extension::Phase::RUNNING, 2000.0f},
      {21.1f, 200.0f, extension::Phase::EXTENDING, 1000.0f},
      {21.6f, 500.0f, extension::Phase::COMFORT_STOP, 0.0f},
      {21.4f, 500.0f, extension::Phase::WAIT_WARM_RESTART, 0.0f},
      {21.4f, 500.0f, extension::Phase::WAIT_WARM_RESTART, 0.0f},
      {21.2f, 500.0f, extension::Phase::WARM_RESTART, 1000.0f},
  };
  auto in = input(true);
  in.hp1.candidate.previous_applied_level = 2;
  const auto cfg = tuning();
  DispatchState dispatch_state;
  extension::State extension_state;
  for (const auto& step : steps) {
    in.now_ms += 10 * kMinuteMs;
    const bool heating = in.hp1.candidate.previous_applied_level > 0;
    const extension::Input extension_input{
        heating, heating, true, true, step.room_c, 21.0f, step.house_requested_w, 1000.0f, 1, 1.0f, true,
    };
    const auto decision = extension::evaluate(extension_input, {}, extension_state);
    extension_state = decision.next;
    assert(extension_state.phase == step.phase);
    in.requested_w = extension::apply_to_request(decision, step.house_requested_w);
    const auto out = apply_dispatch_tick(in, cfg, dispatch_state);
    assert(out.expected_w == step.expected_w && out.hp2_level == 0);
    assert((out.hp1_level > 0) == (step.expected_w > 0.0f));
    assert(extension::compute_house_deficit_w(step.house_requested_w, out.capacity_w, out.output_valid,
                                              out.deficit_w) == 0.0f);
  }
}

void test_cold_weather_boiler_assist_tracks_remaining_demand() {
  auto in = input(false);
  const auto cfg = tuning();
  DispatchState state;
  oq_boiler_dispatch::Inputs boiler;
  boiler.rated_boiler_power_w = 10000.0f;
  boiler.strategy = boiler.strategy_output_source = 3;
  boiler.maximum_water_temperature_c = 60.0f;
  boiler.boiler_inlet_c = 30.0f;
  boiler.flow_lph = 720.0f;
  boiler.heat_request = true;

  for (const int requested_w : {9000, 14000, 9000}) {
    in.now_ms += 30 * kMinuteMs;
    const auto hp = apply_dispatch_tick(in, cfg, state, requested_w);
    const float house_deficit_w = oq_power_house_run_extension::compute_house_deficit_w(in.requested_w, hp.capacity_w,
                                                                                        hp.output_valid, hp.deficit_w);
    const auto assist = oq_boiler::power_house_assist(house_deficit_w, 3000.0f, 1000.0f);
    const bool needs_boiler = requested_w == 14000;
    assert(assist.need_on == needs_boiler && assist.okay_off != needs_boiler);
    boiler.now_ms = boiler.strategy_updated_ms = in.now_ms;
    boiler.strategy_output_valid = hp.output_valid;
    boiler.strategy_requested_power_w = in.requested_w;
    boiler.hp_expected_power_w = hp.expected_w;
    if (boiler.control_mode == 3 && assist.okay_off) {
      // The boiler demand ends even while the supervisor still holds CM3.
      const auto recovering = oq_boiler_dispatch::dispatch(boiler);
      assert(recovering.valid && !recovering.heat_request && recovering.requested_power_w == 0.0f);
    }
    // Observe the boiler after the supervisor has confirmed promotion or demotion.
    boiler.control_mode = assist.need_on ? 3 : 2;
    const auto command = oq_boiler_dispatch::dispatch(boiler);
    assert(command.valid && command.heat_request == needs_boiler);
    if (needs_boiler) {
      assert(command.source == oq_boiler::COMMAND_SOURCE_POWER_HOUSE);
      assert(command.requested_power_w == 4000.0f && command.target_temperature_c > boiler.boiler_inlet_c);
    }
  }
}

void test_duo_holds_and_boost() {
  auto in = input(true);
  auto cfg = tuning();
  in.lead_hp1 = false;
  auto out = decide_dispatch(in, cfg, {});
  assert(out.hp2_level == 3 && out.reason == Reason::RUNTIME_LEAD);
  in.hp2.candidate.available_for_start = false;
  assert(decide_dispatch(in, cfg, {}).hp1_level > 0);

  // Exact #713 dispatch boundary: once performance input is valid, an HP2
  // must-stop condition must still leave HP1 as a valid single-HP candidate.
  in = input(true);
  in.hp2.candidate.must_stop = true;
  out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level > 0 && out.hp2_level == 0 && out.owner_hp == 1);
  in = input(true);
  in.hp1.candidate.previous_applied_level = 3;
  for (int level = 1; level <= kMaxLevel; ++level) {
    in.hp1.levels[level].electrical_w = 500.0f * level;
    in.hp2.levels[level].electrical_w = 100.0f * level;
  }
  out = decide_dispatch(in, cfg, {});
  assert(out.hp2_level == 3 && out.reason == Reason::LESS_POWER);
  out = decide_dispatch(in, cfg, observe_protection({}, {10, true, false, false, 3, 0}));
  assert(out.hp1_level == 3 && out.reason == Reason::DEFROST_HOLD);
  out = decide_dispatch(in, cfg, observe_protection({}, {10, false, false, true, 3, 0}));
  assert(out.hp1_level == 3 && out.reason == Reason::OIL_RETURN_HOLD);
  in.hp1.candidate.link_suspect = true;
  out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level == 3 && out.hp2_level == 0 && out.reason == Reason::KEEP_CURRENT);
  in.hp1.candidate.must_stop = true;
  expect_safe(decide_dispatch(in, cfg, {}), in, cfg);
  in = input(true);
  cfg = tuning();
  in.hp2.candidate.available_for_start = false;
  for (int level = 1; level <= kMaxLevel; ++level) in.hp1.levels[level].allowed = false;
  in.hp1.levels[1] = {true, true, true, 1000, 300};
  in.hp1.levels[2] = {true, true, true, 1140, 200};
  in.hp1.levels[3] = {true, true, true, 1280, 100};
  assert(pick_topology(in, cfg, 1000, 1, 150).hp1_level == 2);
  in = input(true);
  in.hp1.defrost = true;
  assert(decide_dispatch(in, cfg, {}).owner_hp == 1);
  cfg = tuning();
  cfg.soft_limit_w = cfg.peak_limit_w = 1000;
  in = input(true);
  in.requested_w = 7000;
  in.hp1.valve_defrost = true;
  for (auto* selected : {&in.hp1, &in.hp2})
    for (int level = 1; level <= kMaxLevel; ++level)
      selected->levels[level] =
          level <= 3 ? LevelEstimate{true, true, true, 2000.0f * level, level == 3 ? 500.0f : 50.0f * (level + 1)}
                     : LevelEstimate{};
  out = decide_dispatch(in, cfg, {});
  assert(out.reason == Reason::DEFROST_BOOST && out.hp1_level + out.hp2_level == 4);
  expect_safe(out, in, cfg);
  cfg.defrost_boost_min_w = in.requested_w;
  assert(decide_dispatch(in, cfg, {}).reason == Reason::DEFROST_BOOST);
  cfg.defrost_boost_min_w = std::nextafter(in.requested_w, INFINITY);
  assert(decide_dispatch(in, cfg, {}).reason != Reason::DEFROST_BOOST);
  cfg.defrost_boost_min_w = NAN;
  assert(decide_dispatch(in, cfg, {}).reason != Reason::DEFROST_BOOST);
  cfg.defrost_boost_min_w = tuning().defrost_boost_min_w;
  cfg.soft_limit_w = cfg.peak_limit_w = 550;
  out = decide_dispatch(in, cfg, {});
  assert(out.reason != Reason::DEFROST_BOOST && out.hp1_level + out.hp2_level == 3);
  assert(out.capacity_w == 8000.0f);
  expect_safe(out, in, cfg);
  assert(strcmp(request_reason_name(0), "ph_idle") == 0);
  assert(strcmp(request_reason_name(14), "ph_single_topology") == 0);
  assert(strcmp(request_reason_name(15), "oil_return_hold") == 0);
  assert(strcmp(request_reason_name(16), "ph_idle") == 0);
}
oq_odu::RuntimeFrequencySnapshot heating_snapshot(oq_odu::Variant variant, const std::array<float, 10>& frequencies) {
  oq_odu::RuntimeFrequencySnapshot snapshot;
  snapshot.variant = variant;
  snapshot.heating.level_count = oq_odu::LEGACY_FREQUENCY_LEVEL_COUNT;
  for (size_t i = 0; i < frequencies.size(); ++i) snapshot.heating.hz[i + 1] = static_cast<uint8_t>(frequencies[i]);
  snapshot.heating.valid = oq_odu::validate_frequency_table(snapshot.heating);
  assert(snapshot.heating.valid);
  return snapshot;
}

void test_mixed_generations_dispatch_with_individual_frequency_limits() {
  using namespace oq_power_house_dispatch;
  const auto v1 = heating_snapshot(oq_odu::Variant::V1_5, oq_perf::V1_HEATING_FREQUENCIES_HZ);
  const auto v2 = heating_snapshot(oq_odu::Variant::V2_OLD_MODEL, oq_perf::V2_HEATING_FREQUENCIES_HZ);
  const oq_frequency_runtime::Context frequency{false, v1, v2, 72, {55, 61}, {26, 30}, v1.variant, v2.variant};
  DispatchInput in{60000U, 9000.0f, true, true, true, {}, {}};
  for (bool hp1 : {true, false}) {
    auto& pump = hp1 ? in.hp1 : in.hp2;
    pump.candidate.available_for_start = true;
    for (int level = 1; level <= kMaxLevel; ++level) {
      const auto prediction =
          oq_perf::predict_candidate(frequency, frequency.performance_variant(hp1), hp1, level, 2.0f, 35.0f);
      const auto& values = prediction.performance;
      pump.levels[level] = {prediction.frequency_policy_allowed,
                            std::isfinite(values.pth_w),
                            std::isfinite(values.pel_w),
                            values.pth_w,
                            values.pel_w,
                            static_cast<uint8_t>(prediction.runtime_frequency_hz)};
    }
  }
  assert(frequency.automatic_frequency_hz(true, 2, 1) == 30);
  assert(frequency.automatic_frequency_hz(false, 2, 1) == 20);
  assert(!in.hp1.levels[4].allowed && in.hp2.levels[4].allowed);
  assert(in.hp1.levels[2].allowed && !in.hp2.levels[2].allowed);

  const DispatchTuning tuning{4000.0f, 4000.0f};
  const auto best_single = pick_topology(in, tuning, in.requested_w, 1, 0.0f);
  assert(best_single.valid && best_single.thermal_w + tuning.topology_heat_advantage_w < in.requested_w);
  const auto out = decide_dispatch(in, tuning, {});
  assert(out.output_valid && !out.saturated && out.hp1_level > 0 && out.hp2_level > 0);
  const auto& hp1 = in.hp1.levels[out.hp1_level];
  const auto& hp2 = in.hp2.levels[out.hp2_level];
  assert(hp1.allowed && hp2.allowed);
  assert(out.expected_w == hp1.thermal_w + hp2.thermal_w);
  assert(std::fabs(out.expected_w - in.requested_w) <= in.requested_w * 0.05f);
  assert(hp1.electrical_w + hp2.electrical_w <= tuning.peak_limit_w);
}

}  // namespace
int main() {
  test_cold_weather_request_reaches_dispatch_and_deficit();
  test_extension_uses_only_active_units();
  test_actuator_hold_constrains_owner_swap();
  test_feedback_hold_prefers_upshift_before_starting_peer();
  test_timing_and_matrix();
  test_performance_supply_fallback();
  test_single_and_failures();
  test_mixed_generations_dispatch_with_individual_frequency_limits();
  test_duo_daily_demand_profile();
  test_runtime_lead_changes_between_heating_cycles();
  test_duo_waits_for_minimum_off_after_demand_rebound();
  test_duo_heat_deficit_under_electrical_limit();
  test_weather_drives_single_and_duo_dispatch();
  test_duo_recovers_after_water_temperature_limit();
  test_duo_switches_for_meaningful_power_savings();
  test_defrost_holds_single_owner_until_recovery_tail_ends();
  test_mild_weather_run_extension_drives_compressor_requests();
  test_cold_weather_boiler_assist_tracks_remaining_demand();
  test_duo_holds_and_boost();
  return 0;
}
