#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_power_house_demand_logic.h"

namespace {
using namespace oq_power_house;
DemandInput input(uint32_t now_ms) { return {now_ms, 3, -10, 16, 6000, 20, 20, NAN, 1, false}; }
DemandTuning tuning() { return {0.5f, 3000, 0.1f, 0.3f, 10, 5, 20}; }
bool near(float value, float expected) { return fabsf(value - expected) < 0.01f; }

void test_inputs_arrive_in_any_order() {
  const int orders[][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  float DemandInput::* fields[] = {&DemandInput::outside_c, &DemandInput::room_c, &DemandInput::setpoint_c};
  for (const auto& order : orders) {
    auto in = input(4000U);
    in.outside_c = in.room_c = in.setpoint_c = NAN;
    DemandState state;
    auto phase = StartupPhase::WAITING_INPUTS;
    const auto missing = decide_startup_demand(in, tuning(), state, phase);
    assert(!missing.valid && isnan(missing.requested_w) && missing.raw_demand == 0);
    assert(missing.next.last_ms == 0 && missing.next.last_w == 0 && missing.next.comfort_memory_c == 0);
    for (int index = 0; index < 3; ++index) {
      in.now_ms = 30000U + 30000U * index;
      in.*fields[order[index]] = input(in.now_ms).*fields[order[index]];
      const auto out = decide_startup_demand(in, tuning(), state, phase);
      state = out.next;
      if (index < 2) {
        assert(phase == StartupPhase::WAITING_INPUTS && !out.valid && isnan(out.requested_w));
        assert(state.last_ms == 0 && state.last_w == 0);
      } else {
        assert(phase == StartupPhase::READY && out.valid && near(out.requested_w, 3000));
        assert(out.raw_demand == 10 && state.last_ms == in.now_ms);
      }
    }
  }
}

void test_first_setpoint_does_not_wait_for_normal_cadence() {
  auto in = input(4000U);
  in.setpoint_c = NAN;
  auto phase = StartupPhase::WAITING_INPUTS;
  auto out = decide_startup_demand(in, tuning(), {}, phase);
  in = input(51000U);
  // A normal 60-second control cadence is not due yet; acquisition is.
  assert(!decide_cadence(in.now_ms, 4000U, 60000U).due);
  out = decide_startup_demand(in, tuning(), out.next, phase);
  assert(phase == StartupPhase::READY && out.valid && near(out.requested_w, 3000));
}

void test_timeout_is_from_boot_and_survives_rollover() {
  auto in = input(kStartupInputWaitMs - 1U);
  in.setpoint_c = NAN;
  auto phase = StartupPhase::WAITING_INPUTS;
  auto out = decide_startup_demand(in, tuning(), {}, phase);
  assert(phase == StartupPhase::WAITING_INPUTS && isnan(out.requested_w));
  in.now_ms = kStartupInputWaitMs;
  out = decide_startup_demand(in, tuning(), out.next, phase);
  assert(phase == StartupPhase::INPUT_TIMEOUT && !out.valid && out.requested_w == 0 && out.raw_demand == 0);
  assert(out.next.last_ms == 0);
  in.now_ms = UINT32_MAX;
  out = decide_startup_demand(in, tuning(), out.next, phase);
  in.now_ms = 1U;
  out = decide_startup_demand(in, tuning(), out.next, phase);
  assert(phase == StartupPhase::INPUT_TIMEOUT && out.requested_w == 0 && out.next.last_ms == 0);
  // Late first data still establishes a real baseline, never a synthetic zero.
  in = input(10000U);
  out = decide_startup_demand(in, tuning(), out.next, phase);
  assert(phase == StartupPhase::READY && out.valid && near(out.requested_w, 3000));
  // Inactive strategies observe the same boot deadline, rather than re-arming it.
  phase = update_startup_phase(StartupPhase::WAITING_INPUTS, kStartupInputWaitMs, false);
  assert(phase == StartupPhase::INPUT_TIMEOUT);
  assert(update_startup_phase(phase, 1U, false) == StartupPhase::INPUT_TIMEOUT);
}

void test_valid_at_deadline_and_valid_zero_demand() {
  auto phase = StartupPhase::WAITING_INPUTS;
  auto out = decide_startup_demand(input(kStartupInputWaitMs), tuning(), {}, phase);
  assert(phase == StartupPhase::READY && out.valid && near(out.requested_w, 3000));
  auto in = input(4000U);
  in.room_c = 22;
  phase = StartupPhase::WAITING_INPUTS;
  out = decide_startup_demand(in, tuning(), {}, phase);
  assert(phase == StartupPhase::READY && out.valid && out.requested_w == 0);
}

void test_invalid_required_data_and_optional_external_demand() {
  float DemandInput::* fields[] = {&DemandInput::outside_c, &DemandInput::room_c, &DemandInput::setpoint_c,
                                   &DemandInput::rated_w, &DemandInput::water_limit_factor};
  const float bad_values[] = {NAN, INFINITY, -INFINITY};
  for (float bad : bad_values) {
    for (auto field : fields) {
      auto in = input(4000U);
      in.*field = bad;
      auto phase = StartupPhase::WAITING_INPUTS;
      const auto out = decide_startup_demand(in, tuning(), {}, phase);
      assert(phase == StartupPhase::WAITING_INPUTS && !out.valid && out.next.last_ms == 0);
    }
    auto in = input(4000U);
    in.external_w = bad;
    in.external_valid = true;
    auto phase = StartupPhase::WAITING_INPUTS;
    const auto out = decide_startup_demand(in, tuning(), {}, phase);
    assert(phase == StartupPhase::READY && out.valid && !out.external && near(out.requested_w, 3000));
  }
}

void test_runtime_input_loss_keeps_existing_stop_and_slew() {
  auto phase = StartupPhase::WAITING_INPUTS;
  auto out = decide_startup_demand(input(4000U), tuning(), {}, phase);
  auto in = input(60001U);
  in.room_c = NAN;
  out = decide_startup_demand(in, tuning(), out.next, phase);
  assert(phase == StartupPhase::READY && !out.valid && out.requested_w == 0);
  assert(out.next.last_ms == in.now_ms && out.next.last_w == 0);
  in = input(120001U);
  out = decide_startup_demand(in, tuning(), out.next, phase);
  assert(near(out.requested_w, 600));
  // A strategy reset clears demand, but does not renew a boot grace period.
  in = input(4000U);
  in.room_c = NAN;
  out = decide_startup_demand(in, tuning(), {}, phase);
  assert(phase == StartupPhase::READY && !out.valid && out.requested_w == 0 && out.next.last_ms == 4000U);
}
}  // namespace

int main() {
  test_inputs_arrive_in_any_order();
  test_first_setpoint_does_not_wait_for_normal_cadence();
  test_timeout_is_from_boot_and_survives_rollover();
  test_valid_at_deadline_and_valid_zero_demand();
  test_invalid_required_data_and_optional_external_demand();
  test_runtime_input_loss_keeps_existing_stop_and_slew();
}
