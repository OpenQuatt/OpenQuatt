#include <cassert>
#include <limits>
#include <map>
#include <string>

namespace {
struct Entity {
  float state = std::numeric_limits<float>::quiet_NaN();
  bool has_state() const { return state == state; }
  void publish_state(float value) { state = value; }
};

Entity& entity(const char* name) {
  static std::map<std::string, Entity> entities;
  return entities[name];
}
}  // namespace

#define id(name) entity(#name)
#include "openquatt/includes/control/oq_api_ingress_runtime.h"
#undef id

int main() {
  using oq_api_ingress::Slot;
  oq_api_ingress::Runtime api;
  api.reset();
  api.set_accept_updates(true);
  entity("api_input_room_temperature").state = 19.9f;
  entity("api_input_room_setpoint").state = 20.0f;
  api.observe(Slot::ROOM_TEMPERATURE, 1000);
  api.observe(Slot::ROOM_SETPOINT, 1000);
  assert(entity("api_input_room_temperature_valid").state);
  assert(entity("api_input_room_setpoint_valid").state);
  assert(api.fresh(Slot::ROOM_TEMPERATURE, 11000, 10));
  assert(!api.fresh(Slot::ROOM_TEMPERATURE, 11001, 10));
  assert(!api.fresh(Slot::ROOM_SETPOINT, 11001, 10));
  assert(api.fresh(Slot::ROOM_SETPOINT, 11001, 0));
  assert(entity("api_input_room_temperature_valid").state);
  api.observe(Slot::ROOM_TEMPERATURE, 11002);
  assert(api.fresh(Slot::ROOM_TEMPERATURE, 11002, 10));
  api.reset();
  assert(!api.fresh(Slot::ROOM_TEMPERATURE, 11003, 0));
  assert(!api.fresh(Slot::ROOM_SETPOINT, 11003, 0));
}
