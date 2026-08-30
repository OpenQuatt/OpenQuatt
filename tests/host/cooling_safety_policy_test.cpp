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
  bool latched = false;
  RoomRequestInput in;
  in.room_valid = in.setpoint_valid = true;
  in.room_c = in.setpoint_c = 20;
  in.on_delta_c = 0.4f, in.off_delta_c = 0.1f;
  assert(!update_room_request(in, latched));
  in.room_c = 20.41f;
  assert(update_room_request(in, latched) && latched);
  in.room_c = 20.1f;
  assert(update_room_request(in, latched));
  in.room_c = 20.09f;
  assert(!update_room_request(in, latched) && !latched);
  in.room_c = INFINITY;
  assert(!update_room_request(in, latched));
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
  return 0;
}
