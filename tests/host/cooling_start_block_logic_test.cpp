#include <assert.h>
#include <cstring>

#include "../../openquatt/includes/control/oq_cooling_start_block_logic.h"

using namespace oq_cooling_start_block;

static Inputs base_inputs() {
  Inputs in;
  in.cooling_demand_active = true;
  in.any_hp_running = false;
  in.duo = false;
  return in;
}

void test_no_demand_reports_ready() {
  Inputs in = base_inputs();
  in.cooling_demand_active = false;
  in.cooling_remaining_ms = 120000U;
  in.hp1_rest_remaining_ms = 60000U;
  const auto out = resolve(in);
  assert(!out.blocked);
  assert(std::strcmp(out.reason, kReady) == 0);
  assert(!out.has_countdown);
}

void test_running_reports_ready() {
  Inputs in = base_inputs();
  in.any_hp_running = true;
  in.cooling_remaining_ms = 120000U;
  in.hp1_rest_remaining_ms = 60000U;
  const auto out = resolve(in);
  assert(!out.blocked);
  assert(std::strcmp(out.reason, kReady) == 0);
}

void test_cooling_min_off_wins_over_general() {
  Inputs in = base_inputs();
  in.cooling_remaining_ms = 190000U;
  in.hp1_rest_remaining_ms = 60000U;
  const auto out = resolve(in);
  assert(out.blocked);
  assert(std::strcmp(out.reason, kCoolingMinOff) == 0);
  assert(out.has_countdown);
  assert(out.remaining_ms == 190000U);
  assert(remaining_seconds_ceil(out.remaining_ms) == 190U);
}

void test_confirmation_pending_has_no_countdown() {
  Inputs in = base_inputs();
  in.cooling_confirmation_pending = true;
  const auto out = resolve(in);
  assert(out.blocked);
  assert(std::strcmp(out.reason, kCoolingConfirm) == 0);
  assert(!out.has_countdown);
  assert(out.remaining_ms == 0);
}

void test_general_restart_with_countdown() {
  Inputs in = base_inputs();
  in.hp1_rest_remaining_ms = 190000U;
  const auto out = resolve(in);
  assert(out.blocked);
  assert(std::strcmp(out.reason, kCompressorRestart) == 0);
  assert(out.has_countdown);
  assert(remaining_seconds_ceil(out.remaining_ms) == 190U);
}

void test_startup_inhibit_distinguished_from_general() {
  Inputs in = base_inputs();
  in.hp1_rest_remaining_ms = 180000U;
  in.hp1_startup_inhibited = true;
  const auto out = resolve(in);
  assert(out.blocked);
  assert(std::strcmp(out.reason, kStartupInhibit) == 0);
  assert(out.has_countdown);
}

void test_start_limit_after_rest_guards() {
  Inputs in = base_inputs();
  in.hp1_start_limit_remaining_ms = 420000U;
  const auto out = resolve(in);
  assert(out.blocked);
  assert(std::strcmp(out.reason, kStartLimit) == 0);
  assert(out.has_countdown);
}

void test_other_block_has_no_countdown() {
  Inputs in = base_inputs();
  in.dispatch_blocked_other = true;
  const auto out = resolve(in);
  assert(out.blocked);
  assert(std::strcmp(out.reason, kOtherBlocked) == 0);
  assert(!out.has_countdown);
}

void test_expiry_clears_block() {
  Inputs in = base_inputs();
  in.hp1_rest_remaining_ms = 1000U;
  assert(resolve(in).blocked);
  in.hp1_rest_remaining_ms = 0U;
  const auto out = resolve(in);
  assert(!out.blocked);
  assert(std::strcmp(out.reason, kReady) == 0);
}

void test_duo_uses_max_remaining() {
  Inputs in = base_inputs();
  in.duo = true;
  in.hp1_rest_remaining_ms = 30000U;
  in.hp2_rest_remaining_ms = 190000U;
  const auto out = resolve(in);
  assert(out.blocked);
  assert(std::strcmp(out.reason, kCompressorRestart) == 0);
  assert(out.remaining_ms == 190000U);
}

void test_duo_ignores_hp2_when_single() {
  Inputs in = base_inputs();
  in.duo = false;
  in.hp2_rest_remaining_ms = 190000U;
  const auto out = resolve(in);
  assert(!out.blocked);
}

void test_time_bound_reasons_only() {
  assert(is_time_bound_reason(kCoolingMinOff));
  assert(is_time_bound_reason(kCompressorRestart));
  assert(is_time_bound_reason(kStartupInhibit));
  assert(is_time_bound_reason(kStartLimit));
  assert(!is_time_bound_reason(kReady));
  assert(!is_time_bound_reason(kCoolingConfirm));
  assert(!is_time_bound_reason(kOtherBlocked));
}

int main() {
  test_no_demand_reports_ready();
  test_running_reports_ready();
  test_cooling_min_off_wins_over_general();
  test_confirmation_pending_has_no_countdown();
  test_general_restart_with_countdown();
  test_startup_inhibit_distinguished_from_general();
  test_start_limit_after_rest_guards();
  test_other_block_has_no_countdown();
  test_expiry_clears_block();
  test_duo_uses_max_remaining();
  test_duo_ignores_hp2_when_single();
  test_time_bound_reasons_only();
  return 0;
}
