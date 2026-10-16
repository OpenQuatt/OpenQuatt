#include <cassert>
#include <cmath>
#include <string>

struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V2"; }
} hp_generation;
#define id(value) value
#include "includes/performance/hp_perf_frequency.h"
#undef id
#include "includes/performance/oq_performance_observer_logic.h"

namespace {
using namespace oq_performance_observer;
constexpr int first_hz[] = {0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90};
constexpr int second_hz[] = {0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60, 65, 68, 72, 76, 82, 85, 90, 95, 102, 110};
Policy policy() {
  Policy p;
  p.cap_hz = 90;
  p.variant = {oq_odu::Variant::V2_OLD_MODEL, oq_odu::Variant::V2_NEW_MODEL};
  for (unsigned hp = 0; hp < 2; ++hp)
    for (int level = 0; level <= (hp == 0 ? 10 : 20); ++level) {
      const int hz = hp == 0 ? first_hz[level] : second_hz[level];
      p.hz[hp][level] = hz;
      p.allowed[hp][level] = hz <= 90;
    }
  return p;
}
Snapshot frame(uint64_t now = 100000) {
  Snapshot s;
  s.now_ms = now;
  s.context_valid_until_ms = {now + 30000, now + 30000};
  s.outside.observe(-7, now, true);
  s.flow.observe(800, now, true);
  s.order_confirmed = s.common_flow_confirmed = true;
  s.online = s.heating = {true, true};
  for (unsigned hp = 0; hp < 2; ++hp) {
    s.inlet[hp].observe(hp == 0 ? 30 : 33, now, true);
    s.outlet[hp].observe(hp == 0 ? 33 : 36, now, true);
    s.frequency[hp].observe(hp == 0 ? 48 : 45, now, true);
  }
  return s;
}
oq_water_shadow::ModelPoint model(unsigned hp, float hz, float outside, float outlet) {
  const auto p = oq_perf::predict_heating_hz(policy().variant[hp], hz, outside, outlet);
  return {p.pth_w, p.pel_w, p.available, p.low_supply_boundary_estimate};
}
Context context() {
  Context c;
  c.mode = Mode::POWER_HOUSE;
  c.captured_ms = 99000;
  c.reference_outside_c = -7;
  c.reference_water_c = 35;
  c.ph.now_ms = 100000;
  c.ph.demand_level = 20;
  c.ph.requested_w = 5000;
  c.ph.duo = c.ph.performance_valid = c.ph.lead_hp1 = true;
  c.ph.hp1.candidate = {4, true, false, false, true};
  c.ph.hp2.candidate = {6, true, false, false, true};
  c.ph_tuning = {6000, 6500};
  const auto p = policy();
  for (unsigned hp = 0; hp < 2; ++hp)
    for (int level = 0; level <= 20; ++level) {
      const auto point = model(hp, p.hz[hp][level], -7, 35);
      (hp == 0 ? c.ph.hp1 : c.ph.hp2).levels[level] = {p.allowed[hp][level], point.available,    point.available,
                                                       point.thermal_w,      point.electrical_w, p.hz[hp][level]};
      c.curve_power[hp][level] = level == 0 ? 0 : p.allowed[hp][level] && point.available ? point.thermal_w : NAN;
    }
  c.ph_actual = oq_power_house_dispatch::decide_dispatch(c.ph, c.ph_tuning, c.ph_state);
  c.curve.now_ms = 1000000;
  c.curve.loop_ms = 60000;
  c.curve.demand_active = true;
  c.curve.capped = c.curve.previous_capped = 16;
  c.curve.demand_u = c.curve.dispatch_u = .8;
  c.curve.temperature_error_c = .95;
  c.curve.previous_hp1 = 4;
  c.curve.previous_hp2 = 6;
  c.curve.hp1 = c.ph.hp1.candidate;
  c.curve.hp2 = c.ph.hp2.candidate;
  c.curve.hp1_model_available = c.curve.hp2_model_available = true;
  c.curve.hz = p.hz;
  c.curve_state.dual_enabled = true;
  c.curve_actual = oq_curve_duo::decide(c.curve, c.curve_state, [&](int first, int second) {
    const float value = c.curve_power[0][first] + c.curve_power[1][second];
    return oq_power_house_dispatch::PairPower{value, NAN, std::isfinite(value)};
  });
  return c;
}
void receipt_windows_and_rejection() {
  Stability stable;
  const auto p = policy();
  auto s = frame();
  assert(stable.observe(s, p, 0) == Status::SETTLING);
  for (unsigned tick = 1; tick < 100; ++tick) {
    s.now_ms += 100;
    assert(stable.observe(s, p, 0) == Status::SETTLING);
  }
  assert(stable.samples() == 1);
  stable.reset();
  for (unsigned sample = 0; sample <= 6; ++sample) {
    s = frame(100000 + sample * 10000);
    assert(stable.observe(s, p, 0) == (sample == 6 ? Status::VALID : Status::SETTLING));
  }
  // A formerly valid window must be revoked by each frequency receipt failure.
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto invalid = s;
    if (fault == 0) invalid.frequency[1].valid = false;
    if (fault == 1) invalid.frequency[1].received_ms = invalid.now_ms - 30001;
    if (fault == 2) invalid.frequency[1].received_ms = invalid.now_ms + 1;
    if (fault == 3) invalid.frequency[1].received_ms = invalid.now_ms - 20000;
    assert(stable.observe(invalid, p, 0) != Status::VALID);
    assert(stable.samples() == 0);
    assert(stable.observe(s, p, 0) == Status::SETTLING);
  }
  auto changed = p;
  changed.offsets[0] = .1;
  assert(stable.observe(s, changed, 0) == Status::SETTLING);
  s.outlet[0].observe(NAN, s.now_ms, true);
  assert(stable.observe(s, p, 0) == Status::INVALID_INPUT);
  s = frame();
  s.hp1_upstream = false;
  assert(stable.observe(s, p, 0) == Status::HYDRAULIC_MISMATCH);
  s = frame();
  s.context_valid_until_ms = {};
  assert(stable.observe(s, p, 0) == Status::STALE);
  s = frame();
  s.online[1] = false;
  assert(stable.observe(s, p, 0) == Status::INVALID_INPUT);
  stable.reset();
  for (unsigned sample = 0; sample <= 6; ++sample)
    assert(stable.observe(frame(100000 + sample * 10000), p, 300000) == Status::SETTLING);
  // A level/frequency step requires another window, regardless of wall time.
  s = frame(170000);
  s.frequency[1].value = 48;
  assert(stable.observe(s, p, 0) == Status::SETTLING);
  assert(stable.samples() == 1);
}
void complete_jobs_and_oracle() {
  for (const auto mode : {Mode::POWER_HOUSE, Mode::CURVE}) {
    Job job;
    job.context = context();
    job.context.mode = mode;
    job.begin(frame(), policy());
    assert(!job.result.valid);
    unsigned ticks = 0;
    while (job.running) {
      job.step(100000 + ticks * 25);
      assert(++ticks <= 441);
      if (job.running) assert(!job.result.valid);
    }
    assert(job.result.valid && job.status == Status::VALID && ticks == 198);
    assert(job.result.context_ms == 99000);
    job.context.captured_ms = 101000;  // Pending next context cannot redate a published result.
    assert(job.result.context_ms == 99000);
    const auto& chosen = job.result.series;
    assert(job.policy.hz[0][chosen.first] <= 90 && job.policy.hz[1][chosen.second] <= 90);
    const auto pair = [&](int first, int second) {
      const auto p = oq_water_shadow::predict_series(
          frame(), {static_cast<float>(policy().hz[0][first]), static_cast<float>(policy().hz[1][second])}, {}, model);
      const bool allowed = policy().allowed[0][first] && policy().allowed[1][second];
      return oq_power_house_dispatch::PairPower{p.thermal_w, p.electrical_w, p.valid() && allowed};
    };
    if (mode == Mode::POWER_HOUSE) {
      auto in = job.context.ph;
      in.performance_valid = true;
      const auto expected = oq_power_house_dispatch::decide_dispatch_using(
          in, job.context.ph_tuning, job.context.ph_state, [&](int first, int second, float target) {
            const auto estimate = pair(first, second);
            return oq_power_house_dispatch::make_candidate(in, job.context.ph_tuning, first, second, target, &estimate);
          });
      assert(chosen.first == expected.hp1_level && chosen.second == expected.hp2_level);
      assert(std::fabs(chosen.thermal_w - expected.expected_w) < .001);
    } else {
      const auto expected = oq_curve_duo::decide(job.context.curve, job.context.curve_state, pair, false);
      assert(chosen.first == expected.hp1_level && chosen.second == expected.hp2_level);
    }
    job.invalidate(Status::CONTEXT_CHANGED);
    assert(!job.result.valid && !job.running && job.result.context_ms == 0);
    job.begin(frame(), policy());
    for (unsigned tick = 0; job.running; ++tick) job.step(100000 + tick * 25);
    assert(job.result.valid && job.result.context_ms == 101000);
    job.begin(frame(), policy());
    job.step(130001);
    assert(!job.running && !job.result.valid && job.status == Status::STALE);
  }
  Job mismatch;
  mismatch.context = context();
  ++mismatch.context.ph_actual.hp1_level;
  mismatch.begin(frame(), policy());
  for (unsigned tick = 0; mismatch.running; ++tick) mismatch.step(100000 + tick * 25);
  assert(mismatch.status == Status::BASELINE_MISMATCH && !mismatch.result.valid);
}
void effective_hold_context() {
  for (unsigned timer = 0; timer < 3; ++timer) {
    auto c = context();
    c.curve.dual_on_hold_min = 2;
    c.curve.dual_off_hold_min = 5;
    c.curve.tuning.dual_emergency_hold_min = 3;
    auto* value = timer == 0   ? &c.curve_state.on_min
                  : timer == 1 ? &c.curve_state.off_min
                               : &c.curve_state.emergency_min;
    const float threshold = timer == 0 ? 2 : timer == 1 ? 5 : 3;
    const auto get = [&] { return key(c.curve, c.curve_state, c.curve_power, {-7, 35}); };
    *value = threshold - .25f;
    const auto before = get();
    *value = threshold;
    const auto reached = get();
    assert(before.valid && reached.valid && !(before == reached));
    *value = threshold + 1;
    assert(get() == reached);
    *value = threshold + 100;
    assert(get() == reached);
    if (timer == 0) ++c.curve.dual_on_hold_min;
    if (timer == 1) ++c.curve.dual_off_hold_min;
    if (timer == 2) ++c.curve.tuning.dual_emergency_hold_min;
    assert(!(get() == reached));
    for (const float invalid : {NAN, INFINITY, -INFINITY}) {
      *value = invalid;
      assert(!get().valid);
    }
  }
}
void context_identity_and_pair_limits() {
  auto c = context();
  const Reference ref{-7, 35};
  auto a = key(c.ph, c.ph_tuning, c.ph_state, ref);
  assert(a.valid && a == key(c.ph, c.ph_tuning, c.ph_state, ref));
  ++c.ph.now_ms;
  assert(a == key(c.ph, c.ph_tuning, c.ph_state, ref));
  c.ph.hp2.candidate.minimum_off_ready = false;
  assert(!(a == key(c.ph, c.ph_tuning, c.ph_state, ref)));
  const auto b = key(c.curve, c.curve_state, c.curve_power, ref);
  c.curve.tuning.dual_emergency_hold_min++;
  assert(!(b == key(c.curve, c.curve_state, c.curve_power, ref)));
  c = context();
  const oq_power_house_dispatch::PairPower excessive{6000, 7000, true};
  assert(!oq_power_house_dispatch::make_candidate(c.ph, c.ph_tuning, 4, 6, 5000, &excessive).valid);
  const oq_power_house_dispatch::PairPower valid{5000, 1800, true};
  c.ph.hp2.candidate = {0, false, false, false, false};
  assert(!oq_power_house_dispatch::make_candidate(c.ph, c.ph_tuning, 4, 6, 5000, &valid).valid);
}
}  // namespace
int main() {
  receipt_windows_and_rejection();
  complete_jobs_and_oracle();
  context_identity_and_pair_limits();
  effective_hold_context();
}
