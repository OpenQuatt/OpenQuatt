#include <assert.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "../../openquatt/includes/control/oq_hp_restart_guard.h"
#include "../../openquatt/includes/control/oq_supervisory_state_logic.h"
#include "../../openquatt/includes/incidents/oq_hp_incident_types.h"

namespace {

void test_low_load_hysteresis_and_cache() {
  using namespace oq_supervisory_state;
  LowLoadInput input;
  input.now_ms = 1000;
  input.active = true;
  input.raw_heating_request = true;
  input.openquatt_enabled = true;
  input.requested_power_w = 1300.0f;
  input.live_minimum_power_w = 1200.0f;
  input.off_factor = 0.75f;
  input.on_factor = 1.0f;
  input.minimum_hysteresis_w = 200.0f;
  input.fallback_off_w = 900.0f;
  input.fallback_on_w = 1300.0f;
  input.cache_max_ms = 60000;

  auto output = update_low_load(input, {});
  assert(output.source_code == 1);
  assert(output.off_threshold_w == 900.0f);
  assert(output.on_threshold_w == 1200.0f);
  assert(output.state.heat_latched && output.heating_request);

  input.now_ms = 2000;
  input.requested_power_w = 901.0f;
  input.live_minimum_power_w = NAN;
  output = update_low_load(input, output.state);
  assert(output.source_code == 2 && output.heating_request);
  input.requested_power_w = 900.0f;
  output = update_low_load(input, output.state);
  assert(!output.state.heat_latched && !output.heating_request);

  input.now_ms = 62001;
  input.requested_power_w = NAN;
  output = update_low_load(input, output.state);
  assert(output.source_code == 3);
  assert(output.state.heat_latched && output.heating_request);

  input.active = false;
  input.raw_heating_request = false;
  output.state.reentry_block_until_ms = 70000;
  output = update_low_load(input, output.state);
  assert(output.source_code == 0 && !output.heating_request && output.state.reentry_block_until_ms == 0);
}

void test_low_load_reentry_release_and_invalid_tuning() {
  using namespace oq_supervisory_state;
  LowLoadInput input;
  input.now_ms = 1000;
  input.active = true;
  input.raw_heating_request = true;
  input.openquatt_enabled = true;
  input.requested_power_w = 1000.0f;
  input.cache_max_ms = 1000;
  LowLoadState state;
  state.reentry_block_until_ms = 2000;
  auto output = update_low_load(input, state);
  assert(output.reentry_block_active);
  assert(output.off_threshold_w == 900.0f && output.on_threshold_w == 1300.0f);

  input.requested_power_w = 1300.0f;
  output = update_low_load(input, output.state);
  assert(!output.reentry_block_active && output.state.reentry_block_until_ms == 0);

  input.openquatt_enabled = false;
  input.off_factor = std::numeric_limits<float>::infinity();
  input.on_factor = std::numeric_limits<float>::infinity();
  output = update_low_load(input, output.state);
  assert(output.off_threshold_w == 900.0f && output.on_threshold_w == 1300.0f);
  assert(!output.heating_request);

  LowLoadState rollover;
  rollover.cached_minimum_power_w = 1200.0f;
  rollover.cached_minimum_power_updated_ms = UINT32_MAX - 20;
  input.openquatt_enabled = true;
  input.now_ms = 29;
  input.requested_power_w = 1300.0f;
  input.cache_max_ms = 50;
  output = update_low_load(input, rollover);
  assert(output.source_code == 2);
  input.now_ms = 30;
  output = update_low_load(input, output.state);
  assert(output.source_code == 3);
}

void test_low_load_minimum_off_does_not_restart_preflow() {
  using namespace oq_supervisory_state;
  // Issue #871: 1500 W correctly stops below 1600 W, but a 60 s cache used to
  // expire during the 240 s rest and re-arm preflow at the 1300 W fallback.
  constexpr uint32_t stopped_ms = 1000000;
  constexpr uint32_t minimum_off_ms = 240000;
  oq_hp_restart_guard::Policy rest;
  assert(rest.configure(minimum_off_ms, 30000));
  rest.observe_stopped(stopped_ms);
  oq_incidents::DerivedOutputs stopped;
  stopped.link_state = oq_incidents::LinkState::HEALTHY;
  stopped.run_state = oq_incidents::RunState::STOPPED;
  stopped.stop_confirmed = true;
  LowLoadInput input;
  input.now_ms = stopped_ms;
  input.active = true;
  input.raw_heating_request = true;
  input.openquatt_enabled = true;
  input.requested_power_w = 1500.0f;
  input.live_minimum_power_w = 2527.63f;
  input.cache_max_ms = 60000;
  auto output = update_low_load(input, {true});
  assert(!output.heating_request && output.off_threshold_w == 1600.0f && output.on_threshold_w == 2200.0f);

  for (uint32_t elapsed_ms = 10000; elapsed_ms <= minimum_off_ms; elapsed_ms += 10000) {
    input.now_ms = stopped_ms + elapsed_ms;
    rest.observe_stopped(input.now_ms);
    const bool resting = low_load_minimum_off_active(stopped, 0, rest.remaining_ms(input.now_ms));
    assert(resting == (elapsed_ms < minimum_off_ms));
    assert(rest.can_start(input.now_ms) == !resting);
    input.live_minimum_power_w = resting ? NAN : 2527.63f;
    input.cache_max_ms = low_load_cache_duration_ms(60000, minimum_off_ms, resting, false);
    output = update_low_load(input, output.state);
    assert(output.source_code == (resting ? 2 : 1));
    assert(!output.state.heat_latched && !output.heating_request);
    assert(output.off_threshold_w == 1600.0f && output.on_threshold_w == 2200.0f);
    assert(output.state.cached_minimum_power_updated_ms == (resting ? stopped_ms : input.now_ms));
    const auto startup = power_house_start(input.now_ms, output.heating_request, true, false, 30000, {});
    assert(!startup.preflow_request && !startup.heating_request);
  }

  // Genuine demand still passes the unchanged latch/confirmation path.
  input.requested_power_w = 2200.0f;
  output = update_low_load(input, output.state);
  assert(output.heating_request);
  auto startup = power_house_start(input.now_ms, output.heating_request, true, false, 30000, {});
  assert(startup.preflow_request && !startup.heating_request);
  startup = power_house_start(input.now_ms + 30000, output.heating_request, true, false, 30000, startup.state);
  assert(startup.heating_request && !startup.preflow_request);
}

void test_low_load_cache_only_extends_for_normal_rest() {
  using namespace oq_supervisory_state;
  using namespace oq_incidents;
  DerivedOutputs stopped;
  stopped.link_state = LinkState::HEALTHY;
  stopped.run_state = RunState::STOPPED;
  stopped.stop_confirmed = true;
  assert(low_load_minimum_off_active(stopped, 0, 240000));
  assert(!low_load_minimum_off_active(stopped, 1, 240000));
  assert(!low_load_minimum_off_active(stopped, 0, 0));
  stopped.protection_state = ProtectionState::LIMITED;
  assert(low_load_minimum_off_active(stopped, 0, 240000));
  stopped.protection_state = ProtectionState::CLEAR;
  for (auto state : {ProtectionState::START_BLOCKED, ProtectionState::FAULT_ACTIVE, ProtectionState::FAULT_RECOVERY}) {
    auto fault = stopped;
    fault.protection_state = state;
    assert(!low_load_minimum_off_active(fault, 0, 240000));
  }
  for (auto state : {LinkState::BOOTSTRAP, LinkState::SUSPECT, LinkState::LOST, LinkState::RECOVERING}) {
    auto offline = stopped;
    offline.link_state = state;
    assert(!low_load_minimum_off_active(offline, 0, 240000));
  }
  for (auto state : {RunState::UNKNOWN, RunState::RUNNING, RunState::STOPPING, RunState::STOP_UNCONFIRMED}) {
    auto running = stopped;
    running.run_state = state;
    assert(!low_load_minimum_off_active(running, 0, 240000));
  }
  auto unknown = stopped;
  unknown.stop_confirmed = false;
  assert(!low_load_minimum_off_active(unknown, 0, 240000));
  for (bool DerivedOutputs::* flag :
       {&DerivedOutputs::must_stop, &DerivedOutputs::fault_active, &DerivedOutputs::running_confirmed,
        &DerivedOutputs::stop_confirmation_pending, &DerivedOutputs::stop_unconfirmed}) {
    auto unsafe = stopped;
    unsafe.*flag = true;
    assert(!low_load_minimum_off_active(unsafe, 0, 240000));
  }

  // A failed Duo peer cannot extend ordinary model dropouts while the other
  // compressor is running. A healthy stopped peer may finish its real rest.
  auto failed_peer = stopped;
  failed_peer.must_stop = true;
  const bool running_or_fault =
      low_load_minimum_off_active(stopped, 1, 240000) || low_load_minimum_off_active(failed_peer, 0, 240000);
  assert(low_load_cache_duration_ms(60000, 240000, running_or_fault, true) == 60000);
  const bool healthy_rest_or_fault =
      low_load_minimum_off_active(stopped, 0, 1) || low_load_minimum_off_active(failed_peer, 0, 240000);
  assert(low_load_cache_duration_ms(60000, 240000, healthy_rest_or_fault, false) == 300000);
  // A running healthy peer keeps the original dropout deadline too.
  assert(low_load_cache_duration_ms(60000, 240000, healthy_rest_or_fault, true) == 60000);
}

void test_low_load_minimum_off_cache_limits() {
  using namespace oq_supervisory_state;
  assert(low_load_cache_duration_ms(60000, 240000, false, false) == 60000);
  assert(low_load_cache_duration_ms(60000, 240000, true, false) == 300000);
  assert(low_load_cache_duration_ms(60000, 0, true, false) == 60000);
  assert(low_load_cache_duration_ms(UINT32_MAX - 10, 240000, true, false) == UINT32_MAX);

  LowLoadInput input;
  input.active = true;
  input.raw_heating_request = true;
  input.openquatt_enabled = true;
  input.requested_power_w = 1500.0f;
  input.cache_max_ms = low_load_cache_duration_ms(60000, 240000, true, false);
  input.now_ms = 301000;
  LowLoadState cached{false, 2527.63f, 1000};
  auto output = update_low_load(input, cached);
  assert(output.source_code == 2 && !output.heating_request);
  input.now_ms++;
  output = update_low_load(input, output.state);
  assert(output.source_code == 3 && output.heating_request);

  // No rest or no previous model keeps the existing missing-model fallback.
  input.now_ms = 62000;
  input.cache_max_ms = low_load_cache_duration_ms(60000, 240000, false, false);
  output = update_low_load(input, cached);
  assert(output.source_code == 3 && output.heating_request);
  input.cache_max_ms = low_load_cache_duration_ms(60000, 240000, true, false);
  output = update_low_load(input, {});
  assert(output.source_code == 3 && output.heating_request);

  cached.cached_minimum_power_updated_ms = UINT32_MAX - 200000;
  input.now_ms = 99999;
  output = update_low_load(input, cached);
  assert(output.source_code == 2 && !output.heating_request);
  input.now_ms++;
  output = update_low_load(input, output.state);
  assert(output.source_code == 3 && output.heating_request);
}

void test_request_confirmation() {
  using namespace oq_supervisory_state;
  auto output = confirm_request(0, true, true, 1000, {});
  assert(output.state.timing && !output.confirmed);
  output = confirm_request(999, true, true, 1000, output.state);
  assert(!output.confirmed);
  output = confirm_request(1000, true, true, 1000, output.state);
  assert(output.confirmed);
  output = confirm_request(1001, false, true, 1000, output.state);
  assert(!output.state.timing && !output.confirmed);
  output = confirm_request(1002, true, true, 0, output.state);
  assert(!output.state.timing && output.confirmed);
  output = confirm_request(1003, true, false, 1000, output.state);
  assert(!output.state.timing && output.confirmed);

  ConfirmationState rollover{true, UINT32_MAX - 20};
  output = confirm_request(29, true, true, 50, rollover);
  assert(output.confirmed);

  auto startup = power_house_start(2000, true, true, false, 30000, {});
  assert(!startup.heating_request && startup.preflow_request && startup.state.timing);
  startup = power_house_start(31999, true, true, false, 30000, startup.state);
  assert(!startup.heating_request && startup.preflow_request);
  startup = power_house_start(32000, true, true, false, 30000, startup.state);
  assert(startup.heating_request && !startup.preflow_request);
  startup = power_house_start(33000, true, true, true, 30000, {});
  assert(startup.heating_request && !startup.preflow_request && !startup.state.timing);
  startup = power_house_start(34000, false, true, false, 30000, startup.state);
  assert(!startup.heating_request && !startup.preflow_request && !startup.state.timing);
}

void test_first_heating_preflow_overlaps_sensor_acquisition() {
  using namespace oq_supervisory_state;
  assert(start_heating_preflow(true, false, false, 0, 0, 1));
  const uint32_t until = 31000;
  assert(window_active(30000, until));
  // Getting flow and new samples during preflow cannot start a second window.
  assert(!start_heating_preflow(true, false, false, 1, until, 2));
  assert(!window_active(31001, until));
  assert(!hold_expired_heating_preflow(2, true, 2));
  // Missing/unsafe samples keep CM1 after 30 s. Cancellation discards the window.
  assert(hold_expired_heating_preflow(2, true, 1));
  assert(!start_heating_preflow(true, false, false, 1, until, 1));
  assert(!hold_expired_heating_preflow(2, false, 1));
  assert(!start_heating_preflow(false, false, false, 0, 0, 1));
  // Postflow, active compressors, cooling, CM4 and service retain ownership.
  assert(!hold_expired_heating_preflow(0, true, 1));
  assert(!hold_expired_heating_preflow(2, true, 4));
  assert(!start_heating_preflow(true, true, false, 0, 0, 1));
  assert(!start_heating_preflow(true, false, true, 1, 0, 1));
  assert(!start_heating_preflow(true, false, false, 4, 0, 2));
  assert(!start_heating_preflow(true, false, false, 100, 0, 2));
}

void test_idle_exit_boundaries() {
  using namespace oq_supervisory_state;
  IdleExitInput input;
  input.now_ms = 1000;
  input.timeout_ms = 5000;
  input.in_cm2 = true;
  input.heating_request = true;
  input.both_levels_off = true;
  input.both_units_idle = true;
  auto output = update_idle_exit(input, {});
  assert(output.reason == IdleExitReason::TIMING && !output.trip);
  input.now_ms = 5999;
  output = update_idle_exit(input, output.state);
  assert(!output.trip);
  input.now_ms = 6000;
  output = update_idle_exit(input, output.state);
  assert(output.trip && output.reason == IdleExitReason::TRIP);

  input.curve_mode = true;
  output = update_idle_exit(input, output.state);
  assert(!output.state.timing && output.reason == IdleExitReason::CURVE_MODE);
  input.curve_mode = false;
  input.high_load_block = true;
  output = update_idle_exit(input, output.state);
  assert(output.reason == IdleExitReason::HIGH_LOAD);
  assert(std::string(idle_exit_reason_name(output.reason)) == "blocked_high_load");

  input.high_load_block = false;
  input.heating_request = false;
  output = update_idle_exit(input, output.state);
  assert(output.reason == IdleExitReason::NO_HEAT_REQUEST && !output.state.timing);
  input.heating_request = true;
  input.both_levels_off = false;
  output = update_idle_exit(input, output.state);
  assert(output.reason == IdleExitReason::LEVELS_ON);
  input.both_levels_off = true;
  input.both_units_idle = false;
  output = update_idle_exit(input, output.state);
  assert(output.reason == IdleExitReason::UNITS_NOT_IDLE);
  input.both_units_idle = true;
  input.startup_grace_active = true;
  output = update_idle_exit(input, output.state);
  assert(output.reason == IdleExitReason::STARTUP_GRACE);
}

void test_override_timeout() {
  using namespace oq_supervisory_state;
  auto output = update_override(0, 2, 1000, {});
  assert(output.effective_mode == 2 && output.state.timing && !output.expired);
  output = update_override(999, 2, 1000, output.state);
  assert(output.effective_mode == 2 && !output.expired);
  output = update_override(1000, 2, 1000, output.state);
  assert(output.effective_mode == 0 && output.expired && !output.state.timing);

  OverrideState rollover{3, true, UINT32_MAX - 20};
  output = update_override(29, 3, 50, rollover);
  assert(output.expired);
  output = update_override(30, 99, 50, {});
  assert(output.effective_mode == 0);
  output = update_override(100, 1, 0, {});
  assert(output.effective_mode == 0 && output.expired);
}

void test_silent_window() {
  using namespace oq_supervisory_state;
  auto output = silent_window(true, 22 * 60, 21 * 60, 7 * 60, SilentOverride::SCHEDULE);
  assert(output.active && std::string(output.status) == "in_window");
  output = silent_window(true, 12 * 60, 21 * 60, 7 * 60, SilentOverride::SCHEDULE);
  assert(!output.active && std::string(output.status) == "out_of_window");
  output = silent_window(true, 12 * 60, 12 * 60, 12 * 60, SilentOverride::SCHEDULE);
  assert(!output.active && std::string(output.status) == "window_disabled");
  output = silent_window(false, 0, 0, 1, SilentOverride::ON);
  assert(output.active && std::string(output.status) == "forced_on");
  output = silent_window(true, 0, 23 * 60, 1 * 60, SilentOverride::OFF);
  assert(!output.active && std::string(output.status) == "forced_off");
}

void test_flow_guard_and_cm1_idle_hold() {
  using namespace oq_supervisory_state;
  assert(flow_guard_required(true, false, false));
  assert(flow_guard_required(false, true, false));
  assert(flow_guard_required(false, false, true));
  assert(!flow_guard_required(false, false, false));

  assert(hold_cm1_until_hp_idle(true, 0, true));
  assert(hold_cm1_until_hp_idle(true, 98, true));
  assert(!hold_cm1_until_hp_idle(true, 98, false));
  assert(!hold_cm1_until_hp_idle(false, 98, true));
  assert(!hold_cm1_until_hp_idle(true, 2, true));
  // Recovered demand can still yield CM0 when Power House postflow expires.
  // Decide from the actual destination, not demand/base_target: keep the pump on.
  assert(hold_cm1_until_hp_idle(true, 0, true));
}

void test_sticky_pump_timing() {
  using namespace oq_supervisory_state;
  auto output = update_sticky_pump(0, true, 1000, 500, {});
  assert(output.state.cm0_timing && !output.active);
  output = update_sticky_pump(999, true, 1000, 500, output.state);
  assert(!output.active);
  output = update_sticky_pump(1000, true, 1000, 500, output.state);
  assert(output.active && output.started);
  output = update_sticky_pump(1499, true, 1000, 500, output.state);
  assert(output.active && !output.started);
  output = update_sticky_pump(1500, true, 1000, 500, output.state);
  assert(output.active && !output.started);
  output = update_sticky_pump(1501, true, 1000, 500, output.state);
  assert(!output.active && output.state.cm0_since_ms == 1501);
  output = update_sticky_pump(1502, false, 1000, 500, output.state);
  assert(!output.active && !output.state.cm0_timing && output.state.active_until_ms == 0);

  StickyPumpState rollover{true, UINT32_MAX - 20, 0};
  output = update_sticky_pump(29, true, 50, 100, rollover);
  assert(output.active && output.started);

  output = update_sticky_pump(2000, true, 0, 0, {});
  assert(output.active && output.started && output.state.active_until_ms == 2000);
}

}  // namespace

int main() {
  using oq_supervisory_state::seconds_to_ms;
  using oq_supervisory_state::window_active;
  assert(seconds_to_ms(UINT32_MAX) == (UINT32_MAX / 1000UL) * 1000UL);
  assert(window_active(1000, 1001));
  assert(window_active(1000, 1000));
  assert(!window_active(1000, 0));
  assert(window_active(UINT32_MAX - 10, 20));
  assert(!window_active(20, UINT32_MAX - 10));
  test_low_load_hysteresis_and_cache();
  test_low_load_reentry_release_and_invalid_tuning();
  test_low_load_minimum_off_does_not_restart_preflow();
  test_low_load_cache_only_extends_for_normal_rest();
  test_low_load_minimum_off_cache_limits();
  test_request_confirmation();
  test_first_heating_preflow_overlaps_sensor_acquisition();
  test_idle_exit_boundaries();
  test_override_timeout();
  test_silent_window();
  test_flow_guard_and_cm1_idle_hold();
  test_sticky_pump_timing();
  return 0;
}
