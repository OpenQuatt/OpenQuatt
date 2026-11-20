#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <string>

#include "../../openquatt/includes/control/oq_power_house_dispatch_logic.h"
#include "../../openquatt/includes/control/oq_power_house_demand_logic.h"
#include "../../openquatt/includes/control/oq_power_house_run_extension_logic.h"

namespace {
struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V1.5"; }
} hp_generation;
}  // namespace

#define id(value) value
#include "../../openquatt/includes/performance/oq_water_performance.h"
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
    // A pending Single and an allowed upshift still use their raw watts.
    running.levels[4].electrical_w = 250.0f;
    limits.soft_limit_w = limits.peak_limit_w = 500.0f;
    limits.hp1_circulation_w = limits.hp2_circulation_w = 100.0f;
    check(4, 0);
    pending_single = {3000.0f, 501.0f};
    check(5, 0);
    pending_single = {3000.0f, 450.0f};
    running.levels[4].electrical_w = 450.0f;
    limits = tuning();
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
void test_duo_circulation_economics_at_electrical_limits() {
  constexpr float peer_w = 35.0f;
  struct Scenario {
    float single_w, duo_w, peak_w;
    int expected_active;
  };
  const Scenario scenarios[] = {
      {990, 1010, 1900, 1},                         // Prefer the smaller raw-soft excess.
      {962, 981, 1900, 2},                          // Below soft, use economic cost.
      {1005, 1020, 1900, 1}, {990, 1010, 1000, 1},  // Peak eligibility uses raw electrical watts.
      {990, 1010, 1010, 1},  {990, 1001, 1900, 2},  // A 1 W raw-soft difference is a tie.
      {990, 1002, 1900, 1},
  };
  auto in = input(true);
  in.requested_w = 4000;
  in.hp1.candidate = {1, true, false, false, true};
  in.hp2.candidate = {6, true, false, false, true};
  for (const auto& scenario : scenarios) {
    for (auto* unit : {&in.hp1, &in.hp2}) {
      unit->levels = {};
      unit->levels[1] = {true, true, true, 2000, scenario.duo_w / 2, 30};
      unit->levels[6] = {true, true, true, 4000, scenario.single_w, 67};
    }
    DispatchTuning limits{1000, scenario.peak_w};
    limits.hp1_circulation_w = limits.hp2_circulation_w = 0;
    const auto uncorrected = decide_dispatch(in, limits, {});
    assert(topology(uncorrected.hp1_level, uncorrected.hp2_level) == 1);
    limits.hp1_circulation_w = peer_w;
    for (float invalid : {NAN, INFINITY, -1.0f}) {
      limits.hp2_circulation_w = invalid;
      const auto unknown = decide_dispatch(in, limits, {});
      assert(unknown.hp1_level == uncorrected.hp1_level && unknown.hp2_level == uncorrected.hp2_level &&
             unknown.reason == uncorrected.reason && unknown.capacity_w == uncorrected.capacity_w);
      const auto& single = unknown.economic_comparison.single;
      assert(single.valid && single.electrical_w == single.economic_w);
    }
    limits.hp2_circulation_w = peer_w;
    const auto corrected = decide_dispatch(in, limits, {});
    assert(topology(corrected.hp1_level, corrected.hp2_level) == scenario.expected_active);
    const auto& comparison = corrected.economic_comparison;
    assert(comparison.single.valid && comparison.single.electrical_w == scenario.single_w &&
           comparison.single.economic_w == scenario.single_w + peer_w);
    assert(comparison.duo.valid == (scenario.duo_w <= scenario.peak_w));
    const auto& chosen = comparison.chosen;
    assert(chosen.valid && chosen.electrical_w <= scenario.peak_w && chosen.thermal_w == 4000);
    assert(chosen.hp1_level == corrected.hp1_level && chosen.hp2_level == corrected.hp2_level);
  }

  // A thermal improvement beyond the existing threshold still takes precedence.
  DispatchTuning limits{1000, 1900};
  limits.hp1_circulation_w = limits.hp2_circulation_w = peer_w;
  in.hp1.levels[1] = in.hp2.levels[1] = {true, true, true, 2250, 505, 30};
  in.requested_w = 4500;
  const auto thermal = decide_dispatch(in, limits, {});
  assert(thermal.hp1_level == 1 && thermal.hp2_level == 1 && thermal.expected_w == 4500);
  const auto idle_cost = make_candidate(in, limits, 0, 0, 0);
  assert(idle_cost.economic_w() == 0);

  // A confirmed zero on one unit is a known cost and the idle peer determines the addend.
  limits.hp1_circulation_w = 0;
  const auto first = make_candidate(in, limits, 6, 0, 4000);
  const auto second = make_candidate(in, limits, 0, 6, 4000);
  assert(first.valid && first.economic_w() == first.electrical_w + peer_w);
  assert(second.valid && second.economic_w() == second.electrical_w);
  in.duo = false;
  const auto single = make_candidate(in, limits, 6, 0, 4000);
  assert(single.valid && single.economic_w() == single.electrical_w);
  assert(!decide_dispatch(in, limits, {}).economic_comparison.chosen.valid);
}

struct V15FieldFrequency {
  oq_odu::Variant performance_variant(bool) const { return oq_odu::Variant::V1_5; }
  int automatic_frequency_hz(bool, int, int level) const { return oq_odu::V1_HEATING_DEMAND_REFERENCE_HZ[level]; }
};

void test_duo_circulation_replays_1203_field_vector() {
  using oq_water_performance::CandidateModel;
  struct FieldModel {
    CandidateModel model{V15FieldFrequency{}, 3.9f, 31.67f, 793.512f, {36.4f, 36.4f}, true, false};
    mutable unsigned pair_calls = 0;
  } field;
  auto in = input(true);
  in.now_ms = 900000;
  in.requested_w = 3993;
  in.hp1.candidate = {2, true, false, false, true};
  in.hp2.candidate = {1, true, false, false, true};
  in.pair_power = [](const void* context, int first, int second) {
    const auto& field = *static_cast<const FieldModel*>(context);
    ++field.pair_calls;
    return CandidateModel::dispatch(&field.model, first, second);
  };
  in.pair_context = &field;
  const ConfirmedPower confirmed_power = [](const void* context, int first, int second, float nominal_w) {
    return CandidateModel::confirmed_power(&static_cast<const FieldModel*>(context)->model, first, second, nominal_w);
  };
  for (bool first : {true, false}) {
    auto& unit = first ? in.hp1 : in.hp2;
    for (int level = 1; level <= 10; ++level) {
      const auto p = field.model.unit(first, level);
      unit.levels[level] = {
          true,           p.valid(),
          p.valid(),      p.thermal_w,
          p.electrical_w, static_cast<uint8_t>(V15FieldFrequency{}.automatic_frequency_hz(first, 2, level))};
    }
  }

  DispatchTuning limits{3400, 3650};
  const auto uncorrected = decide_dispatch(in, limits, {}, confirmed_power);
  assert(uncorrected.hp1_level == 6 && uncorrected.hp2_level == 0);
  const unsigned uncorrected_pair_calls = field.pair_calls;

  limits.hp1_circulation_w = limits.hp2_circulation_w = 35.15f;
  field.pair_calls = 0;
  const auto corrected = decide_dispatch(in, limits, {}, confirmed_power);
  assert(corrected.hp1_level == 2 && corrected.hp2_level == 1);
  assert(field.pair_calls == uncorrected_pair_calls);
  assert(corrected.capacity_w == uncorrected.capacity_w);
  const auto& comparison = corrected.economic_comparison;
  assert(comparison.duo.valid && comparison.duo.electrical_w == comparison.duo.economic_w);
  assert(comparison.single.valid &&
         std::fabs(comparison.single.electrical_w + 35.15f - comparison.single.economic_w) < 0.01f);

  // A brief dip at the Solo/Duo boundary after six hours at steady demand.
  // Feed back dispatched levels on each tick; the hydraulic test fixture is fixed.
  auto running = in;
  DispatchState state;
  for (uint32_t elapsed_s = 0; elapsed_s <= 6 * 3600 + 120; elapsed_s += 10) {
    running.now_ms = in.now_ms + elapsed_s * 1000;
    running.requested_w = elapsed_s >= 6 * 3600 && elapsed_s < 6 * 3600 + 60 ? 3993 : 4224;
    state = observe_protection(
        state, {running.now_ms, false, false, false, applied_level(running.hp1), applied_level(running.hp2)});
    const auto next = decide_dispatch(running, limits, state, confirmed_power);
    assert(next.hp1_level > 0 && next.hp2_level > 0);
    running.hp1.candidate.previous_applied_level = next.hp1_level;
    running.hp2.candidate.previous_applied_level = next.hp2_level;
  }

  limits.hp2_circulation_w = NAN;
  const auto stale = decide_dispatch(in, limits, {}, confirmed_power);
  assert(stale.hp1_level == 6 && stale.hp2_level == 0 && stale.reason == uncorrected.reason);

  limits.hp2_circulation_w = 35.15f;
  in.hp2.candidate.must_stop = true;
  const auto stopped = decide_dispatch(in, limits, {}, confirmed_power);
  assert(stopped.hp1_level == 6 && stopped.hp2_level == 0 && !stopped.economic_comparison.duo.valid);
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
}  // namespace
int main() {
  test_cold_weather_request_reaches_dispatch_and_deficit();
  test_extension_uses_only_active_units();
  test_actuator_hold_constrains_owner_swap();
  test_feedback_hold_prefers_upshift_before_starting_peer();
  test_timing_and_matrix();
  test_performance_supply_fallback();
  test_single_and_failures();
  test_duo_holds_and_boost();
  test_duo_circulation_economics_at_electrical_limits();
  test_duo_circulation_replays_1203_field_vector();
  return 0;
}
