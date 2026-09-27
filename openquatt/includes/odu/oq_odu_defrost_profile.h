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
// Separate revocation journal: old/legacy profile flags alone grant no authority.
struct Consent {
  uint32_t magic{0x4f514443U}, profile_checksum{0};
  uint8_t active{0}, reserved[3]{};
  uint32_t checksum{0};
};
inline uint32_t consent_checksum(const Consent& c) {
  uint32_t hash = 2166136261U;
  const auto add = [&hash](uint8_t value) { hash = (hash ^ value) * 16777619U; };
  for (unsigned shift = 0; shift < 32; shift += 8) add(c.magic >> shift);
  for (unsigned shift = 0; shift < 32; shift += 8) add(c.profile_checksum >> shift);
  add(c.active);
  return hash;
}
inline Consent make_consent(const Profile& p, bool active) {
  Consent c;
  c.profile_checksum = active ? p.checksum : 0U;
  c.active = active ? 1U : 0U;
  c.checksum = consent_checksum(c);
  return c;
}
inline bool valid_consent(const Consent& c) {
  return c.magic == 0x4f514443U && c.active <= 1U && c.reserved[0] == 0U && c.reserved[1] == 0U &&
         c.reserved[2] == 0U && (c.active == 1U || c.profile_checksum == 0U) && c.checksum == consent_checksum(c);
}
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
inline bool profile_has_consent(const Profile& p, bool consent_loaded, const Consent& c) {
  return valid_profile(p) && p.flags == 1U && consent_loaded && valid_consent(c) && c.active == 1U &&
         c.profile_checksum == p.checksum;
}
inline const char* profile_boot_state(const Profile& p, bool consent_loaded, const Consent& c) {
  return p.flags == 1U && !profile_has_consent(p, consent_loaded, c) ? "CONSENT_REQUIRED" : "PENDING";
}
inline bool profile_inhibited(const char* state) {
  return std::strcmp(state, "PERSIST_FAILED") == 0 || std::strcmp(state, "REVOKE_FAILED") == 0 ||
         std::strcmp(state, "CONSENT_REQUIRED") == 0;
}
inline const char* profile_state_after_identity(const char* state, const Profile& p, oq_odu::Variant variant,
                                                uint16_t item) {
  // A failed consent change must stay inhibited until an explicit durable save.
  if (profile_inhibited(state)) return state;
  return profile_matches(p, variant, item) ? "PENDING" : "IDENTITY_MISMATCH";
}
inline bool profile_reapply_enabled(const Profile& p, const char* state) {
  return valid_profile(p) && p.flags == 1U && !profile_inhibited(state);
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
enum class ProfileSaveResult : uint8_t { SAVED, REVOKE_FAILED, PROFILE_FAILED, ACTIVATE_FAILED };

// Production transaction, also exercised with a durable/cache-split host store.
// Store save() may only queue data; load() may observe that cache. Require sync
// separately, and never touch the candidate until revocation is confirmed.
template <typename Store>
bool commit_consent(Store& store, const Consent& desired) {
  const bool queued = store.save_consent(desired);
  const bool synced = store.sync();
  Consent actual;
  const bool loaded = store.load_consent(actual);
  return queued && synced && loaded && valid_consent(actual) && std::memcmp(&actual, &desired, sizeof(actual)) == 0;
}
template <typename Store>
ProfileSaveResult save_profile_transaction(Store& store, const Profile& desired) {
  if (!valid_profile(desired)) return ProfileSaveResult::REVOKE_FAILED;
  if (!commit_consent(store, make_consent(desired, false))) return ProfileSaveResult::REVOKE_FAILED;
  const bool queued = store.save_profile(desired);
  const bool synced = store.sync();
  Profile actual;
  const bool loaded = store.load_profile(actual);
  if (!profile_commit_confirmed(queued, synced, loaded, actual, desired)) return ProfileSaveResult::PROFILE_FAILED;
  if (desired.flags == 1U && !commit_consent(store, make_consent(desired, true))) {
    // A failed sync can leave ACTIVE queued, and a failed read can follow a
    // durable ACTIVE write. Replace both with a freshly confirmed revocation.
    if (!commit_consent(store, make_consent(desired, false))) return ProfileSaveResult::REVOKE_FAILED;
    return ProfileSaveResult::ACTIVATE_FAILED;
  }
  return ProfileSaveResult::SAVED;
}
}  // namespace oq_defrost
