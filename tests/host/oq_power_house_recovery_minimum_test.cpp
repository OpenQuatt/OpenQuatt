#include <cassert>
#include <string>

#include "../../openquatt/includes/control/oq_power_house_recovery_minimum.h"

namespace {
struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V2"; }
} hp_generation;
}  // namespace
#define id(x) x
#include "../../openquatt/includes/performance/oq_water_performance.h"
#undef id

namespace {
namespace ext = oq_power_house_run_extension;
namespace dsp = oq_power_house_dispatch;
namespace rec = oq_power_house_recovery;
namespace water = oq_water_performance;
constexpr auto v2 = oq_odu::Variant::V2_NEW_MODEL;
constexpr uint32_t now = 500000;
constexpr float base = 6264.621f;
struct Field {
  dsp::DispatchInput dispatch;
  dsp::DispatchTuning limits{3000, 3680};
  dsp::DispatchState protection;
  ext::State previous{ext::Phase::RUNNING, 19.5f, true, false};
  ext::Input run{true, true, true, true, 19.51953f, 19.5f, base, NAN, 0, 1};
  oq_compressor_feedback::Snapshot response;
  oq_sources::HeatPumpReceipts raw;
  float request_limit_w = INFINITY;
  float inlet = 24.42f, outlet = 27.68f, flow = 1215.606f;
  Field() {
    dispatch.now_ms = now;
    dispatch.performance_valid = true;
    dispatch.hp1.candidate = {16, true, false, false, true};
    dispatch.hp1.feedback_hold_level = 16;
    protection.hp1_defrost = {false, true, now - 100000};
    response.actual_hz = 72;
    response.requested_hz = 85;
    response.settling = true;
    for (auto* receipt : {&raw.working_mode, &raw.compressor_frequency, &raw.status_2108, &raw.status_2119,
                          &raw.defrost, &raw.flow, &raw.water_in, &raw.water_out, &raw.outside})
      receipt->observe(0, now, true);
    raw.working_mode.value = 2;
    raw.compressor_frequency.value = 72;
    raw.water_in.value = inlet;
    raw.water_out.value = outlet;
    raw.flow.value = flow;
    point(1, 26);
    point(13, 72);
    point(14, 76);
    point(16, 72);
    auto current = dsp::minimum_active_candidate(dispatch, limits);
    assert(current.valid && current.hp1_level == 16 && current.thermal_w > 6800);
    run.minimum_viable_level = 16;
    run.minimum_viable_w = current.thermal_w;
  }
  void point(int level, float hz) {
    auto p = water::solve(v2, hz, 3.1f, inlet, flow);
    dispatch.hp1.levels[level] = {p.valid(),   p.valid(),      p.valid(),
                                  p.thermal_w, p.electrical_w, static_cast<uint8_t>(hz)};
  }
  ext::Decision evaluate(bool apply = true) {
    if (apply) {
      float q = rec::delivered_heat(raw, dispatch.now_ms, dispatch.now_ms - protection.hp1_defrost.started_ms, 10000,
                                    inlet, outlet, flow);
      rec::use_structural_minimum(run, previous, dispatch, limits, protection, response, q, request_limit_w);
    }
    return ext::evaluate(run, {1, .2f, .1f}, previous);
  }
};
// F1 was absent from the export. 26 Hz is an explicit feasible F1 test condition.
struct FieldFrequency {
  oq_odu::Variant performance_variant(bool) const { return v2; }
  int automatic_frequency_hz(bool, int, int level) const {
    return level == 1 ? 26 : level == 13 ? 72 : level == 14 ? 76 : level == 16 ? 85 : -1;
  }
};
water::CandidateModel coupled_field(const Field& field) {
  return {FieldFrequency{}, 3.1f, field.inlet, field.flow, {field.outlet, NAN}, true, false, {field.response, {}}};
}
void attach_model(Field& field, const water::CandidateModel& model) {
  field.dispatch.pair_context = &model;
  field.dispatch.pair_power = water::CandidateModel::dispatch;
  const auto minimum = dsp::minimum_active_candidate(field.dispatch, field.limits);
  field.run.minimum_viable_level = minimum.hp1_level;
  field.run.minimum_viable_w = minimum.thermal_w;
}
void test_single_field_and_stops() {
  Field f;
  const auto model = coupled_field(f);
  attach_model(f, model);
  const auto stock = f.evaluate(false);
  assert(stock.force_comfort_stop && stock.comfort_stop_c == 19.5f);
  const auto corrected = f.evaluate();
  assert(!corrected.force_comfort_stop && !corrected.floor_active && !corrected.warm_restart_intent &&
         corrected.comfort_stop_c == 20.5f && corrected.next.phase == ext::Phase::RUNNING &&
         f.run.base_requested_w == base && f.run.minimum_viable_level == 1 && f.run.minimum_viable_w > 2600 &&
         f.run.minimum_viable_w < 2800);
  assert(f.dispatch.hp1.feedback_hold_level == 16 && !dsp::make_candidate(f.dispatch, f.limits, 1, 0, base).valid);
  Field warm;
  warm.run.room_c = 20.5f;
  const auto upper = warm.evaluate();
  assert(upper.force_comfort_stop && upper.comfort_stop_c == 20.5f && !upper.floor_active);
  auto retains_input = [](auto mutate) {
    Field field;
    mutate(field);
    const auto stock = field.evaluate(false);
    const float minimum = field.run.minimum_viable_w;
    const auto checked = field.evaluate();
    assert(field.run.minimum_viable_w == minimum && field.run.minimum_viable_level == 16 &&
           checked.force_comfort_stop == stock.force_comfort_stop && checked.floor_active == stock.floor_active &&
           checked.next.phase == stock.next.phase);
  };
  retains_input([](Field& f) { f.dispatch.duo = true; });
  retains_input([](Field& f) { f.dispatch.hp1.levels[1].allowed = false; });
  retains_input([](Field& f) { f.dispatch.hp1.levels[1].thermal_valid = false; });
  retains_input([](Field& f) { f.dispatch.hp1.fixed_level = 16; });
  retains_input([](Field& f) { f.dispatch.hp1.candidate.must_stop = true; });
  retains_input([](Field& f) { f.dispatch.hp1.candidate.link_suspect = true; });
  retains_input([](Field& f) { f.dispatch.hp1.candidate.available_for_start = false; });
  retains_input([](Field& f) { f.dispatch.hp1.defrost = true; });
  retains_input([](Field& f) { f.dispatch.hp1.valve_defrost = true; });
  retains_input([](Field& f) { f.protection.oil_return.previous_active = true; });
  retains_input([](Field& f) { f.protection.hp1_defrost = {}; });
  retains_input([](Field& f) { f.protection.hp1_defrost.started_ms = now - dsp::kDefrostHoldMs; });
  retains_input([](Field& f) { f.run.water_limit_factor = .8f; });
  retains_input([](Field& f) { f.run.base_requested_w = 0; });
  retains_input([](Field& f) { f.request_limit_w = base; });
  retains_input([](Field& f) { f.request_limit_w = 4200; });
  retains_input([](Field& f) { f.request_limit_w = 0; });
  retains_input([](Field& f) { f.request_limit_w = NAN; });
  Field nonbinding;
  nonbinding.request_limit_w = base + 1;
  assert(!nonbinding.evaluate().force_comfort_stop);
  retains_input([](Field& f) { f.raw.flow.received_ms = now - 10000; });
  retains_input([](Field& f) { f.raw.water_in.received_ms = now - 10001; });
  retains_input([](Field& f) { f.raw.water_out.received_ms = now + 1; });
  retains_input([](Field& f) { f.protection.hp1_defrost.started_ms = now; });
  retains_input([](Field& f) {
    f.protection.hp1_defrost.started_ms = now - 5000;
    f.raw.water_in.received_ms = now - 5000;
  });
  retains_input([](Field& f) {
    f.protection.hp1_defrost.started_ms = now - 5000;
    f.raw.water_out.received_ms = now - 5001;
  });
  retains_input([](Field& f) {
    f.protection.hp1_defrost.started_ms = now - 5000;
    f.raw.flow.received_ms = now - 5001;
  });
  retains_input([](Field& f) {
    f.protection.hp1_defrost.started_ms = now - 5000;
    f.raw.compressor_frequency.received_ms = now - 5001;
  });
  Field after;
  after.protection.hp1_defrost.started_ms = now - 5000;
  after.raw.flow.received_ms = now - 4999;
  assert(!after.evaluate().force_comfort_stop);

  retains_input([](Field& f) { f.run.heating_allowed = false; });
  retains_input([](Field& f) { f.run.inputs_valid = false; });
  retains_input([](Field& f) { f.previous.last_setpoint_c = 20; });
  retains_input([](Field& f) { f.previous.phase = ext::Phase::COMFORT_STOP; });
  retains_input([](Field& f) { f.run.actual_heating_active = false; });
  retains_input([](Field& f) { f.previous.cycle_armed = false; });
  retains_input([](Field& f) { f.limits.soft_limit_w = f.limits.peak_limit_w = 20; });
  retains_input([](Field& f) { f.response.missing = true; });
  retains_input([](Field& f) { f.response.settling = false; });
  retains_input([](Field& f) { f.point(1, 76); });
  retains_input([](Field& f) { f.outlet = f.inlet; });
  retains_input([](Field& f) { f.outlet = 32; });
  retains_input([](Field& f) { f.raw.working_mode.value = 0; });
  retains_input([](Field& f) { f.raw.status_2108.value = 0x40; });
  retains_input([](Field& f) { f.raw.status_2119.value = 0x08; });
  retains_input([](Field& f) { f.raw.defrost.value = 1; });
  for (auto field : {&oq_sources::HeatPumpReceipts::outside, &oq_sources::HeatPumpReceipts::water_in,
                     &oq_sources::HeatPumpReceipts::water_out, &oq_sources::HeatPumpReceipts::flow,
                     &oq_sources::HeatPumpReceipts::working_mode, &oq_sources::HeatPumpReceipts::compressor_frequency,
                     &oq_sources::HeatPumpReceipts::status_2108, &oq_sources::HeatPumpReceipts::status_2119,
                     &oq_sources::HeatPumpReceipts::defrost}) {
    retains_input([field](Field& f) { (f.raw.*field).received_ms = now - 30001; });
    retains_input([field](Field& f) { (f.raw.*field).revoke(now); });
  }
}
void test_defrost_recovery_trajectories() {
  // Same 10 s recording prefix, followed by alternative ODU responses. The real recording ends on a stop.
  for (const auto& readings : std::array<std::array<int, 6>, 4>{
           {{72, 80, 85, 85, 85, 85}, {72, 74, 76, 78, 80, 82}, {72, 72, 72, 72, 72, 72}, {72, 75, 70, 73, 69, 71}}}) {
    std::array<uint8_t, 21> nominal{};
    nominal[1] = 26;
    nominal[16] = 85;
    oq_compressor_feedback::Observer observer;
    auto previous = Field{}.previous;
    for (int s = -160; s <= 50; s += 10) {
      uint32_t t = now + s * 1000;
      bool valve = s < -100;
      int actual = s < 0 ? (s == -10 ? 66 : s == -20 ? 58 : 45) : readings[s / 10];
      auto response = observer.observe(nominal, static_cast<uint8_t>(v2),
                                       {t, 16, 2, float(actual), 3.1f, 27.68f, true, valve, false, t, now - 130000, 0});
      if (s < 0) continue;
      Field f;
      f.response = response;
      f.previous = previous;
      f.dispatch.now_ms = t;
      f.point(16, actual);
      for (auto* receipt : {&f.raw.working_mode, &f.raw.compressor_frequency, &f.raw.status_2108, &f.raw.status_2119,
                            &f.raw.defrost, &f.raw.flow, &f.raw.water_in, &f.raw.water_out, &f.raw.outside})
        receipt->received_ms = t;
      f.raw.compressor_frequency.value = actual;
      f.dispatch.hp1.feedback_hold_level = response.hold_level(16, true, true, true);
      const auto model = coupled_field(f);
      attach_model(f, model);
      const auto result = f.evaluate();
      assert(!result.force_comfort_stop && !result.floor_active && result.next.phase == ext::Phase::RUNNING);
      previous = result.next;
      f.dispatch.requested_w = f.run.base_requested_w;
      auto next = dsp::decide_dispatch(f.dispatch, f.limits, f.protection);
      assert(next.output_valid && next.hp1_level > 0 && next.hp2_level == 0);
      if (s < 50) assert(next.hp1_level == 16);
      assert(response.settling == (s < 50));
      // The earlier fast room-rise trajectory, plus reaching the configured warm edge.
      f.run.room_c = 19.51953f + .04f * (s / 10);
      assert(!f.evaluate().force_comfort_stop);
      f.run.room_c = 20.5f;
      assert(f.evaluate().force_comfort_stop);
    }
  }
}
}  // namespace
int main() {
  test_single_field_and_stops();
  test_defrost_recovery_trajectories();
}
