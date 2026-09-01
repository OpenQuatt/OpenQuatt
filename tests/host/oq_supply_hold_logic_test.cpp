#include <assert.h>
#include <limits.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_supply_hold_logic.h"

int main() {
  using namespace oq_supply_calibration;
  using namespace oq_supply_hold;

  const auto pt1000 = source_identity("Local", true, "PT1000", true, "", false, "sensor.supply");
  const auto ds18b20 = source_identity("Local", true, "DS18B20", true, "", false, "sensor.supply");
  const auto cic_a = source_identity("CIC", false, "", false, "http://cic-a/feed", true, "sensor.supply");
  const auto cic_b = source_identity("CIC", false, "", false, "http://cic-b/feed", true, "sensor.supply");
  const auto ha = source_identity("HA input", false, "", false, "", false, "sensor.supply");

  constexpr uint32_t short_hold_ms = 15000;
  constexpr uint32_t ha_hold_ms = 300000;
  assert(timeout_ms(pt1000, short_hold_ms, ha_hold_ms) == short_hold_ms);
  assert(timeout_ms(ds18b20, short_hold_ms, ha_hold_ms) == short_hold_ms);
  assert(timeout_ms(cic_a, short_hold_ms, ha_hold_ms) == short_hold_ms);
  assert(timeout_ms(ha, short_hold_ms, ha_hold_ms) == ha_hold_ms);

  const int32_t pt1000_code = static_cast<int32_t>(pt1000.code);
  assert(can_hold(32.5f, 1000, pt1000_code, pt1000.fingerprint, pt1000, 15999, short_hold_ms));
  assert(!can_hold(32.5f, 1000, pt1000_code, pt1000.fingerprint, pt1000, 16000, short_hold_ms));
  assert(!can_hold(32.5f, 1000, pt1000_code, pt1000.fingerprint, ds18b20, 5000, short_hold_ms));
  assert(!can_hold(NAN, 1000, pt1000_code, pt1000.fingerprint, pt1000, 5000, short_hold_ms));
  assert(!can_hold(32.5f, 0, pt1000_code, pt1000.fingerprint, pt1000, 5000, short_hold_ms));

  const int32_t cic_code = static_cast<int32_t>(cic_a.code);
  assert(can_hold(28.0f, 1000, cic_code, cic_a.fingerprint, cic_a, 5000, short_hold_ms));
  assert(!can_hold(28.0f, 1000, cic_code, cic_a.fingerprint, cic_b, 5000, short_hold_ms));

  const int32_t ha_code = static_cast<int32_t>(ha.code);
  assert(can_hold(31.0f, 1000, ha_code, ha.fingerprint, ha, 300999, ha_hold_ms));
  assert(!can_hold(31.0f, 1000, ha_code, ha.fingerprint, ha, 301000, ha_hold_ms));

  constexpr uint32_t before_wrap_ms = UINT32_MAX - 4U;
  assert(can_hold(30.0f, before_wrap_ms, pt1000_code, pt1000.fingerprint, pt1000, 5U, short_hold_ms));

  State state;
  assert(!state.has_value());
  state.remember(32.5f, 1000, pt1000);
  assert(state.has_value());
  assert(state.available(pt1000, 5000, short_hold_ms));
  assert(!state.available(ds18b20, 5000, short_hold_ms));

  // A source transition clears the old sample before the new source can recover.
  if (!state.matches_source(ds18b20)) state.reset();
  assert(!state.has_value());
  state.remember(31.75f, 6000, ds18b20);
  assert(state.available(ds18b20, 10000, short_hold_ms));
  assert(state.last_valid_c == 31.75f);
  assert(!state.available(ds18b20, 21000, short_hold_ms));
  if (!state.available(ds18b20, 21000, short_hold_ms)) state.reset();
  assert(!state.has_value());

  state.remember(29.0f, 1000, ha);
  assert(state.available(ha, 300999, ha_hold_ms));
  assert(!state.available(ha, 301000, ha_hold_ms));
  state.remember(NAN, 302000, ha);
  assert(!state.has_value());

  // Timestamp zero is a valid observation at millis() rollover.
  state.remember(30.0f, 0, pt1000);
  assert(state.has_value());
  assert(state.available(pt1000, 10, short_hold_ms));
  return 0;
}
