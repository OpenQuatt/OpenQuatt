#pragma once

#include <cstring>
#include "includes/control/oq_defrost_logic.h"

namespace oq_defrost {
// Separate NVS schema: never share the bottom-plate consent or settings.
struct Profile {
  uint32_t magic{0x4f514446U};
  uint8_t version{1}, flags{0}, variant{0}, mode{0};
  uint16_t control_board_item{0}, reserved{0};
  uint32_t checksum{0};
};
inline uint32_t profile_checksum(const Profile& p) {
  uint32_t hash = 2166136261U;
  const auto add = [&hash](uint8_t value) { hash = (hash ^ value) * 16777619U; };
  for (unsigned shift = 0; shift < 32; shift += 8) add(p.magic >> shift);
  add(p.version);
  add(p.flags);
  add(p.variant);
  add(p.mode);
  add(p.control_board_item);
  add(p.control_board_item >> 8);
  return hash;
}
inline Profile make_profile(int mode, oq_odu::Variant variant, uint16_t item, bool automatic) {
  Profile p;
  p.mode = mode;
  p.variant = static_cast<uint8_t>(variant);
  p.control_board_item = item;
  p.flags = automatic ? 1U : 0U;
  p.checksum = profile_checksum(p);
  return p;
}
inline bool valid_profile(const Profile& p) {
  return p.magic == 0x4f514446U && p.version == 1U && p.flags <= 1U && p.reserved == 0U && p.control_board_item != 0U &&
         is_supported_defrost_mode(p.mode, static_cast<oq_odu::Variant>(p.variant)) &&
         p.checksum == profile_checksum(p);
}
inline bool profile_matches(const Profile& p, oq_odu::Variant variant, uint16_t item) {
  return valid_profile(p) && p.variant == static_cast<uint8_t>(variant) && p.control_board_item == item;
}
inline const char* profile_state_after_identity(const char* state, const Profile& p, oq_odu::Variant variant,
                                                uint16_t item) {
  // A failed consent change must stay inhibited until an explicit durable save.
  if (std::strcmp(state, "PERSIST_FAILED") == 0) return state;
  return profile_matches(p, variant, item) ? "PENDING" : "IDENTITY_MISMATCH";
}
inline bool profile_reapply_enabled(const Profile& p, const char* state) {
  return valid_profile(p) && p.flags == 1U && std::strcmp(state, "PERSIST_FAILED") != 0;
}
inline bool profile_reconcile_ready(const Profile& p, const char* state, oq_odu::Variant variant, uint16_t item,
                                    bool online, bool fresh, bool identity, bool busy, bool request_pending) {
  return profile_reapply_enabled(p, state) && profile_matches(p, variant, item) && online && fresh && identity &&
         !busy && !request_pending;
}
inline bool profile_commit_confirmed(bool queued, bool synced, bool loaded, const Profile& actual,
                                     const Profile& desired) {
  return queued && synced && loaded && valid_profile(actual) && std::memcmp(&actual, &desired, sizeof(actual)) == 0;
}
}  // namespace oq_defrost
