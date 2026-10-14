#pragma once

#include <cstddef>

namespace esphome {
namespace openquatt_modbus {
namespace detail {

// Re-select after retiring a command: its on_not_sent callback may change the
// queue or the policy. Bound work even if such a callback keeps appending.
// select_ready must return only READY entries, never an in-flight command.
template <typename SelectReady, typename Allowed, typename Retire>
bool guard_next_command(size_t budget, SelectReady select_ready, Allowed allowed, Retire retire) {
  while (auto* command = select_ready()) {
    if (allowed(*command)) {
      return true;
    }
    if (budget == 0U) {
      return false;
    }
    --budget;
    retire(*command);
  }
  return false;
}

}  // namespace detail
}  // namespace openquatt_modbus
}  // namespace esphome
