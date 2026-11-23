#pragma once

#include <cstdint>

// The real supervisor owns this decision. Extracted methods default to no hold.
namespace oq_supervisory_state_runtime {

struct Runtime {
  uint8_t hold_hp = 0;
  uint8_t power_house_low_load_hold_hp() const { return hold_hp; }
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_supervisory_state_runtime
