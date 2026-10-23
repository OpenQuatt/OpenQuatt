#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V1"; }
} hp_generation;
#define id(value) value
#include "openquatt/includes/performance/oq_water_performance.h"
#undef id
#include "openquatt/includes/control/oq_power_house_demand_logic.h"
#include "openquatt/includes/control/oq_power_house_dispatch_logic.h"
#include "openquatt/includes/control/oq_warmup_runtime.h"

// Illustrative inputs, not a replay of the tester's installation: its warmup
// settings, pre-floor demand and water-model inputs were not recorded together.
// Compile the real warmup, demand, water model and dispatcher. The existing
// runtime's Pmin max() is applied explicitly; physical timing remains HIL scope.
void check_boost_path(bool duo, float outside_c) {
  using namespace oq_power_house_dispatch;
  oq_warmup_runtime::Runtime warmup;
  const oq_warmup::Settings settings{0.8f, 0.1f, 300000U};
  oq_warmup::Input room{1000U, true, true, true, 1, 1, 19.08f, 17.0f, 0.1f};
  warmup.update(room, settings);
  room.requested_c = 20.0f;
  room.now_ms = 2000U;
  warmup.update(room, settings);
  assert(warmup.active());
  assert(std::fabs(warmup.intermediate_target() - 19.18f) < 0.0001f);
  room.room_c = 19.1f;

  DispatchInput dispatch;
  dispatch.duo = duo;
  dispatch.performance_valid = true;
  dispatch.hp1.candidate = {1, true, false, false, true};
  dispatch.hp2.candidate = {0, false, false, false, true};
  for (int level = 1; level <= 10; ++level) {
    const float hz = oq_perf::V1_HEATING_FREQUENCIES_HZ[level - 1];
    const auto power = oq_water_performance::solve(oq_odu::Variant::V1, hz, outside_c, 30.0f, 800.0f);
    dispatch.hp1.levels[level] = {power.valid(),   power.valid(),      power.valid(),
                                  power.thermal_w, power.electrical_w, static_cast<uint8_t>(hz)};
  }
  assert(dispatch.hp1.levels[1].thermal_valid && dispatch.hp1.levels[2].thermal_valid);
  const float minimum_w = dispatch.hp1.levels[1].thermal_w;
  const oq_power_house::DemandTuning tuning{0, 3000, 0.1f, 0.3f, 5, 2, 20};
  oq_power_house::DemandInput input{0, outside_c, -10, 18, 5000, room.room_c, NAN, NAN, 1, false};
  auto demand_for_target = [&]() {
    const auto target = warmup.control_target(room.requested_c, settings.trigger_c, true, room.room_c);
    assert(target.limited);
    input.now_ms = room.now_ms;
    input.setpoint_c = target.effective_c;
    return oq_power_house::decide_demand(input, tuning, {});
  };
  auto select = [&](float watts) {
    dispatch.now_ms = room.now_ms;
    dispatch.requested_w = watts;
    dispatch.demand_level = static_cast<int>(std::ceil(watts * 20.0f / 5000.0f));
    return decide_dispatch(dispatch, {9000, 10000}, {});
  };
  for (unsigned step = 0; step <= 5; ++step) {
    room.now_ms = 2000U + step * settings.step_ms;
    warmup.update(room, settings);
    assert(std::fabs(warmup.offset() - step * 0.1f) < 0.0001f);
    assert(std::fabs(warmup.intermediate_target() - 19.18f) < 0.0001f);
    const auto demand = demand_for_target();
    assert(demand.valid);
    const float expected_feedback = 3000.0f * std::max(0.0f, 19.18f + step * 0.1f - 0.1f - room.room_c);
    assert(std::fabs(demand.contributions.room_feedback_w - expected_feedback) < 0.1f);
    const auto decision = select(std::max(demand.requested_w, minimum_w));
    assert(decision.output_valid && decision.hp1_level > 0 && decision.hp2_level == 0);
    const auto hz = dispatch.hp1.levels[decision.hp1_level].frequency_hz;
    if (outside_c == 12.0f) {
      assert(demand.requested_w < minimum_w && hz == 30);
      if (step == 3) assert(std::fabs(demand.requested_w - 1911.43f) < 1.0f);
    } else {
      if (step == 2) assert(demand.requested_w > minimum_w && hz == 30);
      if (step == 5) assert(hz > 30);
    }
    std::printf("%s Tout=%.0f boost=%.1f: feedback=%.0f demand=%.0f Pmin=%.0f dispatch=%.0f -> %u Hz\n",
                duo ? "Duo/one available HP" : "Single", outside_c, warmup.offset(),
                demand.contributions.room_feedback_w, demand.requested_w, minimum_w, dispatch.requested_w, hz);
  }

  // A reached intermediate target clears boost; it does not itself ask for a
  // frequency burst. Check this independently of the previous demand slew state.
  room.room_c = warmup.intermediate_target();
  ++room.now_ms;
  warmup.update(room, settings);
  input.room_c = room.room_c;
  assert(warmup.offset() == 0.0f);
  assert(std::fabs(warmup.intermediate_target() - (room.room_c + settings.step_c)) < 0.0001f);
  const auto advanced = demand_for_target();
  assert(advanced.valid && std::fabs(advanced.contributions.room_feedback_w) < 0.1f);
  assert(dispatch.hp1.levels[select(std::max(advanced.requested_w, minimum_w)).hp1_level].frequency_hz == 30);

  // Even a request above the next stand cannot bypass an incident stop or
  // minimum-off permission. Missing performance data only retains a running HP.
  dispatch.hp1.candidate.must_stop = true;
  assert(select(4000).hp1_level == 0);
  dispatch.hp1.candidate = {0, true, false, false, false};
  assert(select(4000).hp1_level == 0);
  dispatch.hp1.candidate = {1, true, false, false, true};
  dispatch.performance_valid = false;
  const auto held = select(4000);
  assert(held.hp1_level == 1 && !held.output_valid);
}

int main() {
  for (bool duo : {false, true})
    for (float outside_c : {12.0f, 8.0f}) check_boost_path(duo, outside_c);
}
