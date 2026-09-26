#include <assert.h>
#include <initializer_list>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <type_traits>
#include <utility>

#include "openquatt/includes/boiler/oq_boiler_logic.h"
#include "openquatt/includes/boiler/oq_boiler_relay_target_logic.h"
#include "openquatt/includes/control/oq_boiler_control_logic.h"
#include "openquatt/includes/control/oq_boiler_dispatch_logic.h"

namespace {

// Detects a defrost member on an input type. This asserts the contract
// "the control chain has no defrost input" at the type level, so a legitimate
// logging, observability or comment change cannot break it, while actually
// wiring a defrost observation into the decision path would.
template <typename T, typename = void>
struct HasDefrostInput : std::false_type {};
template <typename T>
struct HasDefrostInput<T, std::void_t<decltype(std::declval<T&>().defrost)>> : std::true_type {};

using oq_boiler::RelayTargetConfig;
using oq_boiler::RelayTargetDecision;
using oq_boiler::RelayTargetInput;

constexpr float kTargetC = 40.0f;
constexpr RelayTargetConfig kConfig{2.0f, 0.5f};
// Boundaries for kTargetC with kConfig: start below 38.0 C, stop at 39.5 C.
constexpr float kStartThresholdC = 38.0f;
constexpr float kStopThresholdC = 39.5f;

RelayTargetInput target_input() {
  RelayTargetInput in;
  in.applicable = true;
  in.heat_request = true;
  in.output_active = false;
  in.supply_c = 30.0f;
  in.target_c = kTargetC;
  in.config = kConfig;
  return in;
}

// A target-control decision that wants the relay energised, used to prove the
// safety guards decide the output on their own.
RelayTargetDecision requesting_start() { return oq_boiler::evaluate_relay_target(target_input()); }

// A target-control decision that keeps an already energised relay inside the
// dead band.
RelayTargetDecision requesting_hold() {
  auto in = target_input();
  in.output_active = true;
  in.supply_c = 38.7f;
  return oq_boiler::evaluate_relay_target(in);
}

// A target-control decision that releases an energised relay because the
// requested target has been met.
RelayTargetDecision satisfied_target() {
  auto in = target_input();
  in.output_active = true;
  in.supply_c = 39.6f;
  return oq_boiler::evaluate_relay_target(in);
}

// A controller input whose only remaining job is to prove that the target
// controller cannot override a guard: every guard starts satisfied.
oq_boiler::ControllerInput guarded_input(RelayTargetDecision relay_target) {
  oq_boiler::ControllerInput input{};
  input.source_present = true;
  input.assist_enabled = true;
  input.fallback_enabled = true;
  input.supply_temperature_valid = true;
  input.flow_valid = true;
  input.flow_sufficient = true;
  input.fallback_outputs_safe = true;
  input.transport_available = true;
  input.transport_settled = true;
  input.command_rearmed = true;
  input.boiler_start_thermal_state = oq_boiler::BOILER_START_THERMAL_NOT_APPLICABLE;
  input.target_valid = true;
  input.relay_target = relay_target;
  input.now_ms = 5000U;
  input.command_max_age_ms = 15000U;
  input.output_last_change_ms = 0U;
  return input;
}

oq_boiler::BoilerCommand power_house_command() {
  return oq_boiler::BoilerCommand{
      true, true, true, 5000.0f, kTargetC, oq_boiler::COMMAND_SOURCE_POWER_HOUSE, 4000U,
  };
}

void assert_forced_off(const oq_boiler::ControllerInput& input, uint8_t expected_reason) {
  const auto decision = oq_boiler::evaluate(power_house_command(), input);
  assert(decision.force_off);
  assert(!decision.output_active);
  assert(decision.blocked);
  assert(decision.block_reason == expected_reason);
}

void test_no_heat_request_releases_the_relay() {
  auto in = target_input();
  in.heat_request = false;
  in.output_active = true;
  in.supply_c = 20.0f;
  const auto decision = oq_boiler::evaluate_relay_target(in);
  assert(!decision.requested_active);
  assert(decision.state == oq_boiler::RELAY_TARGET_IDLE);
  assert(!decision.satisfied);
}

void test_relay_starts_below_the_start_threshold() {
  auto in = target_input();
  in.supply_c = kStartThresholdC - 0.1f;
  const auto decision = oq_boiler::evaluate_relay_target(in);
  assert(decision.requested_active);
  assert(decision.state == oq_boiler::RELAY_TARGET_START);
  assert(!decision.satisfied);
}

void test_relay_stays_off_inside_the_band() {
  auto in = target_input();
  in.supply_c = kStopThresholdC - 0.1f;
  const auto decision = oq_boiler::evaluate_relay_target(in);
  assert(!decision.requested_active);
  assert(decision.satisfied);
  assert(decision.state == oq_boiler::RELAY_TARGET_SATISFIED);
}

void test_relay_stays_on_inside_the_band() {
  auto in = target_input();
  in.output_active = true;
  in.supply_c = kStartThresholdC + 0.1f;
  const auto decision = oq_boiler::evaluate_relay_target(in);
  assert(decision.requested_active);
  assert(!decision.satisfied);
  assert(decision.state == oq_boiler::RELAY_TARGET_HOLD);
}

void test_relay_stops_at_the_stop_threshold() {
  auto in = target_input();
  in.output_active = true;
  in.supply_c = kStopThresholdC;
  const auto decision = oq_boiler::evaluate_relay_target(in);
  assert(!decision.requested_active);
  assert(decision.satisfied);
  assert(decision.state == oq_boiler::RELAY_TARGET_SATISFIED);
}

void test_exact_boundaries_are_deterministic() {
  // At the start threshold the relay is energised, at the stop threshold it is
  // released. The two thresholds never coincide because the policy requires
  // stop_delta_c < start_delta_c.
  auto at_start = target_input();
  at_start.supply_c = kStartThresholdC;
  const auto started = oq_boiler::evaluate_relay_target(at_start);
  assert(started.state == oq_boiler::RELAY_TARGET_START);
  assert(started.requested_active);

  auto at_stop = target_input();
  at_stop.output_active = true;
  at_stop.supply_c = kStopThresholdC;
  const auto stopped = oq_boiler::evaluate_relay_target(at_stop);
  assert(stopped.state == oq_boiler::RELAY_TARGET_SATISFIED);
  assert(!stopped.requested_active);

  // The opposite state just across each boundary keeps the relay off/on.
  auto just_above_start = target_input();
  just_above_start.supply_c = nextafterf(kStartThresholdC, INFINITY);
  const auto still_off = oq_boiler::evaluate_relay_target(just_above_start);
  assert(!still_off.requested_active);
  assert(still_off.state == oq_boiler::RELAY_TARGET_SATISFIED);

  auto just_below_stop = target_input();
  just_below_stop.output_active = true;
  just_below_stop.supply_c = nextafterf(kStopThresholdC, -INFINITY);
  const auto still_on = oq_boiler::evaluate_relay_target(just_below_stop);
  assert(still_on.requested_active);
  assert(still_on.state == oq_boiler::RELAY_TARGET_HOLD);
}

void test_invalid_hysteresis_policy_is_rejected() {
  const RelayTargetConfig invalid[] = {
      RelayTargetConfig{0.0f, 0.5f}, RelayTargetConfig{2.0f, 0.0f},     RelayTargetConfig{2.0f, -1.0f},
      RelayTargetConfig{0.5f, 2.0f}, RelayTargetConfig{2.0f, 2.0f},     RelayTargetConfig{NAN, 0.5f},
      RelayTargetConfig{2.0f, NAN},  RelayTargetConfig{INFINITY, 0.5f}, RelayTargetConfig{2.0f, -INFINITY},
  };
  for (const RelayTargetConfig& config : invalid) {
    assert(!oq_boiler::relay_target_config_valid(config));
    auto in = target_input();
    in.output_active = true;
    in.config = config;
    const auto decision = oq_boiler::evaluate_relay_target(in);
    assert(!decision.requested_active);
    assert(decision.state == oq_boiler::RELAY_TARGET_UNAVAILABLE);
    assert(!decision.satisfied);
  }
  assert(oq_boiler::relay_target_config_valid(kConfig));
}

void test_unusable_measurements_fail_safe() {
  auto no_supply = target_input();
  no_supply.supply_c = NAN;
  no_supply.output_active = true;
  const auto supply_decision = oq_boiler::evaluate_relay_target(no_supply);
  assert(!supply_decision.requested_active);
  assert(supply_decision.state == oq_boiler::RELAY_TARGET_UNAVAILABLE);

  auto no_target = target_input();
  no_target.target_c = NAN;
  no_target.output_active = true;
  const auto target_decision = oq_boiler::evaluate_relay_target(no_target);
  assert(!target_decision.requested_active);
  assert(target_decision.state == oq_boiler::RELAY_TARGET_UNAVAILABLE);

  auto impossible_target = target_input();
  impossible_target.target_c = 0.0f;
  assert(oq_boiler::evaluate_relay_target(impossible_target).state == oq_boiler::RELAY_TARGET_UNAVAILABLE);

  // An unusable measurement is never reported as a satisfied target, so it
  // stays distinguishable from a genuinely reached target.
  assert(!supply_decision.satisfied);
  assert(!target_decision.satisfied);

  // Without target control the decision belongs to the outer controller and
  // target control adds nothing at all.
  auto not_applicable = target_input();
  not_applicable.applicable = false;
  not_applicable.output_active = true;
  not_applicable.supply_c = NAN;
  const auto skipped = oq_boiler::evaluate_relay_target(not_applicable);
  assert(skipped.state == oq_boiler::RELAY_TARGET_NOT_APPLICABLE);
  assert(skipped.requested_active);
}

void test_target_changes_move_the_request() {
  // A rising target while the relay is off can create a new request.
  auto rising = target_input();
  rising.supply_c = 37.0f;
  rising.target_c = 38.5f;  // Start threshold 36.5 C: the relay stays off.
  const auto held_off = oq_boiler::evaluate_relay_target(rising);
  assert(!held_off.requested_active);
  assert(held_off.state == oq_boiler::RELAY_TARGET_SATISFIED);
  rising.target_c = 42.0f;  // Start threshold 40.0 C: the relay may start.
  const auto started = oq_boiler::evaluate_relay_target(rising);
  assert(started.requested_active);
  assert(started.state == oq_boiler::RELAY_TARGET_START);

  // A falling target while the relay is on can stop the relay again.
  auto falling = target_input();
  falling.output_active = true;
  falling.supply_c = 38.5f;
  falling.target_c = 40.0f;
  assert(oq_boiler::evaluate_relay_target(falling).requested_active);
  falling.target_c = 38.0f;
  const auto stopped = oq_boiler::evaluate_relay_target(falling);
  assert(!stopped.requested_active);
  assert(stopped.state == oq_boiler::RELAY_TARGET_SATISFIED);
}

void test_only_normal_auxiliary_heat_sources_use_target_control() {
  assert(oq_boiler::relay_target_control_applies(oq_boiler::COMMAND_SOURCE_POWER_HOUSE, false));
  assert(oq_boiler::relay_target_control_applies(oq_boiler::COMMAND_SOURCE_HEATING_CURVE, false));
  assert(oq_boiler::relay_target_control_applies(oq_boiler::COMMAND_SOURCE_COLD_START, false));

  // CM4 fault fallback and CM100 commissioning keep their own semantics.
  assert(!oq_boiler::relay_target_control_applies(oq_boiler::COMMAND_SOURCE_FALLBACK, false));
  assert(!oq_boiler::relay_target_control_applies(oq_boiler::COMMAND_SOURCE_COMMISSIONING, false));
  assert(!oq_boiler::relay_target_control_applies(oq_boiler::COMMAND_SOURCE_NONE, false));
}

void test_source_policy_reaches_the_relay_decision() {
  const uint8_t sources[] = {
      oq_boiler::COMMAND_SOURCE_POWER_HOUSE,   oq_boiler::COMMAND_SOURCE_HEATING_CURVE,
      oq_boiler::COMMAND_SOURCE_COLD_START,    oq_boiler::COMMAND_SOURCE_FALLBACK,
      oq_boiler::COMMAND_SOURCE_COMMISSIONING,
  };
  for (const uint8_t source : sources) {
    auto in = target_input();
    const bool applies = oq_boiler::relay_target_control_applies(source, false);
    in.applicable = applies;
    in.supply_c = 39.0f;  // Inside the band, so only target control can hold.
    const auto decision = oq_boiler::evaluate_relay_target(in);
    assert(decision.applicable == applies);
    if (applies) {
      assert(!decision.requested_active);
      assert(decision.state == oq_boiler::RELAY_TARGET_SATISFIED);
    } else {
      assert(decision.state == oq_boiler::RELAY_TARGET_NOT_APPLICABLE);
      assert(decision.requested_active == in.output_active);
    }
  }
}

void test_cm4_fallback_keeps_its_on_off_semantics() {
  const oq_boiler::BoilerCommand command{
      true, true, true, 10000.0f, 60.0f, oq_boiler::COMMAND_SOURCE_FALLBACK, 4000U,
  };
  assert(oq_boiler::boiler_role_for_source(command.source) == oq_boiler::BoilerRole::FALLBACK_CM4);
  // CM4 has no target-control role, so the output decision is exactly as before
  // even when the supply is far below the requested target.
  for (const float supply_c : {20.0f, 5.0f, 59.9f}) {
    RelayTargetInput in;
    in.heat_request = true;
    in.supply_c = supply_c;
    in.target_c = 60.0f;
    in.config = kConfig;
    const auto decision = oq_boiler::evaluate(command, guarded_input(oq_boiler::evaluate_relay_target(in)));
    assert(decision.output_active);
    assert(!decision.force_off);
    assert(!decision.blocked);
    assert(decision.block_reason == oq_boiler::BLOCK_NONE);
  }
}

void test_cm100_commissioning_keeps_its_on_off_semantics() {
  const oq_boiler::BoilerCommand command{
      true, true, true, 10000.0f, 60.0f, oq_boiler::COMMAND_SOURCE_COMMISSIONING, 4000U,
  };
  assert(oq_boiler::boiler_role_for_source(command.source) == oq_boiler::BoilerRole::COMMISSIONING_CM100);
  for (const float supply_c : {20.0f, 5.0f, 59.9f}) {
    RelayTargetInput in;
    in.heat_request = true;
    in.supply_c = supply_c;
    in.target_c = 60.0f;
    in.config = kConfig;
    const auto decision = oq_boiler::evaluate(command, guarded_input(oq_boiler::evaluate_relay_target(in)));
    assert(decision.output_active);
    assert(!decision.force_off);
    assert(!decision.blocked);
    assert(decision.block_reason == oq_boiler::BLOCK_NONE);
  }
}

void test_opentherm_path_is_unaffected_by_relay_target_control() {
  for (const uint8_t source : {oq_boiler::COMMAND_SOURCE_POWER_HOUSE, oq_boiler::COMMAND_SOURCE_HEATING_CURVE,
                               oq_boiler::COMMAND_SOURCE_COLD_START}) {
    assert(!oq_boiler::relay_target_control_applies(source, true));
  }

  // The OpenTherm output decision is identical whether the relay target
  // controller asks for a start, a hold or nothing at all: CH enable and TSet
  // depend on the command target only.
  for (const RelayTargetDecision& relay_target :
       {RelayTargetDecision{}, requesting_start(), requesting_hold(), satisfied_target()}) {
    auto input = guarded_input(relay_target);
    input.target_required = true;  // opentherm_selected drives target_required
    const auto decision = oq_boiler::evaluate(power_house_command(), input);
    assert(decision.output_active);
    assert(decision.desired_active);
    assert(!decision.force_off);
    assert(!decision.blocked);
    assert(decision.block_reason == oq_boiler::BLOCK_NONE);
  }

  // An invalid OpenTherm target still blocks CH enable through the existing
  // target guard, independent of the relay target controller.
  auto invalid_target = guarded_input(requesting_start());
  invalid_target.target_required = true;
  invalid_target.target_valid = false;
  const auto blocked = oq_boiler::evaluate(power_house_command(), invalid_target);
  assert(blocked.force_off);
  assert(!blocked.output_active);
  assert(blocked.block_reason == oq_boiler::BLOCK_TARGET_INVALID);
}

void test_safety_guards_win_over_target_control() {
  const RelayTargetDecision start = requesting_start();

  auto insufficient_flow = guarded_input(start);
  insufficient_flow.flow_sufficient = false;
  assert_forced_off(insufficient_flow, oq_boiler::BLOCK_FLOW_INSUFFICIENT);

  auto no_flow = guarded_input(start);
  no_flow.flow_valid = false;
  assert_forced_off(no_flow, oq_boiler::BLOCK_FLOW_UNAVAILABLE);

  auto inhibit = guarded_input(start);
  inhibit.boiler_inhibit_active = true;
  assert_forced_off(inhibit, oq_boiler::BLOCK_WATER_TEMP_INHIBIT);

  auto trip = guarded_input(start);
  trip.hard_trip_active = true;
  assert_forced_off(trip, oq_boiler::BLOCK_WATER_TEMP_HARD_TRIP);

  auto assist_off = guarded_input(start);
  assist_off.assist_enabled = false;
  assert_forced_off(assist_off, oq_boiler::BLOCK_ASSIST_DISABLED);

  auto no_source = guarded_input(start);
  no_source.source_present = false;
  assert_forced_off(no_source, oq_boiler::BLOCK_SOURCE_NOT_CONNECTED);

  auto no_supply = guarded_input(start);
  no_supply.supply_temperature_valid = false;
  assert_forced_off(no_supply, oq_boiler::BLOCK_SUPPLY_UNAVAILABLE);

  auto stale = guarded_input(start);
  stale.command_max_age_ms = 100U;
  assert_forced_off(stale, oq_boiler::BLOCK_COMMAND_STALE);

  // A satisfied target must not mask a guard that fires in the same tick.
  auto satisfied_then_trip = guarded_input(satisfied_target());
  satisfied_then_trip.hard_trip_active = true;
  assert_forced_off(satisfied_then_trip, oq_boiler::BLOCK_WATER_TEMP_HARD_TRIP);
}

void test_transport_change_keeps_break_before_make() {
  const RelayTargetDecision start = requesting_start();

  auto settling = guarded_input(start);
  settling.transport_settled = false;
  assert_forced_off(settling, oq_boiler::BLOCK_TRANSPORT_SETTLING);

  auto awaiting = guarded_input(start);
  awaiting.command_rearmed = false;
  assert_forced_off(awaiting, oq_boiler::BLOCK_AWAITING_FRESH_COMMAND);

  auto mismatch = guarded_input(start);
  mismatch.connection_mismatch = true;
  assert_forced_off(mismatch, oq_boiler::BLOCK_CONNECTION_MISMATCH);

  auto unavailable = guarded_input(start);
  unavailable.transport_available = false;
  assert_forced_off(unavailable, oq_boiler::BLOCK_TRANSPORT_UNAVAILABLE);

  // While OpenTherm is selected the relay must stay off whatever the target
  // controller decides.
  assert(oq_boiler::relay_must_be_off(true, false, false));
  assert(oq_boiler::relay_must_be_off(false, false, true));
  assert(oq_boiler::relay_must_be_off(false, true, false));
  assert(!oq_boiler::relay_must_be_off(false, false, false));

  // The settle period elapses on its own, never because a command arrived.
  assert(!oq_boiler::settle_period_elapsed(true, 5000U, 4000U, 2000U));
  assert(oq_boiler::settle_period_elapsed(true, 6000U, 4000U, 2000U));
  assert(oq_boiler::settle_period_elapsed(false, 0U, 0U, 2000U));
  assert(!oq_boiler::command_satisfies_rearm(true, power_house_command(), 4000U));
  assert(oq_boiler::command_satisfies_rearm(true, power_house_command(), 3999U));
}

void test_satisfied_target_is_not_a_blocked_boiler() {
  const RelayTargetDecision satisfied = satisfied_target();
  assert(satisfied.state == oq_boiler::RELAY_TARGET_SATISFIED);
  assert(!satisfied.requested_active);
  const auto decision = oq_boiler::evaluate(power_house_command(), guarded_input(satisfied));
  // The demand still exists and stays visible; it is simply not blocked.
  assert(decision.demand_present);
  assert(decision.desired_active);
  assert(!decision.output_active);
  assert(!decision.force_off);
  assert(!decision.blocked);
  assert(decision.block_reason == oq_boiler::BLOCK_TARGET_SATISFIED);
  assert(strcmp(oq_boiler::block_reason_text(oq_boiler::BLOCK_TARGET_SATISFIED),
                "requested boiler target temperature satisfied") == 0);
}

void test_satisfied_target_respects_minimum_on_time() {
  // A satisfied target is a normal end of the heat request, so it follows the
  // normal anti-cycling rule. Priority is safety > ownership > anti-cycling >
  // target control: only a safety trip or a lost ownership withdraws heat
  // immediately.
  auto in = target_input();
  in.output_active = true;
  in.supply_c = kStopThresholdC;
  const auto satisfied = oq_boiler::evaluate_relay_target(in);
  assert(!satisfied.requested_active);
  assert(satisfied.state == oq_boiler::RELAY_TARGET_SATISFIED);

  // Minimum on-time still running: the relay stays on, exactly as it would for
  // a heat request that simply ended.
  auto holding = guarded_input(satisfied);
  holding.output_active = true;
  holding.output_last_change_ms = 4900U;
  holding.min_on_ms = 30000U;
  const auto held = oq_boiler::evaluate(power_house_command(), holding);
  assert(held.output_active);
  assert(held.demand_present);
  assert(held.desired_active);
  assert(!held.blocked);
  assert(held.block_reason == oq_boiler::BLOCK_MIN_ON_TIME);
  // The target itself is still reported as met while anti-cycling holds it.
  assert(held.block_reason != oq_boiler::BLOCK_TARGET_SATISFIED);

  // Same as a normal end of heat request inside the minimum on-time.
  const oq_boiler::BoilerCommand no_request{true, true, false, 0.0f, kTargetC, oq_boiler::COMMAND_SOURCE_POWER_HOUSE,
                                            4000U};
  const auto ended = oq_boiler::evaluate(no_request, holding);
  assert(ended.output_active);
  assert(ended.block_reason == oq_boiler::BLOCK_MIN_ON_TIME);

  // Minimum on-time expired: the relay is released with the satisfied reason.
  // The unsigned age is computed the same way the runtime does it, so this also
  // covers the millis() rollover case.
  auto expired = holding;
  expired.output_last_change_ms = expired.now_ms - 30000U;
  assert((uint32_t)(expired.now_ms - expired.output_last_change_ms) == 30000U);
  const auto released = oq_boiler::evaluate(power_house_command(), expired);
  assert(!released.output_active);
  assert(!released.blocked);
  assert(released.block_reason == oq_boiler::BLOCK_TARGET_SATISFIED);

  // A safety trip still overrides the minimum on-time immediately.
  auto trip = holding;
  trip.hard_trip_active = true;
  const auto forced = oq_boiler::evaluate(power_house_command(), trip);
  assert(!forced.output_active);
  assert(forced.force_off);
  assert(forced.block_reason == oq_boiler::BLOCK_WATER_TEMP_HARD_TRIP);
}

void test_satisfied_target_keeps_the_command_visible() {
  // The transport-neutral command stays intact while the physical output is
  // off: only the R1 target control reduced the output.
  const auto satisfied = satisfied_target();
  const auto decision = oq_boiler::evaluate(power_house_command(), guarded_input(satisfied));
  assert(!decision.output_active);
  assert(decision.demand_present);
  assert(decision.desired_active);
  assert(!decision.blocked);
  assert(decision.block_reason == oq_boiler::BLOCK_TARGET_SATISFIED);

  // Ownership is therefore still held, so the supervisory CM edge keeps the
  // boiler owned by the current mode.
  const auto command = power_house_command();
  assert(command.demand_present);
  assert(command.heat_request);
  assert(command.source == oq_boiler::COMMAND_SOURCE_POWER_HOUSE);
  assert(command.target_temperature_c == kTargetC);
  assert(oq_boiler::boiler_role_for_source(command.source) == oq_boiler::BoilerRole::ASSIST_CM3);

  // A target-satisfied stop is logged with normal severity and is never mapped
  // to a blocked or fault reason.
  const oq_boiler::BoilerLogCodes codes{};
  const auto log = oq_boiler::classify_boiler_controller_log(
      {oq_boiler::BoilerRole::ASSIST_CM3, oq_boiler::BLOCK_TARGET_SATISFIED}, codes);
  assert(oq_boiler::boiler_log_reason_is_normal(log.reason));
  assert(log.reason != oq_boiler::BoilerLogReason::SENSOR_FALLBACK);
  assert(log.reason != oq_boiler::BoilerLogReason::FLOW_TOO_LOW);
  assert(log.reason != oq_boiler::BoilerLogReason::SOFT_GUARD);
}

void test_minimum_off_time_still_delays_a_new_start() {
  const RelayTargetDecision start = requesting_start();
  assert(start.requested_active);
  auto input = guarded_input(start);
  input.output_last_change_ms = 4900U;
  input.min_off_ms = 120000U;
  const auto decision = oq_boiler::evaluate(power_house_command(), input);
  assert(!decision.output_active);
  assert(decision.block_reason == oq_boiler::BLOCK_MIN_OFF_TIME);
  assert(decision.blocked);
}

void test_unusable_target_control_reason_is_reported_as_a_block() {
  auto in = target_input();
  in.output_active = true;
  in.supply_c = NAN;
  const auto unavailable = oq_boiler::evaluate_relay_target(in);
  assert(unavailable.state == oq_boiler::RELAY_TARGET_UNAVAILABLE);
  const auto decision = oq_boiler::evaluate(power_house_command(), guarded_input(unavailable));
  assert(!decision.output_active);
  assert(decision.blocked);
  assert(decision.block_reason == oq_boiler::BLOCK_TARGET_INVALID);
}

void test_power_house_without_a_reachable_target_refuses_to_energise() {
  // Power House keeps a valid command with a heat request even when the
  // hydraulic target cannot be computed, for example because the boiler inlet
  // already sits at the maximum water temperature. The command stays visible
  // and transport-neutral; R1 must then decline to invent a temperature
  // decision, exactly as the OpenTherm path already blocks on an invalid
  // target.
  oq_boiler_dispatch::Inputs in;
  in.control_mode = 3;
  in.now_ms = 5000U;
  in.heat_request = true;
  in.rated_boiler_power_w = 10000.0f;
  in.maximum_water_temperature_c = 60.0f;
  in.strategy = 3;
  in.strategy_output_valid = true;
  in.strategy_output_source = 3;
  in.strategy_updated_ms = 4000U;
  in.strategy_requested_power_w = 9000.0f;
  in.hp_expected_power_w = 2000.0f;
  in.flow_lph = 720.0f;
  in.boiler_inlet_c = 60.0f;  // At the cap: no reachable target.
  const auto command = oq_boiler_dispatch::dispatch(in);
  assert(command.valid);
  assert(command.demand_present);
  assert(command.heat_request);
  assert(command.source == oq_boiler::COMMAND_SOURCE_POWER_HOUSE);
  assert(isnan(command.target_temperature_c));

  RelayTargetInput relay;
  relay.applicable = oq_boiler::relay_target_control_applies(command.source, false);
  relay.heat_request = command.heat_request;
  relay.supply_c = 45.0f;
  relay.target_c = command.target_temperature_c;
  relay.config = kConfig;
  const auto relay_target = oq_boiler::evaluate_relay_target(relay);
  assert(relay_target.state == oq_boiler::RELAY_TARGET_UNAVAILABLE);

  auto controller_input = guarded_input(relay_target);
  controller_input.now_ms = 5000U;
  const auto relay_decision = oq_boiler::evaluate(command, controller_input);
  assert(!relay_decision.output_active);
  assert(relay_decision.blocked);
  assert(relay_decision.block_reason == oq_boiler::BLOCK_TARGET_INVALID);

  // The OpenTherm path already refused this command, and stays unchanged.
  auto opentherm_input = guarded_input(relay_target);
  opentherm_input.target_required = true;
  opentherm_input.target_valid = false;
  const auto opentherm_decision = oq_boiler::evaluate(command, opentherm_input);
  assert(!opentherm_decision.output_active);
  assert(opentherm_decision.block_reason == oq_boiler::BLOCK_TARGET_INVALID);
}

// A defrosting outdoor unit cannot contribute heat, so it shows up in the
// boiler chain only as reduced expected heat-pump power. That is how the
// behaviour below models HP1 and HP2 in a defrost.
struct DefrostCase {
  const char* label;
  bool hp1_defrost;
  bool hp2_defrost;
  float hp1_expected_power_w;
  float hp2_expected_power_w;
  float requested_power_w;
  bool duo;
};

// Single: the only outdoor unit is defrosting. Duo: one unit defrosts while the
// peer is healthy, in both directions.
constexpr DefrostCase kDefrostCases[] = {
    {"single/hp1-defrost", true, false, 0.0f, 0.0f, 9000.0f, false},
    {"duo/hp1-defrost", true, false, 0.0f, 5000.0f, 12000.0f, true},
    {"duo/hp2-defrost", false, true, 5000.0f, 0.0f, 12000.0f, true},
};

oq_boiler_dispatch::Inputs power_house_inputs(const DefrostCase& scenario, uint32_t now_ms) {
  oq_boiler_dispatch::Inputs in;
  in.now_ms = now_ms;
  in.heat_request = true;
  in.rated_boiler_power_w = 10000.0f;
  in.maximum_water_temperature_c = 60.0f;
  in.strategy = 3;
  in.strategy_output_valid = true;
  in.strategy_output_source = 3;
  in.strategy_updated_ms = now_ms - 1000U;
  in.strategy_requested_power_w = scenario.requested_power_w;
  in.flow_lph = 720.0f;
  in.boiler_inlet_c = 35.0f;
  if (scenario.duo) {
    in.hp_expected_power_w = scenario.hp1_expected_power_w + scenario.hp2_expected_power_w;
  } else {
    in.hp_expected_power_w = scenario.hp1_expected_power_w;
  }
  return in;
}

void test_boiler_chain_has_no_defrost_input() {
  static_assert(!HasDefrostInput<oq_boiler_dispatch::Inputs>::value,
                "the boiler dispatcher must not accept a defrost observation");
  static_assert(!HasDefrostInput<oq_boiler::ControllerInput>::value,
                "the boiler controller must not accept a defrost observation");
  static_assert(!HasDefrostInput<oq_boiler::RelayTargetInput>::value,
                "the R1 target controller must not accept a defrost observation");
}

void test_defrost_alone_never_activates_the_boiler_from_cm2() {
  // A defrost observation may only reach the boiler through the normal CM3
  // promotion. The heat deficit it creates is real, so the contrast case below
  // proves the same thermal situation does drive the boiler once CM3 owns it.
  for (const DefrostCase& scenario : kDefrostCases) {
    const auto inputs = power_house_inputs(scenario, 5000U);

    // CM2: heat demand, assist permission, valid flow and an available
    // transport, but no boiler ownership. The relay stays off.
    auto in_cm2 = inputs;
    in_cm2.control_mode = 2;
    const auto cm2_command = oq_boiler_dispatch::dispatch(in_cm2);
    assert(cm2_command.valid);
    assert(!cm2_command.demand_present);
    assert(!cm2_command.heat_request);
    assert(cm2_command.source == oq_boiler::COMMAND_SOURCE_NONE);

    auto controller_input = guarded_input(requesting_start());
    const auto cm2_decision = oq_boiler::evaluate(cm2_command, controller_input);
    assert(cm2_decision.force_off);
    assert(!cm2_decision.output_active);
    assert(cm2_decision.block_reason == oq_boiler::BLOCK_NO_HEAT_REQUEST);
    // No R1 target control and no OpenTherm CH-enable either.
    assert(!oq_boiler::relay_target_control_applies(cm2_command.source, false));
    assert(!oq_boiler::relay_target_control_applies(cm2_command.source, true));
    // No role is held, so the supervisory CM edge withdraws the request.
    assert(oq_boiler::boiler_role_for_source(cm2_command.source) == oq_boiler::BoilerRole::OFF);

    // CM3 for the identical thermal situation: the same deficit now does
    // produce a boiler command, which is what makes the CM2 result meaningful.
    auto in_cm3 = inputs;
    in_cm3.control_mode = 3;
    const auto cm3_command = oq_boiler_dispatch::dispatch(in_cm3);
    assert(cm3_command.valid);
    assert(cm3_command.demand_present);
    assert(cm3_command.heat_request);
    assert(cm3_command.source == oq_boiler::COMMAND_SOURCE_POWER_HOUSE);
    assert(cm3_command.requested_power_w > 0.0f);

    // R1 regulates that command; OpenTherm would transmit it unchanged.
    controller_input.target_valid = true;
    RelayTargetInput relay;
    relay.applicable = oq_boiler::relay_target_control_applies(cm3_command.source, false);
    relay.heat_request = cm3_command.heat_request;
    relay.supply_c = 30.0f;
    relay.target_c = cm3_command.target_temperature_c;
    relay.config = kConfig;
    const auto cm3_decision = oq_boiler::evaluate(cm3_command, guarded_input(oq_boiler::evaluate_relay_target(relay)));
    assert(cm3_decision.output_active);
    assert(!cm3_decision.blocked);
    assert(cm3_decision.block_reason == oq_boiler::BLOCK_NONE);
  }
}

void test_satisfied_target_never_reports_a_blocked_boiler() {
  // A satisfied target must not be re-reported as blocked anywhere the runtime
  // or the decision log look at it, or the UI would show a fault.
  const auto satisfied = satisfied_target();
  const auto decision = oq_boiler::evaluate(power_house_command(), guarded_input(satisfied));
  assert(!decision.output_active);
  assert(!decision.blocked);
  assert(decision.block_reason == oq_boiler::BLOCK_TARGET_SATISFIED);

  // The runtime uses the controller verdict directly instead of recomputing
  // "blocked" from the output state, which is what keeps a satisfied target out
  // of the "Boiler blocked" log line and the decision_blocked event.
  const bool recomputed_blocked = decision.demand_present && !decision.output_active;
  assert(recomputed_blocked);  // The naive computation would say "blocked".
  assert(!decision.blocked);   // The controller verdict does not.
}

void test_relay_target_state_text_covers_every_state() {
  const char* const states[] = {
      oq_boiler::relay_target_state_text(oq_boiler::RELAY_TARGET_NOT_APPLICABLE),
      oq_boiler::relay_target_state_text(oq_boiler::RELAY_TARGET_IDLE),
      oq_boiler::relay_target_state_text(oq_boiler::RELAY_TARGET_START),
      oq_boiler::relay_target_state_text(oq_boiler::RELAY_TARGET_HOLD),
      oq_boiler::relay_target_state_text(oq_boiler::RELAY_TARGET_SATISFIED),
      oq_boiler::relay_target_state_text(oq_boiler::RELAY_TARGET_UNAVAILABLE),
  };
  for (const char* text : states) {
    assert(text != nullptr);
    assert(strlen(text) > 0);
  }
  assert(strcmp(states[0], "not applicable") == 0);
  assert(strstr(states[1], "idle") != nullptr);
  assert(strstr(states[2], "below target") != nullptr);
  assert(strstr(states[3], "inside target band") != nullptr);
  assert(strstr(states[4], "satisfied") != nullptr);
  assert(strstr(states[5], "unavailable") != nullptr);
}

}  // namespace

int main() {
  test_no_heat_request_releases_the_relay();
  test_relay_starts_below_the_start_threshold();
  test_relay_stays_off_inside_the_band();
  test_relay_stays_on_inside_the_band();
  test_relay_stops_at_the_stop_threshold();
  test_exact_boundaries_are_deterministic();
  test_invalid_hysteresis_policy_is_rejected();
  test_unusable_measurements_fail_safe();
  test_target_changes_move_the_request();
  test_only_normal_auxiliary_heat_sources_use_target_control();
  test_source_policy_reaches_the_relay_decision();
  test_cm4_fallback_keeps_its_on_off_semantics();
  test_cm100_commissioning_keeps_its_on_off_semantics();
  test_opentherm_path_is_unaffected_by_relay_target_control();
  test_safety_guards_win_over_target_control();
  test_transport_change_keeps_break_before_make();
  test_satisfied_target_is_not_a_blocked_boiler();
  test_satisfied_target_respects_minimum_on_time();
  test_satisfied_target_keeps_the_command_visible();
  test_minimum_off_time_still_delays_a_new_start();
  test_unusable_target_control_reason_is_reported_as_a_block();
  test_power_house_without_a_reachable_target_refuses_to_energise();
  test_boiler_chain_has_no_defrost_input();
  test_defrost_alone_never_activates_the_boiler_from_cm2();
  test_satisfied_target_never_reports_a_blocked_boiler();
  test_relay_target_state_text_covers_every_state();
  return 0;
}
