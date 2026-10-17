#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace oq_eeprom {

// Control and Modbus execute on the main loop. HTTP never reads these guards.
struct Safety {
  bool online{false}, fresh{false}, identity{false}, stopped{false}, clear{false};
  int mode{-1};
  uint32_t observed_ms{0U};
};
inline std::array<Safety, 2> safety{};
inline uint8_t configured_mask{0U};
inline std::atomic<uint8_t> hold_mask{0U};
inline uint32_t stop_epoch{0U};
struct WritePermit {
  const uint16_t* words{nullptr};
  uint16_t address{0U}, count{0U}, value{0U};
  uint8_t slave{0U}, hp{0U}, function{0U};
  bool writing_mode{false};
};
inline WritePermit permit{};

inline bool holding() { return hold_mask.load(std::memory_order_acquire) != 0U; }
inline void hold(uint8_t hp, bool enabled) {
  const uint8_t bit = static_cast<uint8_t>(1U << (hp - 1U));
  if (enabled)
    hold_mask.fetch_or(bit, std::memory_order_release);
  else
    hold_mask.fetch_and(static_cast<uint8_t>(~bit), std::memory_order_release);
}
inline bool safe(uint32_t now, uint8_t owner = 0U, bool writing_mode = false) {
  if (configured_mask == 0U) return false;
  for (size_t index = 0U; index < safety.size(); ++index) {
    if ((configured_mask & (1U << index)) == 0U) continue;
    const auto& g = safety[index];
    if (!g.online || !g.fresh || !g.identity || !g.stopped || !g.clear || now - g.observed_ms > 5000U) return false;
    if (g.mode != 0 && !(writing_mode && owner == index + 1U && g.mode == 5)) return false;
  }
  return true;
}

inline bool permitted_write(uint8_t slave, const uint8_t* pdu, size_t size, uint32_t now) {
  if (pdu == nullptr || size < 5U || permit.function == 0U || slave != permit.slave || pdu[0] != permit.function ||
      !safe(now, permit.hp, permit.writing_mode))
    return false;
  const auto word = [pdu](size_t offset) {
    return static_cast<uint16_t>((uint16_t(pdu[offset]) << 8U) | pdu[offset + 1U]);
  };
  if (word(1U) != permit.address) return false;
  if (pdu[0] == 6U) return size == 5U && word(3U) == permit.value;
  if (pdu[0] != 16U || size != 6U + 2U * permit.count || word(3U) != permit.count || pdu[5] != 2U * permit.count ||
      permit.words == nullptr)
    return false;
  for (size_t index = 0; index < permit.count; ++index)
    if (word(6U + index * 2U) != permit.words[index]) return false;
  return true;
}

}  // namespace oq_eeprom
