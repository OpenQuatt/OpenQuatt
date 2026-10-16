#include <assert.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

struct FakeGenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V2"; }
} hp_generation;

#define id(value) value
#include "../../openquatt/includes/performance/hp_perf_frequency.h"
#undef id
#include "../../openquatt/includes/experimental/oq_water_performance_probe.h"

namespace {
using namespace oq_water_shadow;

void near(float actual, float expected, float tolerance = 0.02f) {
  assert(std::isfinite(actual));
  assert(std::fabs(actual - expected) <= tolerance);
}

Snapshot snapshot() {
  Snapshot s;
  s.now_ms = 100000;
  s.context_valid_until_ms = {129900, 129900};
  s.outside.observe(7.0f, 99900, true);
  s.flow.observe(800.0f, 99900, true);
  for (unsigned hp = 0; hp < 2; ++hp) {
    s.inlet[hp].observe(hp == 0 ? 30.0f : 33.0f, 99900, true);
    s.outlet[hp].observe(hp == 0 ? 33.0f : 36.0f, 99900, true);
    s.frequency[hp].observe(30.0f, 99900, true);
    s.online[hp] = true;
    s.heating[hp] = true;
  }
  s.order_confirmed = true;
  s.common_flow_confirmed = true;
  return s;
}

ModelPoint linear(unsigned, float hz, float, float outlet) {
  const float thermal = 5000.0f + 10.0f * hz - 50.0f * outlet;
  return {thermal, thermal / 4.0f, true};
}

ModelPoint native(unsigned hp, float hz, float outside, float outlet) {
  const auto p = oq_perf::predict_heating_hz(hp == 0 ? oq_odu::Variant::V2_OLD_MODEL : oq_odu::Variant::V2_NEW_MODEL,
                                             hz, outside, outlet);
  return {p.pth_w, p.pel_w, p.available, p.low_supply_boundary_estimate};
}

void observed_and_receipts() {
  auto s = snapshot();
  const Limits limits;
  const auto first = predict_observed(s, 0, limits, linear);
  const auto second = predict_observed(s, 1, limits, linear);
  assert(first.valid() && second.valid());
  near(first.thermal_w, 3650.0f);
  near(second.thermal_w, 3500.0f);
  s.outlet[0].value += 1.0f;
  near(predict_observed(s, 0, limits, linear).thermal_w, 3600.0f);
  near(predict_observed(s, 1, limits, linear).thermal_w, second.thermal_w);
  s.outlet[0].received_ms = s.now_ms - limits.water_age_ms;
  assert(predict_observed(s, 0, limits, linear).status == Status::TIME_SKEW);
  s.frequency[0].received_ms = s.outlet[0].received_ms;
  assert(predict_observed(s, 0, limits, linear).valid());
  --s.outlet[0].received_ms;
  assert(predict_observed(s, 0, limits, linear).status == Status::STALE);
  for (const auto time : {uint64_t{0}, s.now_ms + 1}) {
    s = snapshot();
    s.outlet[0].received_ms = time;
    assert(!predict_observed(s, 0, limits, linear).valid());
  }
  s = snapshot();
  s.outlet[0].received_ms = s.frequency[0].received_ms - limits.maximum_skew_ms;
  assert(predict_observed(s, 0, limits, linear).valid());
  --s.outlet[0].received_ms;
  assert(predict_observed(s, 0, limits, linear).status == Status::TIME_SKEW);
  for (const float invalid : {NAN, INFINITY, -INFINITY}) {
    s = snapshot();
    s.outlet[0].observe(invalid, s.now_ms, true);
    assert(predict_observed(s, 0, limits, linear).status == Status::INVALID_INPUT);
  }
  s = snapshot();
  s.outlet[0].revoke(s.now_ms);
  assert(!predict_observed(s, 0, limits, linear).valid());
  s = snapshot();
  s.frequency[0].value = 0;
  assert(predict_observed(s, 0, limits, linear).status == Status::TRANSIENT);
  s.heating[0] = false;
  assert(predict_observed(s, 0, limits, linear).status == Status::OFF);
  s.frequency[0].value = 90;
  s.heating[0] = true;
  assert(predict_observed(s, 0, limits, linear).valid());
  s.frequency[0].value = std::nextafter(90.0f, INFINITY);
  assert(predict_observed(s, 0, limits, linear).status == Status::FREQUENCY_POLICY);
  s = snapshot();
  s.transient[0] = true;
  assert(predict_observed(s, 0, limits, linear).status == Status::TRANSIENT);
  s = snapshot();
  s.online[0] = false;
  assert(!predict_observed(s, 0, limits, linear).valid());
  assert(!predict_observed(s, 2, limits, linear).valid());
}

void stationary_solver() {
  const auto s = snapshot();
  const Limits limits;
  const float capacity = 800.0f * 4186.0f / 3600.0f;
  const auto p = predict_series(s, {30, 30}, limits, linear);
  assert(p.valid());
  const float out1 = (capacity * 30.0f + 5300.0f) / (capacity + 50.0f);
  const float out2 = (capacity * out1 + 5300.0f) / (capacity + 50.0f);
  near(p.unit[0].outlet_c, out1, 0.002f);
  near(p.unit[1].outlet_c, out2, 0.002f);
  for (const auto& unit : p.unit) {
    assert(std::fabs(unit.balance_error_w) <= limits.balance_tolerance_w);
    assert(unit.evaluations <= limits.maximum_iterations + 2);
  }
  const auto higher_first = predict_series(s, {60, 30}, limits, linear);
  assert(higher_first.valid());
  assert(higher_first.unit[0].outlet_c > p.unit[0].outlet_c);
  assert(higher_first.unit[1].thermal_w < p.unit[1].thermal_w);
  const auto off_first = predict_series(s, {0, 30}, limits, linear);
  assert(off_first.valid() && off_first.unit[0].status == Status::OFF);
  near(off_first.unit[0].outlet_c, 30);
  near(off_first.unit[1].outlet_c, out1, 0.002f);
  const auto off = predict_series(s, {0, 0}, limits, linear);
  assert(off.status == Status::OFF);
  near(off.thermal_w, 0);
  auto reverse = s;
  reverse.hp1_upstream = false;
  std::swap(reverse.inlet[0], reverse.inlet[1]);
  std::swap(reverse.outlet[0], reverse.outlet[1]);
  const auto reversed = predict_series(reverse, {30, 30}, limits, linear);
  assert(reversed.status == Status::HYDRAULIC_MISMATCH);
  const auto constant = [](unsigned, float, float, float) { return ModelPoint{1000, 250, true}; };
  const auto exact = solve_unit(0, 30, 7, 30, 1000, limits, constant);
  assert(exact.valid() && exact.evaluations == 2);
  near(exact.outlet_c, 31);
  auto short_limit = limits;
  short_limit.maximum_iterations = 1;
  assert(predict_series(s, {30, 30}, short_limit, linear).status == Status::NO_CONVERGENCE);
  const auto growing = [](unsigned, float, float, float outlet) { return ModelPoint{100 * outlet, 500, true}; };
  assert(solve_unit(0, 30, 7, 30, capacity, limits, growing).status == Status::NO_CONVERGENCE);
}

void rejection_contract() {
  const Limits limits;
  auto s = snapshot();
  s.order_confirmed = false;
  assert(predict_series(s, {30, 30}, limits, linear).status == Status::UNKNOWN_ORDER);
  s = snapshot();
  s.common_flow_confirmed = false;
  assert(predict_series(s, {30, 30}, limits, linear).status == Status::INVALID_FLOW);
  for (const float flow : {0.0f, -1.0f, 299.0f, 3001.0f, NAN, INFINITY}) {
    s = snapshot();
    s.flow.observe(flow, s.now_ms, true);
    assert(!predict_series(s, {30, 30}, limits, linear).valid());
  }
  s = snapshot();
  s.inlet[1].value = 40;
  assert(predict_series(s, {30, 30}, limits, linear).status == Status::HYDRAULIC_MISMATCH);
  s = snapshot();
  s.outlet[1].received_ms -= limits.maximum_skew_ms + 1;
  assert(predict_series(s, {30, 30}, limits, linear).status == Status::TIME_SKEW);
  s = snapshot();
  s.transient[1] = true;
  assert(predict_series(s, {30, 30}, limits, linear).status == Status::TRANSIENT);
  s = snapshot();
  for (const auto frequencies : {std::array<float, 2>{90.01f, 30}, {-1, 30}, {NAN, 30}, {30, INFINITY}})
    assert(predict_series(s, frequencies, limits, linear).status == Status::FREQUENCY_POLICY);
  const auto missing_second = [](unsigned hp, float hz, float ambient, float outlet) {
    return hp == 1 ? ModelPoint{} : linear(hp, hz, ambient, outlet);
  };
  const auto missing = predict_series(s, {30, 30}, limits, missing_second);
  assert(missing.status == Status::MODEL_UNAVAILABLE);
  assert(std::isnan(missing.thermal_w) && std::isnan(missing.electrical_w));
  const auto missing_middle = [](unsigned hp, float hz, float ambient, float outlet) {
    return outlet > 31.0f && outlet < 33.0f ? ModelPoint{} : linear(hp, hz, ambient, outlet);
  };
  assert(!solve_unit(0, 30, 7, 30, 800, limits, missing_middle).valid());
}

void native_maps() {
  const auto s = snapshot();
  const Limits limits;
  for (const auto frequencies : {std::array<float, 2>{20, 26}, {30, 30}, {45, 30}}) {
    const auto p = predict_series(s, frequencies, limits, native);
    if (!p.valid())
      std::fprintf(stderr, "native %.1f/%.1f: %s\n", frequencies[0], frequencies[1], status_name(p.status));
    assert(p.valid());
    const float capacity = s.flow.value * 4186.0f / 3600.0f;
    const float total = capacity * (p.unit[1].outlet_c - s.inlet[0].value);
    near(p.thermal_w, total, 2.0f);
  }
  assert(predict_series(s, {90, 90}, limits, native).status == Status::MODEL_UNAVAILABLE);
  auto cold = s;
  cold.outside.value = -7;
  for (const auto frequencies : {std::array<float, 2>{85, 90}, {90, 90}}) {
    const auto p = predict_series(cold, frequencies, limits, native);
    assert(p.valid());
    assert(std::fabs(p.unit[0].balance_error_w) <= 1.0f);
    assert(std::fabs(p.unit[1].balance_error_w) <= 1.0f);
  }
  for (const auto variant : {oq_odu::Variant::V1, oq_odu::Variant::V1_5}) {
    const auto model = [variant](unsigned, float hz, float outside, float outlet) {
      const auto p = oq_perf::predict_heating_hz(variant, hz, outside, outlet);
      return ModelPoint{p.pth_w, p.pel_w, p.available, p.low_supply_boundary_estimate};
    };
    assert(predict_series(s, {85, 90}, limits, model).valid());
  }
  auto masked = s;
  masked.outside.value = -7;
  masked.inlet[0].value = 55;
  assert(predict_series(masked, {20, 30}, limits, native).status == Status::MODEL_UNAVAILABLE);
}

void transition_contract() {
  const auto modulation = movement({30, 30}, {36, 40});
  assert(modulation.valid && modulation.starts == 0 && modulation.stops == 0);
  near(modulation.modulation_hz, 16);
  const auto start = movement({30, 0}, {30, 90});
  assert(start.valid && start.starts == 1 && start.stops == 0 && start.topology_change);
  near(start.modulation_hz, 0);
  const auto swap = movement({30, 0}, {0, 30});
  assert(swap.starts == 1 && swap.stops == 1 && swap.owner_swap && !swap.topology_change);
  const auto stop = movement({30, 30}, {0, 30});
  assert(stop.stops == 1 && stop.starts == 0 && stop.topology_change);
  assert(!movement({NAN, 30}, {30, 30}).valid);
  assert(!movement({30, 30}, {30, 90.01f}).valid);
}

void probe_adapter() {
  auto s = snapshot();
  const std::array<oq_odu::Variant, 2> variants{oq_odu::Variant::V2_OLD_MODEL, oq_odu::Variant::V2_NEW_MODEL};
  const auto p = probe(s, {30, 30}, 35, variants);
  assert(p.observed[0].valid() && p.observed[1].valid() && p.series.valid() && p.transition.valid);
  near(p.shared_thermal_w[0], p.shared_thermal_w[1]);
  assert(p.observed[0].thermal_w > p.observed[1].thermal_w);
  const auto mixed = probe(s, {30, 30}, 35, {oq_odu::Variant::UNKNOWN, oq_odu::Variant::V2_NEW_MODEL});
  assert(!mixed.observed[0].valid() && mixed.observed[1].valid());
  assert(!mixed.series.valid() && !mixed.transition.valid);
  s.outlet[0].revoke(s.now_ms);
  const auto invalid = probe(s, {30, 30}, 35, variants);
  assert(!invalid.observed[0].valid() && !invalid.series.valid());
  assert(std::isnan(invalid.shared_thermal_w[0]));
}

void context_contract() {
  oq_sources::RawFloatReceipt mode, defrost, status, protection;
  mode.observe(2, 99900, true);
  defrost.observe(0, 99900, true);
  status.observe(0x0800, 99900, true);
  protection.observe(0, 99900, true);
  assert(stationary_context(mode, defrost, status, protection, 100000));
  protection.value = 0x0008;
  assert(!stationary_context(mode, defrost, status, protection, 100000));
  protection.value = 0;
  status.value = 0x0040;
  assert(!stationary_context(mode, defrost, status, protection, 100000));
  status.value = 0x0800;
  status.received_ms = 69999;
  assert(!stationary_context(mode, defrost, status, protection, 100000));
  status.received_ms = 99900;
  status.value = 65536;
  assert(!stationary_context(mode, defrost, status, protection, 100000));
  status.value = NAN;
  assert(!stationary_context(mode, defrost, status, protection, 100000));
  status.value = 0x0800;
  mode.value = 1;
  assert(!stationary_context(mode, defrost, status, protection, 100000));
}
}  // namespace

int main() {
  observed_and_receipts();
  stationary_solver();
  rejection_contract();
  native_maps();
  transition_contract();
  probe_adapter();
  context_contract();
}
