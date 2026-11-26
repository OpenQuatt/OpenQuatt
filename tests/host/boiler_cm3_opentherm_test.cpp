#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_boiler_cm3_opentherm.cpp"

namespace {

using oq_boiler_cm3_opentherm::Control;
using oq_boiler_cm3_opentherm::Inputs;

constexpr uint32_t kTickMs = 5000;
constexpr float kFlowLph = 800.0f;
constexpr float kCp = 4186.0f;
constexpr float kConductance = kFlowLph * kCp / 3600.0f;
constexpr float kMinimumW = 4340.0f;

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
  in.room_c = 17.24f;
  in.setpoint_c = 18.5f;
  return in;
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

void test_small_deficit_is_served_in_doses() {
  constexpr float requested_w = 1800.0f;
  constexpr uint32_t duration_ms = 5400000;
  const auto baseline = replay(requested_w, false, duration_ms);
  assert(baseline.short_burns > 4);
  const auto candidate = replay(requested_w, true, duration_ms);
  assert(candidate.starts >= 2 && candidate.starts <= 5);
  assert(candidate.short_burns == 0);
  assert(candidate.longest_burn_ms >= 175000);
  const float measured_average_w = candidate.delivered_ws / (duration_ms / 1000.0f);
  assert(measured_average_w >= 0.75f * requested_w && measured_average_w <= 1.25f * requested_w);
}

void test_low_demand_window_keeps_delivered_heat_bounded() {
  // Replay the 1600 s HIL window with the measured rise from 1.85 to 2.11 kW.
  constexpr uint32_t duration_ms = 1600000;
  const auto boiler = replay(1850.0f, true, duration_ms, 260.0f);
  const float measured_average_w = boiler.delivered_ws / (duration_ms / 1000.0f);
  assert(boiler.starts == 2);
  assert(boiler.short_burns == 0);
  assert(boiler.longest_burn_ms >= 175000);
  assert(measured_average_w >= 1600.0f && measured_average_w <= 2500.0f);
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
  in.ch_applied = true;
  in.flame_on = true;
  for (int ticks = 0; ticks < 12; ++ticks) {
    in.now_ms += kTickMs;
    assert(control.tick(in).target_c == ignition_c);
  }
  in.now_ms += kTickMs;
  assert(fabsf(control.tick(in).target_c - (ignition_c - 0.5f)) < 0.01f);
  for (int ticks = 0; ticks < 35; ++ticks) {
    in.now_ms += kTickMs;
    assert(control.tick(in).target_c >= steady_c - 0.01f);
  }
  assert(fabsf(control.tick(in).target_c - steady_c) < 0.01f);
}

void test_missing_data_water_margin_and_baseline_permission() {
  auto in = field_inputs();
  Control control;
  in.minimum_modulation_pct = NAN;
  assert(!control.tick(in).managed);
  in.minimum_modulation_pct = 101.0f;
  assert(!control.tick(in).managed);
  in.minimum_modulation_pct = 14.0f;
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
  in.capacity_kw = NAN;
  assert(isnan(control.tick(in).target_c));
  in.capacity_kw = 31.0f;
  in.inlet_c = NAN;
  assert(isnan(control.tick(in).target_c));
  in.inlet_c = 28.2f;
  in.heat_requested = false;
  assert(isnan(control.tick(in).target_c));
}

void test_in_target_band_keeps_continuous_heat_but_clears_dose() {
  Control control;
  auto in = field_inputs();
  in.room_c = in.setpoint_c + 0.01f;
  assert(isfinite(control.tick(in).target_c));
  in.ch_applied = true;
  in.flame_on = true;
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
  in.ch_applied = true;
  in.flame_on = true;
  in.now_ms += kTickMs;
  assert(isfinite(control.tick(in).target_c));
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

void test_staggered_cm4_handover_and_force_off() {
  Control control;
  auto in = field_inputs();
  constexpr float unboosted_c = 33.6f;
  auto normal = oq_boiler::ControllerDecision{true, true, true, false, false, oq_boiler::BLOCK_NONE};
  auto output_c = unboosted_c;
  oq_boiler_cm3_opentherm::apply(control, true, in, normal, output_c);
  assert(normal.output_active && output_c > unboosted_c);

  in.now_ms += kTickMs;
  output_c = unboosted_c;
  oq_boiler_cm3_opentherm::apply(control, false, {}, normal, output_c);
  assert(normal.output_active && output_c == unboosted_c);  // CM4 supervisor tick, dispatcher still PH.
  output_c = 55.0f;
  oq_boiler_cm3_opentherm::apply(control, false, {}, normal, output_c);
  assert(normal.output_active && output_c == 55.0f);  // Dispatcher has now published CM4.

  oq_boiler_cm3_opentherm::apply(control, true, in, normal, output_c);
  auto hold = oq_boiler::ControllerDecision{true, false, true, false, false, oq_boiler::BLOCK_MIN_ON_TIME};
  oq_boiler_cm3_opentherm::apply(control, false, {}, hold, output_c);
  assert(!hold.output_active && isnan(output_c));  // New owner is already published and has no heat request.

  oq_boiler_cm3_opentherm::apply(control, true, in, normal, output_c);
  auto trip = oq_boiler::ControllerDecision{true, false, false, true, true, oq_boiler::BLOCK_WATER_TEMP_HARD_TRIP};
  in.heat_requested = false;
  output_c = NAN;
  oq_boiler_cm3_opentherm::apply(control, true, in, trip, output_c);
  assert(!trip.output_active && isnan(output_c) && trip.block_reason == oq_boiler::BLOCK_WATER_TEMP_HARD_TRIP);
}

void test_failed_ignition_observes_retry_rest() {
  Control control;
  auto in = field_inputs();
  in.now_ms = UINT32_MAX - 2 * kTickMs;
  assert(isfinite(control.tick(in).target_c));
  in.ch_applied = true;
  in.now_ms += kTickMs;
  in.flame_on = true;
  assert(isfinite(control.tick(in).target_c));
  in.now_ms += kTickMs;
  in.flame_on = false;
  assert(isnan(control.tick(in).target_c));
  in.ch_applied = false;
  for (unsigned elapsed = kTickMs; elapsed < 360000; elapsed += kTickMs) {
    in.now_ms += kTickMs;
    assert(isnan(control.tick(in).target_c));
  }
  in.now_ms += kTickMs;
  assert(isfinite(control.tick(in).target_c));
  in.ch_applied = true;
  for (unsigned elapsed = kTickMs; elapsed < 150000; elapsed += kTickMs) {
    in.now_ms += kTickMs;
    assert(isfinite(control.tick(in).target_c));
  }
  in.now_ms += kTickMs;
  assert(isnan(control.tick(in).target_c));
}

void test_flame_loss_then_flow_confirmation_uses_normal_off_time() {
  constexpr bool flame_states[] = {false, true};
  for (const bool stable_flame : flame_states) {
    Control control;
    auto in = field_inputs();
    assert(isfinite(control.tick(in).target_c));
    in.ch_applied = in.flame_on = true;
    for (int i = 0; i < (stable_flame ? 20 : 2); ++i) {
      in.now_ms += kTickMs;
      assert(isfinite(control.tick(in).target_c));
    }

    // Real bus ordering: the boiler reports flame-off one tick before the
    // flow filter confirms the interruption.
    in.now_ms += kTickMs;
    in.flame_on = false;
    assert(isnan(control.tick(in).target_c));
    in.ch_applied = false;
    in.start_allowed = false;
    const uint32_t stopped_at = in.now_ms;
    for (uint32_t elapsed = kTickMs; elapsed <= 120000; elapsed += kTickMs) {
      in.now_ms = stopped_at + elapsed;
      in.flow_sufficient = in.heat_requested = elapsed >= 45000;
      in.flow_loss_confirmed = !in.flow_sufficient;
      in.start_allowed = elapsed >= 120000;
      assert(isfinite(control.tick(in).target_c) == in.start_allowed);
    }
  }
}

void test_missing_or_late_flow_preserves_ignition_rest() {
  constexpr bool confirmed_losses[] = {false, true};
  for (const bool confirmed_loss : confirmed_losses) {
    Control control;
    auto in = field_inputs();
    assert(isfinite(control.tick(in).target_c));
    in.ch_applied = in.flame_on = true;
    in.now_ms += kTickMs;
    assert(isfinite(control.tick(in).target_c));
    in.flame_on = false;
    in.now_ms += kTickMs;
    assert(isnan(control.tick(in).target_c));

    in.ch_applied = false;
    const uint32_t failed_at = in.now_ms;
    const uint32_t flow_loss_at = confirmed_loss ? 20000 : kTickMs;
    for (uint32_t elapsed = kTickMs; elapsed <= 360000; elapsed += kTickMs) {
      in.now_ms = failed_at + elapsed;
      in.flow_sufficient = in.heat_requested = elapsed < flow_loss_at || elapsed >= 60000;
      in.flow_loss_confirmed = confirmed_loss && !in.flow_sufficient;
      in.flow_lph = in.flow_sufficient ? kFlowLph : confirmed_loss ? 0.0f : NAN;
      in.start_allowed = elapsed >= 120000;
      assert(isfinite(control.tick(in).target_c) == (elapsed == 360000));
    }
  }
}

}  // namespace

int main() {
  test_warm_start_replay();
  test_small_deficit_is_served_in_doses();
  test_low_demand_window_keeps_delivered_heat_bounded();
  test_setpoint_recovery_in_room_model();
  test_start_holds_then_ramps_to_the_hydraulic_target();
  test_missing_data_water_margin_and_baseline_permission();
  test_in_target_band_keeps_continuous_heat_but_clears_dose();
  test_preferred_boost_is_capped_for_warm_inlet();
  test_defrost_dhw_and_setpoint_drop();
  test_idle_setpoint_drop_waits_for_refreshed_strategy_command();
  test_staggered_cm4_handover_and_force_off();
  test_failed_ignition_observes_retry_rest();
  test_flame_loss_then_flow_confirmation_uses_normal_off_time();
  test_missing_or_late_flow_preserves_ignition_rest();
}
