#include <assert.h>
#include <cmath>
#include <cstdint>
#include <limits>

#include "../../openquatt/includes/control/oq_supervisory_power_limiter_logic.h"

namespace {
oq_supervisory_power::HpMeasurement hp(uint32_t updated_ms = 1000) {
  return {true, true, true, updated_ms, updated_ms};
}

oq_supervisory_power::Input input(uint32_t now_ms, float power_w) { return {now_ms, hp(), hp(), true, power_w}; }

oq_supervisory_power::Config config(bool duo = true) {
  return {duo, 5, 15, 30, 10, 60000, duo ? 16 : 8, 20, 3400.0f, 3650.0f, 3300.0f};
}
}  // namespace

int main() {
  using namespace oq_supervisory_power;
  assert(maximum_current_a(true, false, 16.0f, 20.0f) == 16.0f);
  assert(maximum_current_a(true, true, 16.0f, 20.0f) == 20.0f);
  assert(maximum_current_a(false, true, 16.0f, 20.0f) == 16.0f);
  assert(standard_current_a(true, false, 16.0f, 20.0f) == 16.0f);
  assert(standard_current_a(true, true, 16.0f, 20.0f) == 20.0f);
  assert(standard_current_a(false, true, 16.0f, 20.0f) == 16.0f);
  assert(absolute_maximum_current_a(false, true, true, false, false, 16.0f, 20.0f, 26.0f) == 16.0f);
  // Duo V1 with matching ODU detection may use the derived per-ODU ceiling (2 x 10 A).
  assert(absolute_maximum_current_a(true, true, false, true, false, 16.0f, 20.0f, 26.0f) == 20.0f);
  // Without confirmed detection the installation standard remains the ceiling.
  assert(absolute_maximum_current_a(true, true, false, false, false, 16.0f, 20.0f, 26.0f) == 16.0f);
  assert(absolute_maximum_current_a(true, true, true, false, false, 16.0f, 20.0f, 26.0f) == 20.0f);
  // Duo V2 only reaches its derived ceiling (2 x 13 A) with both ODUs detected as V2.
  assert(absolute_maximum_current_a(true, true, true, false, true, 16.0f, 20.0f, 26.0f) == 26.0f);
  // Family mismatch stays on the standard.
  assert(absolute_maximum_current_a(true, true, true, true, false, 16.0f, 20.0f, 26.0f) == 20.0f);
  assert(absolute_maximum_current_a(true, true, false, false, true, 16.0f, 20.0f, 26.0f) == 16.0f);
  assert(absolute_maximum_current_a(true, false, false, true, false, 16.0f, 20.0f, 26.0f) == 16.0f);
  assert(effective_current_a(NAN, 6.0f, 16.0f) == 16.0f);
  assert(effective_current_a(4.0f, 6.0f, 16.0f) == 6.0f);
  assert(effective_current_a(18.0f, 6.0f, 16.0f) == 16.0f);
  const auto v2 = thresholds(20.0f, 230.0f);
  assert(v2.soft_w == 4250.0f && v2.peak_w == 4562.5f && v2.recover_w == 4125.0f);
  assert(fallback_cap(true, 16.0f, 16.0f, 16, 20) == 16);
  assert(fallback_cap(false, 16.0f, 16.0f, 16, 20) == 8);
  assert(fallback_cap(true, 6.0f, 16.0f, 16, 20) == 6);
  assert(fallback_cap(true, 16.0f, 0.0f, 16, 20) == 0);

  assert(fresh(1500, 1000, 500));
  assert(!fresh(1501, 1000, 500));
  assert(fresh(250, UINT32_MAX - 249, 500));
  assert(seconds_to_ms(UINT32_MAX) == (UINT32_MAX / 1000UL) * 1000UL);

  State state{20, 0, 0, 0};
  auto out = step(input(2000, 3700.0f), config(), state);
  out = step(input(7000, 3700.0f), config(), out.state);
  out = step(input(12000, 3700.0f), config(), out.state);
  assert(out.measurement_valid && out.state.cap_f == 18);
  assert(out.state.over_peak_s == 0 && out.state.over_soft_s == 0);
  out = step(input(17000, 3650.0f), config(), out.state);
  assert(out.state.over_peak_s == 0);

  auto soft_config = config();
  soft_config.soft_trip_s = 10;
  out = step(input(2000, 3500.0f), soft_config, {20, 0, 0, 0});
  out = step(input(7000, 3500.0f), soft_config, out.state);
  assert(out.state.cap_f == 19 && out.state.over_soft_s == 0);
  out = step(input(12000, 3200.0f), soft_config, out.state);
  out = step(input(17000, 3200.0f), soft_config, out.state);
  assert(out.state.cap_f == 20 && out.state.under_ok_s == 0);

  auto invalid = input(2000, 3000.0f);
  invalid.hp1.online = false;
  out = step(invalid, config(), {20, 10, 10, 10});
  assert(!out.measurement_valid && out.state.cap_f == 16);
  assert(out.state.over_soft_s == 0 && out.state.over_peak_s == 0 && out.state.under_ok_s == 0);
  invalid = input(70000, 3000.0f);
  assert(!step(invalid, config(), state).measurement_valid);
  invalid = input(2000, std::numeric_limits<float>::infinity());
  assert(!step(invalid, config(), state).measurement_valid);
  invalid = input(2000, NAN);
  assert(!step(invalid, config(), state).measurement_valid);
  invalid = input(2000, 3000.0f);
  invalid.hp2.current_valid = false;
  assert(!step(invalid, config(true), state).measurement_valid);
  assert(step(invalid, config(false), state).measurement_valid);
  auto invalid_config = config();
  invalid_config.peak_limit_w = 3000.0f;
  assert(!step(input(2000, 3000.0f), invalid_config, state).measurement_valid);

  assert(saturated_add(UINT32_MAX - 2, 5) == UINT32_MAX);
  return 0;
}
