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
#include "openquatt/includes/control/oq_hp_supervisory_logic.h"
#include "openquatt/includes/control/oq_power_house_demand_logic.h"
#include "openquatt/includes/control/oq_power_house_dispatch_logic.h"
#include "openquatt/includes/control/oq_power_house_run_extension_logic.h"
#include "openquatt/includes/control/oq_warmup_runtime.h"

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 0.1f; }

// Real lifecycle, intent, demand, water-model and dispatch functions. The glue
// mirrors PH's two demand histories, recovery floor and final caps. No physical
// plant, coupled Duo hydraulic model, actuator timing or ESPHome callbacks.
struct Simulation {
  oq_warmup_runtime::Runtime warmup;
  oq_warmup::Settings settings{0.8f, 0.1f, 300000U};
  oq_warmup::Input room{1000U, true, true, true, 1, 1, 19.08f, 17.0f, 0.1f};
  oq_power_house::DemandTuning tuning{0, 3000, 0.1f, 0.3f, 5, 2, 20};
  oq_power_house::DemandState house_state;
  oq_heat_intent::State intent_state;
  oq_power_house_dispatch::DispatchInput dispatch;
  oq_power_house_dispatch::DispatchTuning limits{9000, 10000};
  oq_power_house_dispatch::DispatchState dispatch_state;
  oq_power_house_dispatch::DispatchDecision selected;
  float outside_c, minimum_w, fast_floor_w = 0, hp_slew_w = NAN;
  float requested_w = 0, house_w = 0, deficit_w = 0, water = 1;
  bool permission = true, permission_valid = true, room_fresh = true, goal_fresh = true, external = false;
  bool fixed = true;
  int power_cap = 20;

  Simulation(bool duo = false, float outside = 12.0f) : outside_c(outside) {
    warmup.update(room, settings);
    room.requested_c = 20;
    room.now_ms = 2000;
    warmup.update(room, settings);
    room.room_c = 19.1f;
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
    minimum_w = dispatch.hp1.levels[1].thermal_w;
    assert(std::isfinite(minimum_w) && minimum_w > 0);
  }

  void tick(uint32_t now, bool owner_first = true) {
    room.now_ms = now;
    if (owner_first) warmup.update(room, settings);
    const auto target = warmup.control_target(room.requested_c, settings.trigger_c, room.enabled, room.room_c);
    const auto intent = oq_heat_intent::evaluate(
        {now, true, permission_valid, permission, room_fresh, goal_fresh,
         dispatch.hp1.candidate.previous_applied_level > 0, 1, room.room_c, target.effective_c, tuning.comfort_below_c,
         0.2f, 10000, room.requested_c, !target.limited, target.limited},
        intent_state);
    intent_state = intent.next;
    if (dispatch.hp1.candidate.previous_applied_level > 0 && fast_floor_w > 0)
      house_state.last_w = std::max(house_state.last_w, fast_floor_w);
    fast_floor_w = 0;
    if (target.limited) house_state.comfort_memory_c = 0;
    const auto previous = house_state;
    const oq_power_house::DemandInput input{
        now, outside_c, -10, 18, 5000, room.room_c, target.effective_c, external ? 1200.0f : NAN, water, external};
    const auto base = oq_power_house::decide_demand(input, tuning, previous);
    house_state = base.next;
    if (target.limited) house_state.comfort_memory_c = 0;
    requested_w = house_w = base.requested_w;
    const bool floor = (intent.fast_start || intent.room_recovery_active) && water >= 0.999f;
    if (fixed && target.limited && !base.external && dispatch.performance_valid && floor &&
        ((std::isfinite(target.boost_baseline_c) && target.effective_c > target.boost_baseline_c) ||
         std::isfinite(hp_slew_w))) {
      auto hp_state = previous;
      if (std::isfinite(hp_slew_w)) hp_state.last_w = hp_slew_w;
      const auto hp = oq_power_house::decide_demand(input, tuning, hp_state, {target.boost_baseline_c, minimum_w});
      assert(hp.valid);
      requested_w = hp.requested_w;
      hp_slew_w = requested_w;
    } else
      hp_slew_w = NAN;
    if (floor) {
      requested_w = std::max(requested_w, minimum_w);
      house_w = std::max(house_w, minimum_w);
      fast_floor_w = house_w;
      if (std::isfinite(hp_slew_w)) hp_slew_w = requested_w;
    }
    dispatch.now_ms = now;
    dispatch.requested_w = oq_power_house::power_capped_request_w(requested_w, power_cap, 20, 5000);
    dispatch.demand_level = std::min(power_cap, static_cast<int>(std::ceil(dispatch.requested_w * 20 / 5000)));
    selected = oq_power_house_dispatch::decide_dispatch(dispatch, limits, dispatch_state);
    const float capped_house = oq_power_house::power_capped_request_w(house_w, power_cap, 20, 5000);
    deficit_w = oq_power_house_run_extension::compute_house_deficit_w(
        capped_house, selected.capacity_w, selected.output_valid, std::isfinite(hp_slew_w) ? NAN : selected.deficit_w);
    if (!oq_hp_supervisory::apply_heating_enable_gate(true, permission_valid, permission)) {
      selected.hp1_level = selected.hp2_level = 0;
    }
    dispatch.hp1.candidate.previous_applied_level = selected.hp1_level;
    dispatch.hp2.candidate.previous_applied_level = selected.hp2_level;
  }
  unsigned hz() const { return dispatch.hp1.levels[selected.hp1_level].frequency_hz; }
  void boost(unsigned count) {
    for (unsigned step = 0; step <= count; ++step) tick(2000U + step * settings.step_ms);
  }
};

void test_boost_adds_power_and_dispatches() {
  for (bool duo : {false, true})
    for (float outside : {12.0f, 8.0f}) {
      Simulation sim(duo, outside), legacy(duo, outside);
      legacy.fixed = false;
      for (unsigned step = 0; step <= 5; ++step) {
        const auto now = 2000U + step * sim.settings.step_ms;
        sim.tick(now);
        legacy.tick(now);
        assert(near(sim.house_w, legacy.house_w));
        assert(near(sim.house_state.last_w, legacy.house_state.last_w));
        assert(near(sim.requested_w, sim.minimum_w + std::max(0.0f, 3000.0f * (step * 0.1f - 0.02f))));
        if (step >= 3) assert(sim.hz() > 30);
        if (outside == 12 && step == 3) {
          assert(near(sim.requested_w, 3367.63f));
          assert(legacy.hz() == 30 && near(legacy.requested_w, sim.minimum_w));
        }
        std::printf("%s Tout=%.0f boost=%.1f house=%.0f HP=%.0f -> %u Hz\n", duo ? "Duo/one HP" : "Single", outside,
                    sim.warmup.offset(), sim.house_w, sim.requested_w, sim.hz());
      }
    }
}

void test_step_reset_both_orderings_slew_and_no_boiler_feedback() {
  for (bool duo : {false, true})
    for (bool owner_first : {false, true}) {
      Simulation sim(duo), legacy(duo);
      legacy.fixed = false;
      // Restrict HP capacity to Pmin: the new lift must not increase house deficit.
      for (int level = 2; level <= 10; ++level)
        sim.dispatch.hp1.levels[level].allowed = legacy.dispatch.hp1.levels[level].allowed = false;
      sim.boost(5);
      legacy.boost(5);
      assert(sim.selected.deficit_w > 1400 && near(sim.deficit_w, legacy.deficit_w));
      sim.room.room_c = legacy.room.room_c = sim.warmup.intermediate_target();
      uint32_t now = sim.room.now_ms;
      float previous_w = sim.requested_w;
      for (int tick = 1; tick <= 48; ++tick) {
        now += 5000;
        sim.tick(now, tick == 1 ? owner_first : true);
        legacy.tick(now, tick == 1 ? owner_first : true);
        const auto target =
            sim.warmup.control_target(sim.room.requested_c, sim.settings.trigger_c, true, sim.room.room_c);
        assert(!std::isfinite(target.boost_baseline_c) || target.effective_c <= target.boost_baseline_c + 0.0001f);
        assert(sim.requested_w <= previous_w + 0.1f);
        assert(previous_w - sim.requested_w <= 5000.0f * 5 / 120 + 0.1f);
        assert(sim.selected.hp1_level > 0 && sim.selected.hp2_level == 0);
        assert(near(sim.house_w, legacy.house_w) && near(sim.house_state.last_w, legacy.house_state.last_w));
        assert(near(sim.deficit_w, legacy.deficit_w));
        previous_w = sim.requested_w;
        if (tick == 1 && !owner_first) {
          // Read-only consumer suppressed old boost before owner state changed.
          assert(sim.warmup.offset() > 0);
          sim.warmup.update(sim.room, sim.settings);
          legacy.warmup.update(legacy.room, legacy.settings);
        }
      }
      assert(sim.warmup.offset() == 0 && near(sim.requested_w, sim.minimum_w));
    }
}

void test_guards_cancel_and_new_session() {
  for (int boundary = 0; boundary < 12; ++boundary) {
    Simulation sim;
    sim.boost(3);
    assert(sim.hz() > 30 && std::isfinite(sim.hp_slew_w));
    if (boundary == 0) sim.water = 0;
    if (boundary == 1) sim.water = 0.5f;
    if (boundary == 2) sim.permission = false;
    if (boundary == 3) sim.permission_valid = false;
    if (boundary == 4) sim.room_fresh = false;
    if (boundary == 5) sim.goal_fresh = false;
    if (boundary == 6) sim.dispatch.performance_valid = false;
    if (boundary == 7) sim.room.enabled = false;
    if (boundary == 8) sim.warmup.invalidate(oq_warmup::Status::SETTINGS_CHANGED);
    if (boundary == 9) sim.room.requested_c = 19.5f;
    if (boundary == 10) sim.external = true;
    if (boundary == 11) sim.warmup = {};  // No restored session/scalar on reboot.
    auto reference = sim;
    reference.fixed = false;
    reference.hp_slew_w = NAN;
    reference.tick(sim.room.now_ms + 5000, false);
    sim.tick(sim.room.now_ms + 5000, false);
    assert(!std::isfinite(sim.hp_slew_w));
    assert(near(sim.requested_w, reference.requested_w));
    if (boundary == 0) assert(sim.requested_w == 0 && sim.selected.hp1_level == 0);
    if (boundary == 1) assert(sim.requested_w < sim.minimum_w);
    if (boundary == 2 || boundary == 3) assert(sim.selected.hp1_level == 0);
    if (boundary == 6) assert(!sim.selected.output_valid);  // No invented model/frequency increase.
    // Re-enable through a fresh baseline and raise; no previous boost resumes.
    sim.warmup = {};
    sim.room.enabled = true;
    sim.room.requested_c = 17;
    sim.warmup.update(sim.room, sim.settings);
    sim.room.requested_c = 20;
    sim.water = 1;
    sim.permission = sim.permission_valid = true;
    sim.room_fresh = sim.goal_fresh = true;
    sim.external = false;
    sim.dispatch.performance_valid = true;
    sim.tick(sim.room.now_ms + 5000);
    assert(sim.warmup.active() && sim.warmup.offset() == 0 && !std::isfinite(sim.hp_slew_w));
  }
  for (bool duo : {false, true}) {
    Simulation sim(duo);
    sim.boost(5);
    sim.dispatch.hp1.candidate.must_stop = true;
    sim.tick(sim.room.now_ms + 5000);
    assert(sim.selected.hp1_level == 0);
    sim.dispatch.hp1.candidate = {0, true, false, false, false};
    sim.tick(sim.room.now_ms + 5000);
    assert(sim.selected.hp1_level == 0 && !std::isfinite(sim.hp_slew_w));
  }
}

void test_caps_and_comfort_limit() {
  Simulation cap;
  cap.boost(5);
  cap.power_cap = 0;
  cap.tick(cap.room.now_ms + 5000);
  assert(cap.dispatch.requested_w == 0 && cap.selected.hp1_level == 0);
  Simulation electrical;
  electrical.boost(5);
  const float minimum_electrical = electrical.dispatch.hp1.levels[1].electrical_w;
  electrical.limits = {minimum_electrical, minimum_electrical};
  electrical.tick(electrical.room.now_ms + 5000);
  assert(electrical.selected.hp1_level <= 1);
  Simulation frequency;
  frequency.boost(5);
  for (int level = 2; level <= 10; ++level) frequency.dispatch.hp1.levels[level].allowed = false;
  frequency.tick(frequency.room.now_ms + 5000);
  assert(frequency.hz() == 30);
  Simulation held;
  held.boost(3);
  held.dispatch.hp1.fixed_level = 1;
  held.dispatch.hp1.candidate.previous_applied_level = 1;
  held.dispatch.hp1.stop_inhibited = true;
  held.tick(held.room.now_ms + 5000);
  assert(held.hz() == 30);  // Actuator's startup/protection hold wins over boost.
  Simulation duo(true);
  duo.dispatch.hp2 = duo.dispatch.hp1;
  duo.dispatch.hp2.candidate = {0, true, false, false, false};
  duo.boost(5);
  assert(duo.selected.hp1_level > 1 && duo.selected.hp2_level == 0);
  Simulation final;
  final.boost(5);
  // Raising a step close to the real goal clamps its boost contribution.
  final.room.room_c = 19.85f;
  final.tick(final.room.now_ms + 5000);
  final.tick(final.room.now_ms + final.settings.step_ms);
  const auto target = final.warmup.control_target(20, final.settings.trigger_c, true, final.room.room_c);
  assert(target.effective_c == 20 && target.boost_baseline_c > 19.94f);
  assert(final.requested_w <= final.minimum_w + 151);  // <= Kp * remaining 0.05 C.
  final.room.room_c = 20;
  final.tick(final.room.now_ms + 5000, false);
  assert(!std::isfinite(final.hp_slew_w) && !final.intent_state.room_recovery_active);
}

void test_repeated_ticks_do_not_accumulate_boost_or_house_deficit() {
  Simulation sim, legacy;
  legacy.fixed = false;
  sim.boost(3);
  legacy.boost(3);
  for (int tick = 0; tick < 40; ++tick) {
    const uint32_t now = sim.room.now_ms + 5000;
    sim.tick(now);
    legacy.tick(now);
    assert(near(sim.requested_w, 3367.63f));
    assert(near(sim.house_w, legacy.house_w) && near(sim.house_state.last_w, legacy.house_state.last_w));
    assert(near(sim.deficit_w, legacy.deficit_w));
  }
  // Cancellation cannot leak the HP-only history into ordinary demand.
  sim.room.enabled = legacy.room.enabled = false;
  sim.tick(sim.room.now_ms + 5000);
  legacy.tick(legacy.room.now_ms + 5000);
  assert(!std::isfinite(sim.hp_slew_w) && near(sim.requested_w, legacy.requested_w));
  assert(near(sim.house_state.last_w, legacy.house_state.last_w));
}

void test_composition_envelope_slew_and_no_boost_compatibility() {
  using namespace oq_power_house;
  const DemandTuning tuning{0, 3000, 0.1f, 0.3f, 5, 2, 20};
  DemandInput input{6000, 12, -10, 18, 5000, 19.1f, 19.48f, NAN, 1, false};
  const WarmupBoost boost{19.18f, 2527.63f};
  auto out = decide_demand(input, tuning, {2527.63f, 1000, 0}, boost);
  assert(near(out.requested_w, 2527.63f + 5000.0f * 5 / 300));
  assert(near(out.contributions.warmup_base_w, 1071.43f));
  assert(near(out.contributions.warmup_extra_w, 840));
  out = decide_demand(input, tuning, {}, {19.18f, 9000});
  assert(near(out.requested_w, 9840.0f) && out.raw_demand == 20);
  for (float water : {0.0f, 0.5f, 0.998f}) {
    input.water_limit_factor = water;
    assert(near(decide_demand(input, tuning, {}, boost).requested_w, decide_demand(input, tuning, {}).requested_w));
  }
  input.water_limit_factor = 1;
  for (WarmupBoost invalid : {WarmupBoost{}, {NAN, 2528}, {19.18f, NAN}, {19.18f, 0}, {20, 2528}})
    assert(near(decide_demand(input, tuning, {}, invalid).requested_w, decide_demand(input, tuning, {}).requested_w));
  // Above Pmin the normal feedback already supplies the extra demand: do not
  // add the boost a second time on top of that boosted request.
  input.outside_c = -5;
  const auto without_extra_floor = decide_demand(input, tuning, {});
  assert(without_extra_floor.requested_w > boost.minimum_w);
  assert(near(decide_demand(input, tuning, {}, boost).requested_w, without_extra_floor.requested_w));
  input.room_c = NAN;
  assert(!decide_demand(input, tuning, {}, boost).valid);
}
}  // namespace

int main() {
  test_boost_adds_power_and_dispatches();
  test_step_reset_both_orderings_slew_and_no_boiler_feedback();
  test_guards_cancel_and_new_session();
  test_caps_and_comfort_limit();
  test_repeated_ticks_do_not_accumulate_boost_or_house_deficit();
  test_composition_envelope_slew_and_no_boost_compatibility();
}
