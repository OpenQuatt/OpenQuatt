#include "openquatt/includes/diagnostics/oq_pump_ipwm_feedback.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace oq_pump_ipwm;

namespace {

void expect_status(uint16_t raw, Status expected) { assert(decode(raw).status == expected); }

void test_wilo_boundaries() {
  expect_status(0U, Status::PWM_SHORT);
  expect_status(20U, Status::STANDBY);
  expect_status(40U, Status::UNKNOWN);
  expect_status(50U, Status::RUNNING);
  expect_status(750U, Status::RUNNING);
  expect_status(760U, Status::UNKNOWN);
  expect_status(800U, Status::PUMP_ON_ABNORMAL);
  expect_status(840U, Status::UNKNOWN);
  expect_status(850U, Status::PUMP_OFF_ABNORMAL);
  expect_status(900U, Status::PUMP_OFF_ABNORMAL);
  expect_status(910U, Status::UNKNOWN);
  expect_status(950U, Status::PUMP_OFF_FAILURE);
  expect_status(960U, Status::UNKNOWN);
  expect_status(1000U, Status::PWM_OPEN);
  expect_status(1010U, Status::UNKNOWN);
  expect_status(UINT16_MAX, Status::UNKNOWN);
}

void test_power_band_is_fixed_and_fault_codes_never_count_as_power() {
  const DecodedFeedback running = decode(500U);
  assert(running.status == Status::RUNNING);
  assert(running.power_valid);
  assert(running.power_w == 50.0F);
  assert(power_contribution_w(running, true, true) == 50.0F);
  assert(power_contribution_w(running, true, false) == 0.0F);
  assert(power_contribution_w(running, false, true) == 0.0F);

  const DecodedFeedback diagnostic = decode(950U);
  assert(diagnostic.status == Status::PUMP_OFF_FAILURE);
  assert(!diagnostic.power_valid);
  assert(power_contribution_w(diagnostic, true, true) == 0.0F);
}

}  // namespace

int main() {
  test_wilo_boundaries();
  test_power_band_is_fixed_and_fault_codes_never_count_as_power();
  std::cout << "Pump iPWM feedback tests passed\n";
  return 0;
}
