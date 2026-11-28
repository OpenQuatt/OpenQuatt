#include <assert.h>
#include <math.h>

#include <initializer_list>
#include <string_view>

#include "../../openquatt/includes/control/oq_boiler_cm3_opentherm.cpp"

namespace {

using oq_boiler::ControllerDecision;
using oq_boiler_cm3_opentherm::apply;
using oq_boiler_cm3_opentherm::Control;
using oq_boiler_cm3_opentherm::Inputs;

constexpr uint32_t kTickMs = 5000;
constexpr float kFlowLph = 800.0f;
constexpr float kCp = 4186.0f;
constexpr float kConductance = kFlowLph * kCp / 3600.0f;
constexpr float kMinimumW = 4340.0f;
constexpr ControllerDecision kChRequested{true, true, true, false, false, oq_boiler::BLOCK_NONE};

Inputs field_inputs() {
  Inputs in;
  in.now_ms = kTickMs;
  in.command_updated_at_ms = kTickMs - 100;
  in.heat_requested = true;
  in.start_allowed = true;
  in.status_valid = true;
  in.flow_sufficient = true;
  in.capacity_kw = 31.0f;
  in.minimum_modulation_pct = 14.0f;
  in.requested_power_w = 4807.89f;
  in.inlet_c = 28.2f;
  in.outlet_c = 31.4f;
  in.flow_lph = kFlowLph;
  in.water_cp_j_per_kgk = kCp;
  in.maximum_water_c = 55.0f;
  in.ordinary_target_c = in.inlet_c + in.requested_power_w / kConductance;
  in.room_c = 17.24f;
  in.setpoint_c = 18.5f;
  return in;
}

float next_target(Control& control, Inputs& in) {
  in.now_ms += kTickMs;
  return control.tick(in).target_c;
}

void expect_targets_for(Control& control, Inputs& in, uint32_t duration_ms, bool active) {
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += kTickMs) {
    assert(isfinite(next_target(control, in)) == active);
  }
}

// The field capture shows prolonged CH-on, a roughly 30 s initial burn,
// about 180 s until retry, and approximately 10 K lift as the first warm
// start cuts out. The nominal 4.34 kW comes from ID15. Ignition power, plant
// time constants and the internal cutout threshold are modelling assumptions.
struct BoilerModel {
  float inlet_c = 28.2f;
  float outlet_c = 31.4f;
  uint32_t rest_ms = 0;
  uint32_t flame_ms = 0;
  uint32_t longest_burn_ms = 0;
  unsigned starts = 0;
  unsigned short_burns = 0;
  float delivered_ws = 0;
  bool flame = false;

  void tick(float target_c) {
    if (!flame && rest_ms == 0 && isfinite(target_c) && target_c > outlet_c) {
      flame = true;
      ++starts;
    }
    float power_w = 0.0f;
    if (flame) {
      flame_ms += kTickMs;
      if (isfinite(target_c)) {
        power_w = flame_ms <= 45000 ? 16000.0f : fmaxf(kMinimumW, (target_c - inlet_c) * kConductance);
      }
    }
    outlet_c += 0.16f * (inlet_c + power_w / kConductance - outlet_c);
    delivered_ws += fmaxf(0.0f, (outlet_c - inlet_c) * kConductance) * (kTickMs / 1000.0f);
    if (flame && (!isfinite(target_c) || (flame_ms >= 30000 && outlet_c > target_c + 6.0f))) {
      if (flame_ms <= 40000) ++short_burns;
      flame = false;
      rest_ms = 180000;
      flame_ms = 0;
    }
    if (flame_ms > longest_burn_ms) longest_burn_ms = flame_ms;
    if (rest_ms > 0) rest_ms -= kTickMs;
  }
};

BoilerModel replay(float power_w, bool use_candidate, uint32_t duration_ms, float drift_w = 0.0f) {
  BoilerModel boiler;
  auto in = field_inputs();
  Control control;
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += kTickMs) {
    in.now_ms += kTickMs;
    in.requested_power_w = power_w + drift_w * elapsed / duration_ms;
    in.outlet_c = boiler.outlet_c;
    in.flame_on = boiler.flame;
    const float target_c = use_candidate ? control.tick(in).target_c : in.inlet_c + power_w / kConductance;
    in.ch_applied = isfinite(target_c);
    assert(!in.ch_applied || target_c <= in.maximum_water_c);
    boiler.tick(target_c);
  }
  return boiler;
}

void test_warm_start_replay() {
  const auto baseline = replay(4807.89f, false, 1800000);
  assert(baseline.short_burns >= 4);
  assert(baseline.longest_burn_ms <= 40000);
  const auto candidate = replay(4807.89f, true, 1800000);
  assert(candidate.starts == 1);
  assert(candidate.short_burns == 0);
  assert(candidate.longest_burn_ms > 1500000);
}

void test_running_flame_survives_power_dips() {
  for (const bool defrost : {false, true}) {
    Control control;
    auto in = field_inputs();
    assert(isfinite(control.tick(in).target_c));
    in.ch_applied = in.flame_on = true;
    in.outlet_c = in.inlet_c + 6000.0f / kConductance;
    expect_targets_for(control, in, 210000, true);

    in.defrost_active = defrost;
    in.requested_power_w = 4010.0f;
    expect_targets_for(control, in, 90000, true);
    in.requested_power_w = 3675.0f;
    expect_targets_for(control, in, 50000, true);
    in.requested_power_w = 4700.0f;
    expect_targets_for(control, in, 10000, true);

    in.requested_power_w = 1800.0f;
    expect_targets_for(control, in, 60000, true);
    in.now_ms += kTickMs;
    const auto stop = control.tick(in);
    assert(isnan(stop.target_c));
    assert(stop.stop_reason != nullptr && std::string_view(stop.stop_reason) == "heat dose complete");
  }
}

void test_small_deficit_is_served_in_doses() {
  assert(replay(1800.0f, false, 5400000).short_burns > 4);
  struct ReplayCase {
    float initial_w, drift_w;
    uint32_t duration_ms;
    unsigned min_starts, max_starts;
    float min_average_w, max_average_w;
  };
  // Constant demand and the 1600 s HIL window with a rise from 1.85 to 2.11 kW.
  for (const auto& run : {ReplayCase{1800.0f, 0.0f, 5400000, 2, 5, 1350.0f, 2250.0f},
                          {1850.0f, 260.0f, 1600000, 2, 2, 1600.0f, 2500.0f}}) {
    const auto boiler = replay(run.initial_w, true, run.duration_ms, run.drift_w);
    const float average_w = boiler.delivered_ws / (run.duration_ms / 1000.0f);
    assert(boiler.starts >= run.min_starts && boiler.starts <= run.max_starts);
    assert(boiler.short_burns == 0 && boiler.longest_burn_ms >= 175000);
    assert(average_w >= run.min_average_w && average_w <= run.max_average_w);
  }
}

void test_setpoint_recovery_in_room_model() {
  BoilerModel boiler;
  Control control;
  auto in = field_inputs();
  constexpr float room_heat_capacity_j_per_k = 10.0e6f;
  constexpr float missing_power_at_setpoint_w = 800.0f;
  float peak_room_c = in.room_c;
  for (uint32_t elapsed = 0; elapsed < 4 * 3600000U; elapsed += kTickMs) {
    const float temperature_error_c = in.setpoint_c - in.room_c;
    in.heat_requested = temperature_error_c > -0.1f;
    in.requested_power_w = fmaxf(0.0f, missing_power_at_setpoint_w + temperature_error_c * 3200.0f);
    in.outlet_c = boiler.outlet_c;
    in.flame_on = boiler.flame;
    in.now_ms += kTickMs;
    const float target_c = control.tick(in).target_c;
    in.ch_applied = isfinite(target_c);
    const float previous_energy_ws = boiler.delivered_ws;
    boiler.tick(target_c);
    const float extra_heat_ws = boiler.delivered_ws - previous_energy_ws;
    const float loss_ws = missing_power_at_setpoint_w * (kTickMs / 1000.0f);
    in.room_c += (extra_heat_ws - loss_ws) / room_heat_capacity_j_per_k;
    peak_room_c = fmaxf(peak_room_c, in.room_c);
  }
  assert(in.room_c > in.setpoint_c - 0.2f);
  assert(peak_room_c < in.setpoint_c + 0.15f);
  assert(boiler.short_burns == 0);
}

void test_start_holds_then_ramps_to_the_hydraulic_target() {
  Control control;
  auto in = field_inputs();
  const float ignition_c = control.tick(in).target_c;
  const float steady_c = in.inlet_c + in.requested_power_w / kConductance;
  assert(ignition_c > 43.0f && ignition_c < 48.0f);
  in.ch_applied = in.flame_on = true;
  for (int tick = 0; tick < 12; ++tick) assert(next_target(control, in) == ignition_c);
  assert(fabsf(next_target(control, in) - (ignition_c - 0.5f)) < 0.01f);
  for (int tick = 0; tick < 35; ++tick) assert(next_target(control, in) >= steady_c - 0.01f);
  assert(fabsf(control.tick(in).target_c - steady_c) < 0.01f);
}

void test_missing_data_water_margin_and_baseline_permission() {
  auto in = field_inputs();
  Control control;
  for (float unavailable : {NAN, 101.0f}) {
    auto candidate = in;
    candidate.minimum_modulation_pct = unavailable;
    assert(!control.tick(candidate).managed);
  }
  in.outlet_c = NAN;  // Boiler supports ID15 but not outlet temperature.
  for (int tick = 0; tick < 20; ++tick) {
    in.now_ms += kTickMs;
    assert(!control.tick(in).managed);
  }
  in.outlet_c = 31.4f;
  in.start_allowed = false;
  assert(isnan(control.tick(in).target_c));
  in.status_valid = false;
  const auto stale_status = control.tick(in);
  assert(stale_status.managed && isnan(stale_status.target_c));
  in.status_valid = true;
  in.start_allowed = true;
  in.inlet_c = 47.0f;
  assert(isnan(control.tick(in).target_c));
  in.inlet_c = 28.2f;
  in.outlet_c = 51.0f;
  assert(isnan(control.tick(in).target_c));
  in.outlet_c = 31.4f;
  assert(isfinite(control.tick(in).target_c));
  in.ch_applied = true;
  in.heat_requested = false;
  assert(isnan(control.tick(in).target_c));
}

void test_in_target_band_keeps_continuous_heat_but_clears_dose() {
  Control control;
  auto in = field_inputs();
  in.room_c = in.setpoint_c + 0.01f;
  assert(isfinite(control.tick(in).target_c));
  in.ch_applied = in.flame_on = true;
  in.requested_power_w = 1800.0f;
  assert(isnan(control.tick(in).target_c));
}

void test_preferred_boost_is_capped_for_warm_inlet() {
  Control control;
  auto in = field_inputs();
  in.inlet_c = 37.0f;
  in.outlet_c = 40.0f;
  assert(control.tick(in).target_c == 53.0f);
}

void test_defrost_dhw_and_setpoint_drop() {
  Control control;
  auto in = field_inputs();
  in.defrost_active = true;
  assert(isnan(control.tick(in).target_c));
  in.defrost_active = false;
  assert(isfinite(control.tick(in).target_c));
  in.ch_applied = in.flame_on = true;
  assert(isfinite(next_target(control, in)));
  in.dhw_active = true;
  assert(isnan(control.tick(in).target_c));
  in.dhw_active = false;
  in.ch_applied = false;
  assert(isnan(control.tick(in).target_c));  // No boost on the residual DHW flame.
  in.flame_on = false;
  assert(isfinite(control.tick(in).target_c));
  in.setpoint_c -= 0.5f;
  assert(isnan(control.tick(in).target_c));  // Immediate even if the room is still below the new setpoint.
}

void test_idle_setpoint_drop_waits_for_refreshed_strategy_command() {
  Control control;
  auto in = field_inputs();
  in.defrost_active = true;
  assert(isnan(control.tick(in).target_c));

  in.now_ms += kTickMs;
  in.setpoint_c = NAN;
  assert(isnan(control.tick(in).target_c));
  in.now_ms += kTickMs;
  in.setpoint_c = 18.0f;
  const uint32_t reduction_ms = in.now_ms;
  assert(isnan(control.tick(in).target_c));
  in.defrost_active = false;
  in.now_ms += kTickMs;
  assert(isnan(control.tick(in).target_c));  // Still the command for 18.5 C.

  in.now_ms += kTickMs;
  in.command_updated_at_ms = reduction_ms - 1;
  assert(isnan(control.tick(in).target_c));  // A newer dispatch can still carry a pre-reduction calculation.
  in.now_ms += kTickMs;
  in.command_updated_at_ms = in.now_ms - 100;
  assert(isfinite(control.tick(in).target_c));
}

void test_normal_cm3_stops_respect_minimum_on_time() {
  enum StopCause { WITHDRAWN_REQUEST, DOSE_ROOM_SATISFIED, SETPOINT_REDUCED };
  for (auto cause : {WITHDRAWN_REQUEST, DOSE_ROOM_SATISFIED, SETPOINT_REDUCED}) {
    Control control;
    auto in = field_inputs();
    const float ordinary_c = in.ordinary_target_c;
    const uint32_t started_ms = in.now_ms;
    auto output_c = ordinary_c;
    auto start = kChRequested;
    apply(control, true, in, start, output_c);
    assert(start.output_active && output_c > ordinary_c);

    in.ch_applied = in.flame_on = true;
    if (cause == WITHDRAWN_REQUEST) {
      in.heat_requested = false;
      in.ordinary_target_c = NAN;  // The idle PH command carries no target.
    } else if (cause == DOSE_ROOM_SATISFIED) {
      in.room_c = in.setpoint_c;
      in.requested_power_w = 1800.0f;
      in.ordinary_target_c = in.inlet_c + in.requested_power_w / kConductance;
    } else {
      in.setpoint_c -= 0.5f;
    }
    constexpr uint32_t minimum_on_ms = 30000;
    for (uint32_t age_ms = kTickMs; age_ms <= minimum_on_ms; age_ms += kTickMs) {
      in.now_ms = started_ms + age_ms;
      in.minimum_on_active = oq_boiler::minimum_time_active(in.now_ms, started_ms, minimum_on_ms);
      const auto normal_reason = in.heat_requested      ? oq_boiler::BLOCK_NONE
                                 : in.minimum_on_active ? oq_boiler::BLOCK_MIN_ON_TIME
                                                        : oq_boiler::BLOCK_NO_HEAT_REQUEST;
      ControllerDecision normal{true,  in.heat_requested, in.heat_requested || in.minimum_on_active, false,
                                false, normal_reason};
      apply(control, true, in, normal, output_c);
      assert(normal.output_active == in.minimum_on_active && !normal.blocked);
      if (in.minimum_on_active) {
        assert(normal.block_reason == oq_boiler::BLOCK_MIN_ON_TIME);
        assert(output_c == (cause == DOSE_ROOM_SATISFIED ? in.ordinary_target_c : ordinary_c));
      } else {
        assert(isnan(output_c));
        assert(normal.block_reason ==
               (in.heat_requested ? oq_boiler::BLOCK_CM3_OPENTHERM_WAITING : oq_boiler::BLOCK_NO_HEAT_REQUEST));
      }
    }
  }
}

void test_cm3_lost_feedback_is_reported_and_overrides_minimum_on() {
  enum LostField { OUTLET, INLET, CAPACITY, STATUS };
  for (auto lost : {OUTLET, INLET, CAPACITY, STATUS}) {
    Control control;
    auto in = field_inputs();
    assert(isfinite(control.tick(in).target_c));
    in.now_ms += kTickMs;
    in.minimum_on_active = in.ch_applied = true;
    auto valid = in;
    if (lost == OUTLET) in.outlet_c = NAN;
    if (lost == INLET) in.inlet_c = NAN;
    if (lost == CAPACITY) in.capacity_kw = NAN;
    if (lost == STATUS) in.status_valid = false;
    constexpr auto telemetry_unavailable = oq_boiler::BLOCK_CM3_OPENTHERM_TELEMETRY_UNAVAILABLE;
    float output_c = in.ordinary_target_c;
    for (int tick = 0; tick < 30; ++tick) {
      auto normal = kChRequested;
      apply(control, true, in, normal, output_c);
      assert(!normal.output_active && !normal.force_off && normal.blocked &&
             normal.block_reason == telemetry_unavailable);
      assert(isnan(output_c));
      in.now_ms += kTickMs;
      in.minimum_on_active = in.ch_applied = false;
    }

    // Returning telemetry cannot bypass the ordinary minimum-off time.
    valid.now_ms = in.now_ms;
    valid.minimum_on_active = valid.ch_applied = valid.start_allowed = false;
    auto resting = ControllerDecision{true, true, false, false, true, oq_boiler::BLOCK_MIN_OFF_TIME};
    apply(control, true, valid, resting, output_c);
    assert(!resting.output_active && resting.block_reason == oq_boiler::BLOCK_MIN_OFF_TIME && isnan(output_c));
    valid.now_ms += kTickMs;
    valid.start_allowed = true;
    auto recovered = kChRequested;
    apply(control, true, valid, recovered, output_c);
    assert(recovered.output_active && !recovered.blocked && isfinite(output_c));
  }
}

void test_cm3_minimum_on_does_not_hold_an_unsafe_output() {
  enum Inhibit { DHW, FAULT, DEFROST, HOT_OUTLET, LOW_FLOW };
  for (auto reason : {DHW, FAULT, DEFROST, HOT_OUTLET, LOW_FLOW}) {
    Control control;
    auto in = field_inputs();
    assert(isfinite(control.tick(in).target_c));
    in.now_ms += kTickMs;
    in.ch_applied = in.minimum_on_active = true;
    in.heat_requested = false;
    in.dhw_active = reason == DHW;
    in.fault_active = reason == FAULT;
    in.defrost_active = reason == DEFROST;
    if (reason == HOT_OUTLET) in.outlet_c = in.maximum_water_c - 0.5f;
    in.flow_sufficient = reason != LOW_FLOW;
    auto hold = ControllerDecision{true, false, true, false, false, oq_boiler::BLOCK_MIN_ON_TIME};
    auto output_c = in.ordinary_target_c;
    apply(control, true, in, hold, output_c);
    assert(!hold.output_active && isnan(output_c) && hold.block_reason != oq_boiler::BLOCK_MIN_ON_TIME);
  }
}

void test_cm3_link_recovery_requires_thermal_feedback() {
  Control control;
  auto in = field_inputs();
  assert(isfinite(control.tick(in).target_c));
  control.withdraw_output();  // Selected OpenTherm link lost.
  assert(!control.running());
  in.now_ms += kTickMs;
  in.status_valid = in.heat_requested = in.start_allowed = false;
  assert(isnan(control.tick(in).target_c));

  control.withdraw_output();  // STATUS returns and the transport rearms.
  in.now_ms += 120000;
  in.status_valid = in.heat_requested = in.start_allowed = true;
  in.outlet_c = NAN;
  auto decision = control.tick(in);
  assert(decision.managed && isnan(decision.target_c));
  assert(decision.block_reason == oq_boiler::BLOCK_CM3_OPENTHERM_TELEMETRY_UNAVAILABLE);

  in.now_ms += kTickMs;
  in.outlet_c = 31.4f;
  assert(isfinite(control.tick(in).target_c));
}

void test_staggered_cm4_handover_and_force_off() {
  Control control;
  auto in = field_inputs();
  constexpr float unboosted_c = 33.6f;
  auto normal = kChRequested;
  auto output_c = unboosted_c;
  apply(control, true, in, normal, output_c);
  assert(normal.output_active && output_c > unboosted_c);

  in.now_ms += kTickMs;
  output_c = unboosted_c;
  apply(control, false, {}, normal, output_c);
  assert(normal.output_active && output_c == unboosted_c);  // CM4 supervisor tick, dispatcher still PH.
  output_c = 55.0f;
  apply(control, false, {}, normal, output_c);
  assert(normal.output_active && output_c == 55.0f);  // Dispatcher has now published CM4.

  apply(control, true, in, normal, output_c);
  auto hold = ControllerDecision{true, false, true, false, false, oq_boiler::BLOCK_MIN_ON_TIME};
  apply(control, false, {}, hold, output_c);
  assert(!hold.output_active && isnan(output_c));  // New owner is already published and has no heat request.

  apply(control, true, in, normal, output_c);
  auto trip = ControllerDecision{true, false, false, true, true, oq_boiler::BLOCK_WATER_TEMP_HARD_TRIP};
  in.heat_requested = false;
  output_c = NAN;
  apply(control, true, in, trip, output_c);
  assert(!trip.output_active && isnan(output_c) && trip.block_reason == oq_boiler::BLOCK_WATER_TEMP_HARD_TRIP);
}

void test_failed_ignition_observes_retry_rest() {
  Control control;
  auto in = field_inputs();
  in.now_ms = UINT32_MAX - 2 * kTickMs;
  assert(isfinite(control.tick(in).target_c));
  in.ch_applied = in.flame_on = true;
  assert(isfinite(next_target(control, in)));
  in.flame_on = false;
  assert(isnan(next_target(control, in)));

  in.ch_applied = false;
  expect_targets_for(control, in, 360000 - kTickMs, false);
  assert(isfinite(next_target(control, in)));
  in.ch_applied = true;
  expect_targets_for(control, in, 150000 - kTickMs, true);
  assert(isnan(next_target(control, in)));
}

void test_flow_confirmation_after_flame_loss() {
  struct FlowCase {
    uint32_t firing_ms, flow_loss_ms, flow_recovery_ms, expected_restart_ms;
    bool flow_loss_confirmed;
    float interrupted_flow_lph;
  };
  // Flame-off arrives first. Timely, confirmed flow loss permits ordinary
  // minimum-off recovery even after an early flame loss. Late or missing
  // confirmation leaves the ignition retry rest in effect.
  for (const auto& run : {FlowCase{10000, kTickMs, 45000, 120000, true, kFlowLph},
                          {100000, kTickMs, 45000, 120000, true, kFlowLph},
                          {kTickMs, kTickMs, 60000, 360000, false, NAN},
                          {kTickMs, 20000, 60000, 360000, true, 0.0f}}) {
    Control control;
    auto in = field_inputs();
    assert(isfinite(control.tick(in).target_c));
    in.ch_applied = in.flame_on = true;
    expect_targets_for(control, in, run.firing_ms, true);
    in.flame_on = false;
    assert(isnan(next_target(control, in)));

    in.ch_applied = false;
    for (uint32_t elapsed = kTickMs; elapsed <= run.expected_restart_ms; elapsed += kTickMs) {
      in.flow_sufficient = in.heat_requested = elapsed < run.flow_loss_ms || elapsed >= run.flow_recovery_ms;
      in.flow_loss_confirmed = run.flow_loss_confirmed && !in.flow_sufficient;
      in.flow_lph = in.flow_sufficient ? kFlowLph : run.interrupted_flow_lph;
      in.start_allowed = elapsed >= 120000;
      assert(isfinite(next_target(control, in)) == (elapsed == run.expected_restart_ms));
    }
  }
}

}  // namespace

int main() {
  test_warm_start_replay();
  test_running_flame_survives_power_dips();
  test_small_deficit_is_served_in_doses();
  test_setpoint_recovery_in_room_model();
  test_start_holds_then_ramps_to_the_hydraulic_target();
  test_missing_data_water_margin_and_baseline_permission();
  test_in_target_band_keeps_continuous_heat_but_clears_dose();
  test_preferred_boost_is_capped_for_warm_inlet();
  test_defrost_dhw_and_setpoint_drop();
  test_idle_setpoint_drop_waits_for_refreshed_strategy_command();
  test_normal_cm3_stops_respect_minimum_on_time();
  test_cm3_lost_feedback_is_reported_and_overrides_minimum_on();
  test_cm3_minimum_on_does_not_hold_an_unsafe_output();
  test_cm3_link_recovery_requires_thermal_feedback();
  test_staggered_cm4_handover_and_force_off();
  test_failed_ignition_observes_retry_rest();
  test_flow_confirmation_after_flame_loss();
}
