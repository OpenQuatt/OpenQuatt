#include <cassert>
#include <cmath>
#include <cstdint>

#include "openquatt/includes/control/oq_heat_intent_logic.h"
#include "openquatt/includes/control/oq_heating_curve_logic.h"
#include "openquatt/includes/control/oq_power_house_demand_logic.h"
#include "openquatt/includes/control/oq_warmup_logic.h"

using namespace oq_warmup;

static Input input(uint32_t now_ms = 1000) { return {now_ms, true, true, true, 1, 2, 3, 17.0f, 17.0f, 0.2f}; }

static State start(Input& in, const Settings& settings = {}) {
  auto state = evaluate(in, settings, {});
  assert(!state.active);
  in.now_ms += 1000;
  in.requested_c = 20.0f;
  state = evaluate(in, settings, state);
  assert(state.active);
  assert(std::fabs(state.target_c - 17.1f) < 0.0001f);
  return state;
}

static void test_fixed_target_and_acceleration() {
  Settings settings;
  auto in = input();
  auto state = start(in);
  in.room_c = 17.05f;
  in.now_ms += 60000;
  state = evaluate(in, settings, state);
  assert(std::fabs(state.target_c - 17.1f) < 0.0001f);
  in.now_ms = state.step_started_ms + settings.step_ms;
  state = evaluate(in, settings, state);
  assert(std::fabs(state.offset_c - 0.2f) < 0.0001f);
  assert(std::fabs(state.target_c - 17.25f) < 0.0001f);
  in.room_c = state.target_c;
  in.now_ms += 1000;
  state = evaluate(in, settings, state);
  assert(std::fabs(state.target_c - 17.45f) < 0.0001f);
  assert(std::fabs(state.offset_c - 0.2f) < 0.0001f);
  for (int i = 0; i < 6; ++i) {
    const float old_target = state.target_c;
    in.now_ms += settings.step_ms;
    if (i == 3) in.room_c -= 1.0f;
    state = evaluate(in, settings, state);
    assert(state.active);
    assert(state.target_c >= old_target);
    assert(state.offset_c <= settings.max_offset_c);
  }
  in.now_ms = state.started_ms + settings.max_ms;
  state = evaluate(in, settings, state);
  assert(!state.active && state.status == Status::TIME_LIMIT);
  assert(effective_target(state, in.requested_c) == 20.0f);
  in.now_ms += 1000;
  assert(!evaluate(in, settings, state).active);
}

static void test_trigger_and_comfort_boundaries() {
  Settings settings;
  auto in = input();
  auto state = evaluate(in, settings, {});
  in.requested_c += settings.trigger_c;
  state = evaluate(in, settings, state);
  assert(!state.active);  // Strictly greater, not >=.
  in.requested_c = 20.0f;
  in.room_c = 19.8f;
  state = evaluate(in, settings, state);
  assert(!state.active);  // Already inside real comfort band.
  in = input();
  state = start(in);
  in.room_c = 19.799f;
  state = evaluate(in, settings, state);
  assert(state.active && state.target_c <= in.requested_c);
  in.room_c = 19.8f;
  state = evaluate(in, settings, state);
  assert(!state.active && state.status == Status::COMFORT_REACHED);
  assert(state.offset_c == 0.0f);
}

static void test_target_changes_and_interruption() {
  Settings settings;
  auto in = input();
  auto state = start(in);
  const auto started = state.started_ms;
  const auto target = state.target_c;
  in.requested_c = 22.0f;
  in.now_ms += 1000;
  state = evaluate(in, settings, state);
  assert(state.active && state.started_ms == started && state.target_c == target);
  in.requested_c = 21.9f;
  state = evaluate(in, settings, state);
  assert(!state.active && state.status == Status::SETPOINT_LOWERED);
  assert(!evaluate(in, settings, state).active);
  in = input();
  state = start(in);
  in.enabled = false;
  state = evaluate(in, settings, state);
  assert(!state.active && state.status == Status::DISABLED);
  in.enabled = true;
  assert(!evaluate(in, settings, state).active);
}

static void test_sources_modes_stale_and_restart() {
  for (int boundary = 0; boundary < 7; ++boundary) {
    auto in = input();
    auto state = start(in);
    if (boundary == 0) ++in.room_source;
    if (boundary == 1) ++in.setpoint_source;
    if (boundary == 2) in.strategy = 2;
    if (boundary == 3) in.fresh = false;
    if (boundary == 4) in.room_c = NAN;
    if (boundary == 5) in.automatic_heating = false;
    if (boundary == 6) state = {};  // A session never survives reboot.
    state = evaluate(in, {}, state);
    assert(!state.active);
    in.fresh = true;
    in.room_c = 17.0f;
    in.automatic_heating = true;
    state = evaluate(in, {}, state);
    assert(!state.active);  // Returning input is not a setpoint-raise edge.
  }
  // Generations preserve A->B->A and endpoint changes with the same route.
  auto in = input();
  auto state = start(in);
  in.setpoint_source += 2;
  state = evaluate(in, {}, state);
  assert(!state.active && state.status == Status::SOURCE_CHANGED);
  state = cancel(Status::SETTINGS_CHANGED);
  assert(!evaluate(in, {}, state).active);
}

static void test_rollover_and_invalid_settings() {
  auto in = input(UINT32_MAX - 2000);
  auto state = start(in);
  const Settings settings;
  in.now_ms += settings.step_ms;
  state = evaluate(in, settings, state);
  assert(state.active && std::fabs(state.offset_c - 0.2f) < 0.0001f);
  in.now_ms = state.started_ms + settings.max_ms;
  state = evaluate(in, settings, state);
  assert(!state.active && state.status == Status::TIME_LIMIT);
  for (int field = 0; field < 5; ++field) {
    Settings invalid;
    if (field == 0) invalid.trigger_c = NAN;
    if (field == 1) invalid.step_c = 0.0f;
    if (field == 2) invalid.step_ms = UINT32_MAX;
    if (field == 3) invalid.max_offset_c = INFINITY;
    if (field == 4) invalid.max_ms = 0;
    assert(!valid_settings(invalid));
    in = input();
    state = start(in);
    state = evaluate(in, invalid, state);
    assert(!state.active && state.status == Status::INPUT_UNAVAILABLE);
  }
  Settings smaller_max;
  smaller_max.step_c = 0.5f;
  smaller_max.max_offset_c = 0.1f;
  in = input();
  state = evaluate(in, smaller_max, {});
  in.requested_c = 20.0f;
  state = evaluate(in, smaller_max, state);
  assert(state.offset_c == 0.5f);
  in.now_ms += smaller_max.step_ms;
  state = evaluate(in, smaller_max, state);
  assert(state.offset_c == 0.5f);  // A maximum never reduces the initial step.
}

static void test_strategy_inputs_and_no_synthetic_fast_start() {
  auto in = input();
  auto state = start(in);
  const float target = effective_target(state, in.requested_c);
  // Power House still supplies modelled house losses, but loses the large
  // room-error correction. Boiler deficit derives from this reduced request.
  oq_power_house::DemandInput demand{1000, 5, -10, 18, 10000, 17, 20, NAN, 1, false};
  oq_power_house::DemandTuning tuning{0, 2000, 0.2f, 0.3f, 8, 3, 20};
  const auto normal = oq_power_house::decide_demand(demand, tuning, {});
  demand.setpoint_c = target;
  const auto warming = oq_power_house::decide_demand(demand, tuning, {});
  assert(normal.valid && warming.valid);
  assert(warming.requested_w < normal.requested_w);
  assert(warming.contributions.room_feedback_w == 0.0f);

  // Actual setpoint is observed separately: intermediate steps and release
  // cannot masquerade as a new thermostat raise. Minimum-off/safety remains
  // downstream, and ordinary room demand can still start the heat pump.
  oq_heat_intent::Input intent{1000, true, true, true, true, true, false, 1, 17, target, 0.05f, 0.2f, 0};
  intent.requested_setpoint_c = 20;
  intent.allow_setpoint_raise = false;
  auto decision = oq_heat_intent::evaluate(intent, {});
  intent.setpoint_c += 0.4f;
  decision = oq_heat_intent::evaluate(intent, decision.next);
  assert(!decision.setpoint_raise_edge && decision.reason != oq_heat_intent::SETPOINT_RAISE);
  intent.allow_setpoint_raise = true;
  intent.room_c = 19.8f;
  intent.setpoint_c = 20;
  decision = oq_heat_intent::evaluate(intent, decision.next);
  assert(!decision.setpoint_raise_edge);
  intent.requested_setpoint_c = 21;
  intent.setpoint_c = 21;
  decision = oq_heat_intent::evaluate(intent, decision.next);
  assert(decision.setpoint_raise_edge);

  // Heating Curve uses its existing room resume band to end recovery.
  for (const auto* profile : {"Comfort", "Balanced", "Stable"}) {
    in = input();
    in.strategy = 2;
    in.comfort_below_c = oq_curve::control_profile(profile).room_resume_heat_c;
    state = start(in);
    in.room_c = in.requested_c - in.comfort_below_c;
    state = evaluate(in, {}, state);
    assert(!state.active && state.status == Status::COMFORT_REACHED);
  }
}

static void test_delayed_heating_permission_does_not_authorize_heat() {
  auto in = input();
  auto state = start(in);
  oq_heat_intent::Input intent{in.now_ms, true,  true, false,     true,
                               true,      false, 1,    in.room_c, effective_target(state, in.requested_c),
                               0.05f,     0.2f,  0};
  intent.requested_setpoint_c = in.requested_c;
  intent.allow_setpoint_raise = false;
  auto decision = oq_heat_intent::evaluate(intent, {});
  assert(state.active && !decision.active && !decision.fast_start);
  in.now_ms += 60000;
  state = evaluate(in, {}, state);
  assert(state.active);  // No heating permission is needed to retain the limiter.
  intent.now_ms = in.now_ms;
  intent.heating_enabled = true;
  decision = oq_heat_intent::evaluate(intent, decision.next);
  assert(decision.active && decision.reason == oq_heat_intent::ROOM_DEMAND);
  assert(!decision.setpoint_raise_edge);
}

static void test_cic_missing_payload_and_selected_update_lag() {
  auto in = input();
  auto state = start(in);
  oq_sources::RawFloatReceipt room, goal;
  room.observe(in.room_c, 1000U, true);
  goal.observe(in.requested_c, 1000U, true);
  float producer_room = in.room_c;
  const float producer_goal = in.requested_c;
  auto fresh = [&]() {
    return current_receipt_matches(room, producer_room) && current_receipt_matches(goal, producer_goal);
  };
  assert(fresh());
  // A normal valid CIC payload can precede the 10s selected-sensor update.
  producer_room = 17.15f;
  room.observe(producer_room, 2000U, true);
  in.fresh = fresh();
  state = evaluate(in, {}, state);
  assert(state.active && in.room_c == 17.0f);
  in.room_c = producer_room;
  in.now_ms += 1000;
  state = evaluate(in, {}, state);
  assert(state.active && state.target_c > 17.2f);
  // CIC intentionally suppresses producer publications within 0.001 C.
  room.observe(producer_room + 0.0005f, 2500U, true);
  in.fresh = fresh();
  state = evaluate(in, {}, state);
  assert(state.active);
  for (auto* missing : {&room, &goal}) {
    // HTTP200 may retain both public entities while omitting one raw field.
    missing->observe(NAN, 3000U, false);
    in.fresh = fresh();
    state = evaluate(in, {}, state);
    assert(!state.active && state.status == Status::INPUT_UNAVAILABLE);
    assert(effective_target(state, in.requested_c) == producer_goal);
    room.observe(producer_room, 4000U, true);
    goal.observe(producer_goal, 4000U, true);
    in.fresh = fresh();
    state = evaluate(in, {}, state);
    assert(!state.active);  // A recovered feed must not replay the old raise.
    in.requested_c = producer_goal - 3.0f;
    state = evaluate(in, {}, state);
    in.requested_c = producer_goal;
    state = evaluate(in, {}, state);
    assert(state.active);
  }
  room.received_ms = 0;
  assert(!current_receipt_matches(room, producer_room));
  room.observe(producer_room, 5000U, true);
  assert(!current_receipt_matches(room, producer_room + 1.0f));
}

static void test_persisted_settings_fail_closed() {
  for (int index = 0; index < 6; ++index) {
    float saved[6] = {1, 2, 0.2f, 60, 1, 12};
    saved[index] = NAN;
    normalize_stored_settings(saved);
    assert(saved[0] == 0 && saved[1] == 1.5f && saved[2] == 0.1f);
    assert(saved[3] == 45 && saved[4] == 0.5f && saved[5] == 8);
  }
  float valid[6] = {1, 2, 0.2f, 60, 1, 12};
  normalize_stored_settings(valid);
  assert(valid[0] == 1 && valid[1] == 2 && valid[2] == 0.2f);
  assert(valid[3] == 60 && valid[4] == 1 && valid[5] == 12);
  float bad_permission[6] = {2, 2, 0.2f, 60, 1, 12};
  normalize_stored_settings(bad_permission);
  assert(bad_permission[0] == 0);
  assert(duration_ms(INFINITY, 60000, 5, 120) == 0);
}

int main() {
  test_cic_missing_payload_and_selected_update_lag();
  test_delayed_heating_permission_does_not_authorize_heat();
  test_persisted_settings_fail_closed();
  test_fixed_target_and_acceleration();
  test_trigger_and_comfort_boundaries();
  test_target_changes_and_interruption();
  test_sources_modes_stale_and_restart();
  test_rollover_and_invalid_settings();
  test_strategy_inputs_and_no_synthetic_fast_start();
}
