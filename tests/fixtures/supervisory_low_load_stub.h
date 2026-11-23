#pragma once

// These harnesses exercise extracted request and actuator methods outside a
// Power House low-load hold. The supervisor normally owns that decision.
namespace oq_supervisory_state_runtime {

struct Runtime {
  bool power_house_low_load_hold() const { return false; }
};

inline const Runtime& runtime() {
  static const Runtime inactive;
  return inactive;
}

}  // namespace oq_supervisory_state_runtime
