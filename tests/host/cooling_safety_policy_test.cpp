#include <assert.h>
#include <math.h>
#include "../../openquatt/includes/control/oq_cooling_safety_policy.h"
using namespace oq_cooling_safety;
int main() {
  DewPointSelectionState state;
  DewPointSources sources{15, 16, 17, true, true, true};
  auto out = select_dew_point(3, sources, 100, 1000, state);
  assert(out.value == 17 && out.route == 2 && !out.held);
  assert(isnan(select_dew_point(0, sources, 101, 1000, state).value));
  assert(isnan(select_dew_point(3, {}, 150, 1000, state).value));
  assert(select_dew_point(4, sources, 160, 1000, state).route == 4 &&
         select_dew_point(2, sources, 170, 1000, state).route == 2);
  out = select_dew_point(1, sources, 200, 1000, state);
  assert(out.value == 15 && out.route == 1);
  sources.ha = INFINITY;
  out = select_dew_point(1, sources, 1200, 1000, state);
  assert(isnan(out.value) && !out.held);
  state = {};
  sources = {15, NAN, NAN, true, false, false};
  assert(select_dew_point(1, sources, 0, 1000, state).value == 15);
  sources.ha_valid = false;
  out = select_dew_point(1, sources, 999, 1000, state);
  assert(out.value == 15 && out.held && isnan(select_dew_point(1, sources, 1000, 1000, state).value));
  sources.ha_valid = true;
  select_dew_point(1, sources, UINT32_MAX - 99U, 250, state);
  sources.ha_valid = false;
  assert(select_dew_point(1, sources, 100, 250, state).held &&
         isnan(select_dew_point(2, sources, 101, 250, state).value));
  // Required coverage invalidates Auto even when another source or an HA hold is available.
  sources = {16, 15, 14, true, true, true, 12, true};
  assert(select_dew_point(3, sources, 100, 1000, state).route == 1);
  sources.ble = NAN;
  assert(isnan(select_dew_point(3, sources, 101, 1000, state).value));
  sources.ha_valid = sources.api_valid = sources.mqtt_valid = false;
  sources.ble_required = false;
  assert(isnan(select_dew_point(3, sources, 102, 1000, state).value));
  sources = {16, 15, 14, true, true, true, 21, true};
  assert(select_dew_point(3, sources, 103, 1000, state).value == 21);
  assert(select_dew_point(5, sources, 104, 1000, state).route == 5);
  sources.ble = NAN;
  assert(isnan(select_dew_point(5, sources, 105, 1000, state).value));
  assert(select_dew_point(1, sources, 106, 1000, state).value == 16);
  sources.ble = 21;
  sources.ble_required = false;
  assert(select_dew_point(3, sources, 107, 1000, state).value == 16);
  const auto rising = ble_guard(3, 22, true, 13);
  assert(rising.active && rising.floor(2, false, 18, false, NAN) == 24);
  assert(rising.must_stop(24, 21, false) && !rising.must_stop(24, 25, false));
  const auto stale = ble_guard(3, NAN, true, 22);
  assert(isnan(stale.dew) && isnan(stale.floor(2, false, 18, false, 20)) && stale.must_stop(NAN, 22, false));
  assert(stale.floor(2, false, 18, true, 20) == 20 && stale.floor(2, true, 18, false, 20) == 18);
  assert(!stale.must_stop(18, 18, true) && stale.must_stop(NAN, 18, true));
  assert(!ble_guard(3, NAN, false, 20).active && !ble_guard(1, NAN, true, 20).active);
  assert(isnan(ble_guard(5, NAN, false, 20).dew));
  bool latched = false;
  RoomRequestInput in{true, false, false, true, true, 20, 20, 0.4f, 0.1f};
  auto demand = [&](float room_c) {
    in.room_c = room_c;
    return update_room_request(in, latched);
  };
  assert(!demand(20) && demand(20.41f) && latched && demand(20.1f) && !demand(20.09f) && !latched && !demand(INFINITY));
  in.room_required = false, in.enabled_valid = in.enabled = true;
  assert(update_room_request(in, latched) && !latched);
  assert(core_permitted(false, false, true, true, false, false));
  assert(!core_permitted(true, false, true, true, true, true) &&
         core_permitted(true, true, false, false, false, false));
  assert(flow_permitted(true, true, 500, 400, false));
  assert(!flow_permitted(true, true, INFINITY, 400, false) && !flow_permitted(true, true, 500, 400, true));
  assert(isnan(fallback_minimum_supply(true, 19, false, NAN, false, NAN)) &&
         isnan(fallback_minimum_supply(true, INFINITY, false, NAN, false, NAN)));
  assert(fallback_minimum_supply(true, 24, true, 20, false, NAN) == 21.5f &&
         fallback_minimum_supply(true, 24, true, 20, true, 21) == 20.0f && isnan(clamp_finite(INFINITY, 0, 4)));
}
