#pragma once
#include <cstddef>
#include <cstdint>

namespace oq_command_guard {
inline uint16_t word(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

// Inspect FC06 and every register of FC16, including a protected register in
// the middle of a range. Only ordinary control writes use these functions.
template <typename Allowed>
inline bool allows(const uint8_t* pdu, size_t size, Allowed allowed) {
  if (size == 0) return false;
  const auto check = [&](uint16_t address, uint16_t value) {
    return (address != 1999 && address != 3999 && address != 2006 && address != 2010 && address != 2015) ||
           allowed(address, value);
  };
  if (pdu[0] == 6) return size == 5 && check(word(pdu + 1), word(pdu + 3));
  if (pdu[0] != 16) return pdu[0] != 23;  // No read/write-multiple bypass.
  if (size < 6) return false;
  const uint16_t start = word(pdu + 1), count = word(pdu + 3);
  if (count == 0 || count > 123 || pdu[5] != count * 2 || size != 6U + count * 2U ||
      static_cast<uint32_t>(start) + count > 65536U)
    return false;
  for (uint16_t i = 0; i < count; ++i)
    if (!check(start + i, word(pdu + 6 + i * 2))) return false;
  return true;
}
}  // namespace oq_command_guard
