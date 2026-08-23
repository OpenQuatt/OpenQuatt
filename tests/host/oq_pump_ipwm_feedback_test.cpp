#include "openquatt/includes/diagnostics/oq_pump_ipwm_feedback.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace oq_pump_ipwm;

namespace {

void expect_status(Profile profile, uint16_t raw, Status expected) { assert(decode(profile, raw).status == expected); }

void test_wilo_boundaries() {
  constexpr Profile kFlow = Profile::WILO_FLOW;
  expect_status(kFlow, 0U, Status::PWM_SHORT);
  expect_status(kFlow, 20U, Status::STANDBY);
  expect_status(kFlow, 40U, Status::UNKNOWN);
  expect_status(kFlow, 50U, Status::RUNNING);
  expect_status(kFlow, 750U, Status::RUNNING);
  expect_status(kFlow, 760U, Status::UNKNOWN);
  expect_status(kFlow, 800U, Status::PUMP_ON_ABNORMAL);
  expect_status(kFlow, 840U, Status::UNKNOWN);
  expect_status(kFlow, 850U, Status::PUMP_OFF_ABNORMAL);
  expect_status(kFlow, 900U, Status::PUMP_OFF_ABNORMAL);
  expect_status(kFlow, 910U, Status::UNKNOWN);
  expect_status(kFlow, 950U, Status::PUMP_OFF_FAILURE);
  expect_status(kFlow, 960U, Status::UNKNOWN);
  expect_status(kFlow, 1000U, Status::PWM_OPEN);
  expect_status(kFlow, 1010U, Status::UNKNOWN);
  expect_status(kFlow, UINT16_MAX, Status::UNKNOWN);
}

void test_profile_scopes_power() {
  const DecodedFeedback unknown = decode(Profile::UNKNOWN, 500U);
  assert(unknown.status == Status::UNKNOWN);
  assert(!unknown.power_valid);

  const DecodedFeedback flow = decode(Profile::WILO_FLOW, 500U);
  assert(flow.status == Status::RUNNING);
  assert(!flow.power_valid);

  const DecodedFeedback power = decode(Profile::WILO_POWER_5_75_W, 500U);
  assert(power.status == Status::RUNNING);
  assert(power.power_valid);
  assert(power.power_w == 50.0F);
  assert(power_contribution_w(power, true, true) == 50.0F);
  assert(power_contribution_w(power, true, false) == 0.0F);
  assert(power_contribution_w(power, false, true) == 0.0F);

  const DecodedFeedback diagnostic = decode(Profile::WILO_POWER_5_75_W, 950U);
  assert(diagnostic.status == Status::PUMP_OFF_FAILURE);
  assert(!diagnostic.power_valid);
  assert(power_contribution_w(diagnostic, true, true) == 0.0F);
}

void test_option_mapping_fails_closed() {
  assert(profile_from_option("Wilo flow feedback") == Profile::WILO_FLOW);
  assert(profile_from_option("Wilo 5-75 W feedback") == Profile::WILO_POWER_5_75_W);
  assert(profile_from_option("Unknown / other") == Profile::UNKNOWN);
  assert(profile_from_option("AWMT") == Profile::UNKNOWN);
  assert(profile_from_option(nullptr) == Profile::UNKNOWN);
}

void test_raw_observation_retains_recent_diagnostic_context_and_is_wrap_safe() {
  assert(context_raw_observation_index(2010U) == 0U);
  assert(context_raw_observation_index(2137U) == 3U);
  assert(context_raw_observation_index(2138U) == kContextRawObservationCount);
  assert(kDiagnosticContextFreshnessMs == 20000U);

  ContextRawObservation observation;
  uint16_t raw = 0U;
  observation.observe(950U, 100U);
  // The caller may still pass the 5 s Modbus base interval, but diagnostic
  // context remains usable for the 20 s operator-facing freshness window.
  assert(observation.consume_if_fresh(15100U, 5000U, raw));
  assert(raw == 950U);
  // Reading the context does not consume it; another snapshot may reuse the
  // same recent last-known-good observation.
  assert(observation.consume_if_fresh(15100U, 5000U, raw));
  assert(!observation.consume_if_fresh(20100U, 5000U, raw));

  observation.observe(20U, UINT32_MAX - 4U);
  assert(observation.consume_if_fresh(3U, 5000U, raw));
  assert(raw == 20U);
}

}  // namespace

int main() {
  test_wilo_boundaries();
  test_profile_scopes_power();
  test_option_mapping_fails_closed();
  test_raw_observation_retains_recent_diagnostic_context_and_is_wrap_safe();
  std::cout << "Pump iPWM feedback tests passed\n";
  return 0;
}
