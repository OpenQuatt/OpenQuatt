#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../../openquatt/includes/control/oq_power_house_dispatch_logic.h"

namespace {
using namespace oq_power_house_dispatch;
static_assert(sizeof(DispatchState) <= 40);
HpInput hp(int previous = 0, bool available = true) {
  HpInput result;
  result.candidate = {previous, available, false, false};
  for (int level = 0; level <= kMaxLevel; ++level)
    result.levels[level] = {true, true, true, 1000.0f * level, 100.0f * level};
  return result;
}
DispatchInput input(bool duo = false) { return {60000, 3, 3000.0f, duo, true, true, hp(), hp()}; }
DispatchTuning tuning() { return {9000.0f, 10000.0f, 5.0f, 150.0f, 450.0f, 6, 1}; }
void expect_safe(const DispatchDecision& out, const DispatchInput& in, const DispatchTuning& cfg) {
  assert(out.hp1_level >= 0 && out.hp1_level <= kMaxLevel && out.hp2_level >= 0 && out.hp2_level <= kMaxLevel);
  assert((in.duo || out.hp2_level == 0) && out.owner_hp == owner(out.hp1_level, out.hp2_level));
  if (in.demand_level <= 0) assert(out.hp1_level == 0 && out.hp2_level == 0);
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
    for (int demand : {0, 1, 10, 20})
      for (int previous : {-1, 0, 3, 11})
        for (int stops = 0; stops < 4; ++stops)
          for (float cap : {0.0f, 299.0f, 300.0f, 10000.0f}) {
            auto in = input(duo);
            in.demand_level = demand;
            in.hp1.candidate = {previous, true, (stops & 1) != 0, true};
            in.hp2.candidate = {previous, true, (stops & 2) != 0, true};
            auto cfg = tuning();
            cfg.soft_limit_w = cfg.peak_limit_w = cap;
            expect_safe(decide_dispatch(in, cfg, {}), in, cfg);
          }
}
void test_single_and_failures() {
  auto cfg = tuning();
  auto in = input(true);
  in.demand_level = 20;
  in.performance_valid = false;
  in.hp1.candidate.previous_applied_level = 4;
  in.hp2.candidate.previous_applied_level = 3;
  auto out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level == 4 && out.hp2_level == 3 && out.reason == Reason::FALLBACK_DUO && !out.output_valid);
  in.hp1.candidate.must_stop = true;
  assert(decide_dispatch(in, cfg, {}).reason == Reason::FALLBACK_HP2);
  in.hp2.candidate = {};
  for (int previous : {-1, 11, 20}) {
    in.hp1.candidate = {previous, true, false, false};
    assert(decide_dispatch(in, cfg, {}).reason == Reason::FALLBACK_IDLE);
  }
  in.requested_w = NAN;
  in.performance_valid = true;
  out = decide_dispatch(in, cfg, {});
  assert(out.hp1_level == 0 && out.hp2_level == 0 && !out.output_valid);
  in.requested_w = 3000;
  in.demand_level = 0;
  in.performance_valid = false;
  assert(!decide_dispatch(in, cfg, {}).output_valid);
  in = input(true);
  in.hp1.candidate = {11, false, false, true};
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
void test_duo_holds_and_boost() {
  auto in = input(true);
  auto cfg = tuning();
  in.lead_hp1 = false;
  auto out = decide_dispatch(in, cfg, {});
  assert(out.hp2_level == 3 && out.reason == Reason::RUNTIME_LEAD);
  in.hp2.candidate.available_for_start = false;
  assert(decide_dispatch(in, cfg, {}).hp1_level > 0);
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
  in.demand_level = 7;
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
  test_timing_and_matrix();
  test_single_and_failures();
  test_duo_holds_and_boost();
  return 0;
}
