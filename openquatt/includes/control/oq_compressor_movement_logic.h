#pragma once

#include <array>
#include <cstdlib>

namespace oq_compressor_movement {

// A running level with an unknown table entry must not look like standstill.
inline int frequency(int level, int table_hz) {
  if (level == 0) return 0;
  return level > 0 && table_hz > 0 && table_hz <= 120 ? table_hz : -1;
}

// Starts and stops are separate events; only continuing compressors modulate.
// -1 means the comparison lacks a known physical frequency reference.
inline int modulation_hz(const std::array<int, 2>& previous, const std::array<int, 2>& next) {
  int result = 0;
  for (unsigned hp = 0; hp < 2; ++hp) {
    if (previous[hp] < 0 || previous[hp] > 120 || next[hp] < 0 || next[hp] > 120) return -1;
    if (previous[hp] > 0 && next[hp] > 0) result += std::abs(next[hp] - previous[hp]);
  }
  return result;
}

}  // namespace oq_compressor_movement
