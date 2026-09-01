#include <assert.h>

#include <cmath>
#include <cstdint>
#include <limits>

#include "../../openquatt/includes/control/oq_supervisory_safety_logic.h"

namespace {
oq_supervisory_safety::Config config() { return {250.0f, 60000, 30000, 5.0f, 6.0f, 30000}; }

oq_supervisory_safety::Input input(uint32_t now_ms, bool thermal_request, float flow_lph, float outside_c = 10.0f) {
  return {now_ms, thermal_request, true, flow_lph, true, outside_c};
}
}  // namespace

int main() {
  using namespace oq_supervisory_safety;

  State state;
  auto output = step(input(1000, true, 249.0f), config(), state);
  assert(output.flow_valid && output.flow_low && !output.flow_ok && !output.minimum_flow_ok);
  assert(!output.state.low_flow_fault_active && !output.low_flow_fault_started);
  output = step(input(60999, true, 249.0f), config(), output.state);
  assert(!output.state.low_flow_fault_active);
  output = step(input(61000, true, 249.0f), config(), output.state);
  assert(output.state.low_flow_fault_active && output.low_flow_fault_started);

  output = step(input(62000, true, 250.0f), config(), output.state);
  assert(output.flow_ok && output.state.low_flow_fault_active && !output.minimum_flow_ok);
  output = step(input(91999, true, 400.0f), config(), output.state);
  assert(output.state.low_flow_fault_active);
  output = step(input(92000, true, 400.0f), config(), output.state);
  assert(!output.state.low_flow_fault_active && output.low_flow_fault_cleared && output.minimum_flow_ok);

  output = step(input(93000, true, 100.0f), config(), output.state);
  output = step(input(153000, true, 100.0f), config(), output.state);
  assert(output.state.low_flow_fault_active);
  output = step(input(153001, false, 100.0f), config(), output.state);
  assert(!output.state.low_flow_fault_active && output.low_flow_fault_cleared && output.minimum_flow_ok);
  assert(!output.state.low_flow_timing && !output.state.flow_recovery_timing);

  output = step(input(1000, true, 100.0f), config(), {});
  output = step(input(30000, true, 300.0f), config(), output.state);
  output = step(input(90000, true, 100.0f), config(), output.state);
  output = step(input(149999, true, 100.0f), config(), output.state);
  assert(!output.state.low_flow_fault_active);
  output.state.low_flow_fault_active = true;
  output = step(input(150000, true, 300.0f), config(), output.state);
  output = step(input(160000, true, 100.0f), config(), output.state);
  assert(output.state.low_flow_fault_active && !output.state.flow_recovery_timing);
  output = step(input(170000, true, 300.0f), config(), output.state);
  output = step(input(199999, true, 300.0f), config(), output.state);
  assert(output.state.low_flow_fault_active);

  auto invalid_flow = input(2000, true, NAN);
  invalid_flow.flow_has_state = false;
  output = step(invalid_flow, config(), {});
  assert(!output.flow_valid && output.flow_low && !output.flow_ok);
  invalid_flow.flow_has_state = true;
  invalid_flow.flow_lph = std::numeric_limits<float>::infinity();
  output = step(invalid_flow, config(), {});
  assert(!output.flow_valid && output.flow_low);
  auto invalid_flow_config = config();
  invalid_flow_config.minimum_flow_lph = NAN;
  output = step(input(2000, true, 1000.0f), invalid_flow_config, {});
  assert(output.flow_valid && output.flow_low && !output.flow_ok);

  auto immediate_config = config();
  immediate_config.low_flow_fault_ms = 0;
  immediate_config.flow_recover_ms = 0;
  output = step(input(0, true, 100.0f), immediate_config, {});
  assert(output.state.low_flow_fault_active);
  output = step(input(0, true, 300.0f), immediate_config, output.state);
  assert(!output.state.low_flow_fault_active);

  State rollover_state;
  output = step(input(UINT32_MAX - 20, true, 100.0f), config(), rollover_state);
  auto rollover_config = config();
  rollover_config.low_flow_fault_ms = 60;
  output = step(input(39, true, 100.0f), rollover_config, output.state);
  assert(output.state.low_flow_fault_active);
  assert(seconds_to_ms(UINT32_MAX) == (UINT32_MAX / 1000UL) * 1000UL);

  output = step(input(100, false, 300.0f, 4.9f), config(), {});
  assert(output.frost_active);
  output = step(input(200, false, 300.0f, 5.5f), config(), output.state);
  assert(output.frost_active);
  output = step(input(300, false, 300.0f, 6.0f), config(), output.state);
  assert(!output.frost_active);
  output = step(input(400, false, 300.0f, 5.0f), config(), output.state);
  assert(!output.frost_active);
  output = step(input(500, true, 300.0f, 0.0f), config(), output.state);
  assert(!output.frost_active);

  // On a cold boot, the gap between the on/off thresholds must fail safe.
  output = step(input(100, false, 300.0f, 5.5f), config(), {});
  assert(output.frost_active);
  output = step(input(200, false, 300.0f, 6.0f), config(), {});
  assert(!output.frost_active);
  output = step(input(300, false, 300.0f, 5.5f), config(), output.state);
  assert(!output.frost_active);

  auto missing_outside = input(1000, false, 300.0f, NAN);
  missing_outside.outside_temperature_has_state = false;
  output = step(missing_outside, config(), {});
  assert(output.frost_nan_grace_active && !output.frost_active);
  auto delayed_outside = input(2000, false, 300.0f, 5.5f);
  output = step(delayed_outside, config(), output.state);
  assert(output.frost_active && output.state.frost_initialized);
  output = step(missing_outside, config(), {});
  missing_outside.now_ms = 30999;
  output = step(missing_outside, config(), output.state);
  assert(output.frost_nan_grace_active && !output.frost_active);
  missing_outside.now_ms = 31000;
  output = step(missing_outside, config(), output.state);
  assert(!output.frost_nan_grace_active && output.frost_active);
  missing_outside.outside_temperature_has_state = true;
  missing_outside.outside_temperature_c = std::numeric_limits<float>::infinity();
  output = step(missing_outside, config(), output.state);
  assert(output.frost_active);

  State active_frost_hysteresis;
  active_frost_hysteresis.initialized = true;
  active_frost_hysteresis.frost_initialized = true;
  active_frost_hysteresis.frost_active = true;
  output = step(input(1000, false, 300.0f, 5.5f), config(), active_frost_hysteresis);
  assert(output.frost_active);
  auto invalid_frost_config = config();
  invalid_frost_config.frost_on_c = 7.0f;
  invalid_frost_config.frost_off_c = 6.0f;
  invalid_frost_config.frost_nan_grace_ms = 0;
  output = step(input(1000, false, 300.0f, 10.0f), invalid_frost_config, {});
  assert(output.frost_active);

  auto rollover_frost_config = config();
  rollover_frost_config.frost_nan_grace_ms = 30;
  missing_outside.now_ms = UINT32_MAX - 10;
  output = step(missing_outside, rollover_frost_config, {});
  missing_outside.now_ms = 18;
  output = step(missing_outside, rollover_frost_config, output.state);
  assert(output.frost_nan_grace_active && !output.frost_active);
  missing_outside.now_ms = 19;
  output = step(missing_outside, rollover_frost_config, output.state);
  assert(!output.frost_nan_grace_active && output.frost_active);

  assert(!force_standby(false, false, 5, 5));
  assert(force_standby(true, false, 5, 5));
  assert(!force_standby(true, true, 5, 0));
  assert(!force_standby(true, true, 0, 5));
  assert(force_standby(true, true, 0, 0));
  return 0;
}
