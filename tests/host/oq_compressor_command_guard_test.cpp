#include <cassert>
#include <array>
#include "includes/control/oq_compressor_command_guard.h"

int main() {
  bool permitted = true;
  int desired = 17;
  const auto gate = [&](uint16_t address, uint16_t value) {
    if (value == 0) return true;
    return permitted && (address == 3999 ? value == 2 : value == desired);
  };
  const std::array<uint8_t, 5> level{6, 7, 207, 0, 17};  // R1999
  const std::array<uint8_t, 5> mode{6, 15, 159, 0, 2};   // R3999
  const std::array<uint8_t, 5> stop{6, 7, 207, 0, 0};
  assert(oq_command_guard::allows(level.data(), level.size(), gate));
  assert(oq_command_guard::allows(mode.data(), mode.size(), gate));
  desired = 13;
  assert(!oq_command_guard::allows(level.data(), level.size(), gate));
  permitted = false;
  assert(!oq_command_guard::allows(mode.data(), mode.size(), gate));
  assert(oq_command_guard::allows(stop.data(), stop.size(), gate));
  // An FC16 range crossing R1999 must not bypass the final gate.
  std::array<uint8_t, 12> range{16, 7, 206, 0, 3, 6, 0, 0, 0, 17, 0, 0};
  assert(!oq_command_guard::allows(range.data(), range.size(), gate));
  range[9] = 0;
  assert(oq_command_guard::allows(range.data(), range.size(), gate));
  range[5] = 4;
  assert(!oq_command_guard::allows(range.data(), range.size(), gate));
  const std::array<uint8_t, 5> unrelated{6, 7, 214, 0, 1};
  assert(oq_command_guard::allows(unrelated.data(), unrelated.size(), gate));
  const std::array<uint8_t, 5> read{3, 7, 207, 0, 1};
  assert(oq_command_guard::allows(read.data(), read.size(), gate));
  assert(!oq_command_guard::allows(read.data(), 0, gate));
  const std::array<uint8_t, 1> unsupported{23};
  assert(!oq_command_guard::allows(unsupported.data(), unsupported.size(), gate));
}
