#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "openquatt/includes/control/oq_hp_supervisory_logic.h"
#include "openquatt/includes/control/oq_power_house_demand_logic.h"
#include "openquatt/includes/control/oq_power_house_run_extension_logic.h"
#include "openquatt/includes/control/oq_supervisory_state_logic.h"
#include "openquatt/includes/control/oq_warmup_runtime.h"

namespace {
struct Sensor {
  float state = 0;
  bool present = true;
  bool has_state() const { return present; }
};
struct Source {
  std::string option = "API input";
  bool has_state() const { return true; }
  const std::string& current_option() const { return option; }
};
struct Entities {
  Source room_temp_source, room_setpoint_source;
  Sensor room_temp_selected, room_setpoint_selected;
  Sensor room_temp_valid_ha, thermostat_room_temp_ha, room_setpoint_valid_ha, thermostat_setpoint_ha;
  Sensor ot_thermostat_room_temp, ot_thermostat_room_setpoint;
  Sensor feed_ok, cic_data_stale, cic_room_temp, cic_room_setpoint;
  Sensor api_input_room_temperature_valid{1}, api_input_room_temperature;
  Sensor api_input_room_setpoint_valid{1}, api_input_room_setpoint;
  Sensor mqtt_room_temperature_valid, mqtt_room_temperature, mqtt_room_setpoint_valid, mqtt_room_setpoint;
  Sensor heating_enable_valid{1}, heating_enable_selected{1};
  bool oq_room_temp_selected_hold_active = false, oq_room_setpoint_selected_hold_active = false;
} entities;
}  // namespace

#define id(name) entities.name
#include "openquatt/includes/control/oq_heat_intent_runtime.h"
#undef id

namespace {
constexpr float kMinimumW = 2527.63f;

// Exercise the actual lifecycle, input adapter, intent, demand and low-load
// helpers. The existing PH minimum-power glue is reproduced below; dispatch,
// minimum on/off timers and physical compressor feedback remain HIL scope.
struct Simulation {
  oq_warmup_runtime::Runtime warmup;
  oq_warmup::Settings settings;
  oq_warmup::Input input{1000, true, true, true, 1, 1, 20.05859375f, 18.0f, 0.1f};
  oq_heat_intent::State intent_state;
  oq_power_house::DemandState demand_state;
  oq_supervisory_state::LowLoadState low_load_state;
  bool compressor = false;
  int starts = 0, stops = 0;
  float fast_floor_w = 0;
  float requested_w = 0;
  float water_factor = 1;

  Simulation(float room = 20.05859375f, float below = 0.1f, float step = 0.1f) {
    entities = {};
    input.room_c = room;
    input.comfort_below_c = below;
    settings.step_c = step;
    warmup.update(input, settings);
  }

  oq_heat_intent::Decision intent(uint32_t now, float room, float requested, bool owner_first = true,
                                  bool fixed = true) {
    input.now_ms = now;
    input.room_c = room;
    input.requested_c = requested;
    if (owner_first) warmup.update(input, settings);
    entities.room_temp_selected.state = entities.api_input_room_temperature.state = room;
    entities.room_setpoint_selected.state = entities.api_input_room_setpoint.state = requested;
    const auto target = warmup.control_target(requested, settings.trigger_c, input.enabled);
    const auto decision = oq_heat_intent_runtime::evaluate(now, compressor, input.comfort_below_c, 10000, false, false,
                                                           intent_state, target.effective_c, fixed && target.limited);
    intent_state = decision.next;
    assert(!decision.setpoint_raise_edge);
    return decision;
  }

  oq_heat_intent::Decision tick(uint32_t now, float room, float requested = 22, bool owner_first = true,
                                bool fixed = true) {
    const auto decision = intent(now, room, requested, owner_first, fixed);
    const auto target = warmup.control_target(requested, settings.trigger_c, input.enabled);
    if (compressor && fast_floor_w > 0) demand_state.last_w = std::max(demand_state.last_w, fast_floor_w);
    if (target.limited) demand_state.comfort_memory_c = 0;
    const oq_power_house::DemandInput demand_input{now, 19.62f,       -10,  18, 10000, room, target.effective_c,
                                                   NAN, water_factor, false};
    const oq_power_house::DemandTuning tuning{0, 2000, input.comfort_below_c, 0.3f, 5, 2, 20};
    const auto demand = oq_power_house::decide_demand(demand_input, tuning, demand_state);
    assert(demand.valid && demand.contributions.modelled_base_w == 0);
    demand_state = demand.next;
    if (target.limited) demand_state.comfort_memory_c = 0;
    requested_w = demand.requested_w;
    if ((decision.fast_start || decision.room_recovery_active) && water_factor >= 0.999f) {
      requested_w = std::max(requested_w, kMinimumW);
      fast_floor_w = requested_w;
    } else if (!compressor) {
      fast_floor_w = 0;
    }
    const auto low_load = oq_supervisory_state::update_low_load(
        {now, true, requested_w > 0, true, requested_w, kMinimumW, 0.75f, 1, 200, 900, 1300, 600000}, low_load_state);
    low_load_state = low_load.state;
    const bool next_compressor = oq_hp_supervisory::apply_heating_enable_gate(
        low_load.heating_request, entities.heating_enable_valid.state, entities.heating_enable_selected.state);
    if (next_compressor && !compressor) ++starts;
    if (!next_compressor && compressor) ++stops;
    compressor = next_compressor;
    return decision;
  }
};

void test_tester_trace_before_and_after() {
  for (bool fixed : {false, true}) {
    Simulation sim;
    sim.tick(2000, 20.05859375f, 22, true, fixed);
    assert(!sim.compressor);  // Normal 10s start confirmation, no start bypass.
    sim.tick(62000, 20.05859375f, 22, true, fixed);
    assert(sim.compressor);
    sim.tick(122000, 20.05859375f, 22, true, fixed);
    sim.tick(182000, 20.09765625f, 22, true, fixed);
    const auto release = sim.tick(242000, 20.109375f, 22, true, fixed);
    assert(sim.warmup.active());
    assert(std::fabs(sim.warmup.effective_target() - 20.15859375f) < 0.00001f);
    assert(release.room_recovery_active == fixed);
    assert(sim.compressor == fixed);
    sim.tick(302000, 20.16796875f, 22, true, fixed);
    sim.tick(362000, 20.16796875f, 22, true, fixed);
    assert(std::fabs(sim.warmup.effective_target() - 20.26796875f) < 0.00001f);
    assert(sim.compressor);
    assert(sim.starts == (fixed ? 1 : 2));
    assert(sim.stops == (fixed ? 0 : 1));
    std::printf("tester trace %s: starts=%d stops=%d\n", fixed ? "fixed" : "ordinary recovery", sim.starts, sim.stops);
  }
}

void test_existing_run_and_both_owner_orderings() {
  for (bool owner_first : {false, true}) {
    Simulation sim;
    sim.compressor = true;
    sim.low_load_state.heat_latched = true;
    // Pending raise still exposes the old 18 C goal before the lifecycle tick.
    auto decision = sim.tick(2000, 20.05859375f, 22, owner_first);
    assert(decision.room_recovery_active && sim.compressor && sim.stops == 0);
    sim.warmup.update(sim.input, sim.settings);
    sim.tick(62000, 20.109375f);
    decision = sim.tick(122000, 20.16796875f, 22, owner_first);
    assert(decision.room_recovery_active && sim.compressor && sim.stops == 0);
    sim.warmup.update(sim.input, sim.settings);
    assert(std::fabs(sim.warmup.effective_target() - 20.26796875f) < 0.00001f);
    // Comfort releases even if PH sees the last intermediate target first.
    const float comfort = 22.0f - sim.input.comfort_below_c;
    decision = sim.tick(182000, comfort, 22, owner_first);
    assert(!decision.room_recovery_active && !decision.fast_start && !sim.compressor);
    sim.warmup.update(sim.input, sim.settings);
    assert(!sim.warmup.active());
  }
}

void test_step_comfort_matrix_and_noise() {
  int scenarios = 0;
  for (float step : {0.1f, 0.2f, 0.5f}) {
    for (float below : {0.0f, 0.05f, 0.1f, 0.2f, 0.5f, 2.0f}) {
      Simulation sim(17.03125f, below, step);
      sim.tick(2000, sim.input.room_c);
      sim.tick(62000, sim.input.room_c);
      assert(sim.compressor);
      uint32_t now = 122000;
      const float comfort = 22 - below;
      int steps = 0;
      for (float room = 17.03125f; room < comfort; room += 0.03125f) {
        const float old_target = sim.warmup.effective_target();
        sim.tick(now, room);
        assert(sim.warmup.active() && sim.compressor && sim.starts == 1 && sim.stops == 0);
        if (sim.warmup.effective_target() != old_target) ++steps;
        now += 60000;
        // A small cooling/noise sample must not drop recovery or move target back.
        const float target = sim.warmup.effective_target();
        sim.tick(now, room - 0.00390625f);
        assert(sim.compressor && sim.warmup.effective_target() >= target);
        now += 60000;
      }
      assert(steps > 1);
      sim.tick(now, comfort);
      assert(!sim.warmup.active() && !sim.intent_state.room_recovery_active);
      assert(sim.starts == 1 && sim.stops == 1);
      ++scenarios;
    }
  }
  std::printf("step/comfort/noise matrix: %d scenarios passed\n", scenarios);
}

void test_permission_freshness_water_limit_and_invalid_goal() {
  for (int boundary = 0; boundary < 6; ++boundary) {
    Simulation sim;
    sim.tick(2000, sim.input.room_c);
    sim.tick(62000, sim.input.room_c);
    sim.tick(122000, 20.109375f);
    assert(sim.intent_state.room_recovery_active);
    if (boundary == 0) entities.heating_enable_selected.state = 0;
    if (boundary == 1) entities.heating_enable_valid.state = 0;
    if (boundary == 2) entities.api_input_room_temperature_valid.state = 0;
    if (boundary == 3) entities.api_input_room_setpoint_valid.state = 0;
    if (boundary == 4) sim.water_factor = 0;
    if (boundary == 5) entities.room_temp_source.option = "Unknown source";
    const auto decision = sim.tick(182000, 20.109375f);
    assert(!sim.compressor && sim.requested_w == 0);
    if (boundary != 4) assert(!decision.active && !decision.next.initialized);
  }
  Simulation sim;
  sim.tick(2000, sim.input.room_c);
  sim.tick(62000, sim.input.room_c);
  for (float goal : {NAN, 0.0f, 19.0f}) {
    oq_heat_intent::Input in{122000, true, true, true, true, true, true, 1, 20, 20.1f, 0.1f, 0.2f, 10000};
    in.allow_setpoint_raise = false;
    in.controlled_warmup = true;
    in.requested_setpoint_c = goal;
    const auto decision = oq_heat_intent::evaluate(in, sim.intent_state);
    assert(!decision.active && !decision.next.initialized);
  }
}

void test_cancellation_and_return_to_normal() {
  for (int boundary = 0; boundary < 7; ++boundary) {
    Simulation sim;
    sim.tick(2000, sim.input.room_c);
    sim.tick(62000, sim.input.room_c);
    sim.tick(122000, 20.109375f);
    assert(sim.intent_state.room_recovery_active);
    float goal = 22;
    uint32_t now = 182000;
    if (boundary == 0) goal = 21.5f;  // Lower before the owner; no old warmup floor.
    if (boundary == 1) sim.input.enabled = false;
    if (boundary == 2) sim.warmup.invalidate(oq_warmup::Status::SOURCE_CHANGED);
    if (boundary == 3) sim.warmup.invalidate(oq_warmup::Status::MODE_CHANGED);
    if (boundary == 4) sim.warmup.invalidate(oq_warmup::Status::SETTINGS_CHANGED);
    if (boundary == 5) sim.warmup = {};  // Reboot: no session or old recovery survives.
    if (boundary == 6) {
      now = 2000 + oq_warmup::MAX_DURATION_MS;
      sim.input.now_ms = now;
      sim.warmup.update(sim.input, sim.settings);
    }
    const auto decision = sim.intent(now, 20.109375f, goal, false);
    assert(!decision.room_recovery_active && !decision.next.controlled_warmup);
    assert(!decision.setpoint_raise_edge && !decision.fast_start);
    sim.warmup.update(sim.input, sim.settings);
    assert(!sim.warmup.active());
  }
}

void test_interrupted_run_requires_a_new_confirmed_start() {
  for (bool owner_first : {false, true}) {
    for (bool crossed_target : {false, true}) {
      Simulation sim;
      sim.tick(2000, sim.input.room_c);
      sim.tick(62000, sim.input.room_c);
      sim.tick(122000, 20.109375f);
      assert(sim.intent_state.room_recovery_active);
      // Model an independently stopped output after a water/dispatch guard.
      sim.compressor = false;
      sim.low_load_state.heat_latched = false;
      const float room = crossed_target ? 20.16796875f : 20.109375f;
      auto decision = sim.intent(182000, room, 22, owner_first);
      assert(!decision.active && !decision.fast_start && !decision.room_recovery_active);
      if (crossed_target && !owner_first) {
        decision = sim.intent(242000, room, 22, false);
        assert(!decision.active);  // Old target reached: no inherited run right.
      }
      sim.warmup.update(sim.input, sim.settings);
      decision = sim.intent(302000, room, 22);
      if (crossed_target && !owner_first) assert(!decision.fast_start);
      decision = sim.intent(312000, room, 22);
      assert(decision.fast_start && !decision.room_recovery_active);
      sim.compressor = true;
      decision = sim.intent(322000, room, 22);
      assert(decision.room_recovery_active && !decision.fast_start);
    }
  }

  Simulation sim;
  sim.tick(2000, sim.input.room_c);
  sim.tick(62000, sim.input.room_c);
  sim.tick(122000, 20.109375f);
  sim.water_factor = 0;
  sim.tick(182000, 20.109375f);
  assert(!sim.compressor && sim.requested_w == 0);
  sim.water_factor = 1;
  auto decision = sim.tick(242000, 20.109375f);
  assert(!decision.fast_start && !decision.room_recovery_active && !sim.compressor && sim.requested_w == 0);
  decision = sim.tick(302000, 20.109375f);
  assert(decision.fast_start && sim.compressor && sim.starts == 2);
}

void test_warmup_handoff_to_adjustable_run_extension() {
  namespace extension = oq_power_house_run_extension;
  // A 0.1 C step is smaller than the 0.2 C cold comfort margin. The
  // extension thresholds must still follow the real 22 C thermostat goal.
  for (float cooldown : {0.2f, 0.7f, 0.9f, 1.2f, 3.0f}) {
    Simulation sim(20.0f, 0.2f);
    sim.tick(2000, 20.0f);
    sim.tick(62000, 20.0f);
    assert(sim.compressor);
    extension::State state;
    const extension::Tuning tuning{0.7f, cooldown, sim.input.comfort_below_c};
    extension::Input in{true, true, true, true, true, 20.0f, 22.0f, sim.requested_w, kMinimumW, 1.0f};
    auto result = extension::evaluate(in, tuning, state);
    state = result.next;
    for (float room : {20.05f, 20.1f, 20.2f, 21.0f}) {
      const auto intent = sim.tick(sim.input.now_ms + 60000, room);
      assert(intent.room_recovery_active && sim.compressor);
      in.room_c = room;
      in.base_requested_w = sim.requested_w;
      result = extension::evaluate(in, tuning, state);
      state = result.next;
      assert(!result.force_comfort_stop && !result.warm_restart_intent);
      assert(result.comfort_stop_c == 22.7f);
    }
    sim.tick(sim.input.now_ms + 60000, 21.8f);
    assert(!sim.warmup.active() && !sim.intent_state.room_recovery_active);
    in.room_c = 21.8f;
    in.base_requested_w = 0;
    result = extension::evaluate(in, tuning, state);
    assert(result.floor_active && !result.force_comfort_stop);
    in.room_c = result.comfort_stop_c;
    result = extension::evaluate(in, tuning, result.next);
    assert(result.force_comfort_stop);
    in.cycle_active = in.actual_heating_active = false;
    result = extension::evaluate(in, tuning, result.next);
    assert(result.next.phase == extension::Phase::WAIT_WARM_RESTART);
    const float restart = std::max(22.7f - cooldown, 21.8f);
    assert(std::fabs(result.warm_restart_c - restart) < 0.00001f);
    in.base_requested_w = 100;
    in.room_c = restart + 0.01f;
    result = extension::evaluate(in, tuning, result.next);
    assert(result.force_comfort_stop && !result.warm_restart_intent);
    in.room_c = restart;
    result = extension::evaluate(in, tuning, result.next);
    assert(result.warm_restart_intent && result.floor_active);
    in.heating_allowed = false;
    result = extension::evaluate(in, tuning, result.next);
    assert(!result.warm_restart_intent && !result.floor_active && !result.next.cycle_armed);
  }
}

void test_warmup_never_fabricates_a_thermostat_raise() {
  oq_heat_intent::Input in{1000, true, true, true, true, true, false, 1, 20, 20.1f, 0.1f, 0.2f, 10000};
  in.requested_setpoint_c = 18;
  auto state = oq_heat_intent::evaluate(in, {}).next;
  in.controlled_warmup = true;
  in.requested_setpoint_c = 22;
  in.now_ms = 2000;
  auto decision = oq_heat_intent::evaluate(in, state);
  assert(!decision.setpoint_raise_edge && !decision.fast_start);
  in.setpoint_c = 20.5f;
  in.now_ms = 3000;
  decision = oq_heat_intent::evaluate(in, decision.next);
  assert(!decision.setpoint_raise_edge);
  in.controlled_warmup = false;
  in.setpoint_c = 22;
  in.now_ms = 4000;
  decision = oq_heat_intent::evaluate(in, decision.next);
  assert(!decision.setpoint_raise_edge && !decision.room_recovery_active);
  // A genuinely new user raise still works after the session ends.
  in.requested_setpoint_c = in.setpoint_c = 23;
  in.now_ms = 5000;
  decision = oq_heat_intent::evaluate(in, decision.next);
  assert(decision.setpoint_raise_edge && decision.reason == oq_heat_intent::SETPOINT_RAISE);
}
}  // namespace

int main() {
  test_tester_trace_before_and_after();
  test_existing_run_and_both_owner_orderings();
  test_step_comfort_matrix_and_noise();
  test_permission_freshness_water_limit_and_invalid_goal();
  test_cancellation_and_return_to_normal();
  test_interrupted_run_requires_a_new_confirmed_start();
  test_warmup_handoff_to_adjustable_run_extension();
  test_warmup_never_fabricates_a_thermostat_raise();
  std::printf("warmup intent state: %zu B; all boundary assertions passed\n", sizeof(oq_heat_intent::State));
}
