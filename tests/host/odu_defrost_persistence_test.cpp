#include <cassert>
#include <cstring>
#include <initializer_list>
#include "includes/odu/oq_odu_defrost_profile.h"

using namespace oq_defrost;
enum class Fault { NONE, SAVE, SYNC, LOAD, READBACK };
// ESP preferences have separate queued/cache and durable flash state. A reboot
// drops pending writes; a failed sync can still have committed its data.
struct Store {
  Profile disk_profile = make_profile(4, oq_odu::Variant::V1_5, 123, true);
  Consent disk_consent = make_consent(disk_profile, true);
  Profile cached_profile = disk_profile;
  Consent cached_consent = disk_consent;
  bool profile_pending{false}, consent_pending{false};
  unsigned phase{1}, failure_phase{0};
  Fault fault{Fault::NONE};
  unsigned cleanup_failure_phase{0};
  Fault cleanup_fault{Fault::NONE};
  bool commit_despite_sync_failure{false};
  bool fails(Fault value) const {
    return (phase == failure_phase && fault == value) || (phase == cleanup_failure_phase && cleanup_fault == value);
  }
  bool save_profile(const Profile& p) {
    if (fails(Fault::SAVE)) return false;
    cached_profile = p;
    profile_pending = true;
    return true;
  }
  bool save_consent(const Consent& c) {
    if (fails(Fault::SAVE)) return false;
    cached_consent = c;
    consent_pending = true;
    return true;
  }
  bool sync() {
    const bool failed = fails(Fault::SYNC);
    if (!failed || commit_despite_sync_failure) {
      if (profile_pending) disk_profile = cached_profile;
      if (consent_pending) disk_consent = cached_consent;
      profile_pending = consent_pending = false;
    }
    return !failed;
  }
  bool load_profile(Profile& p) {
    p = cached_profile;
    if (fails(Fault::READBACK)) ++p.checksum;
    const bool loaded = !fails(Fault::LOAD);
    ++phase;
    return loaded;
  }
  bool load_consent(Consent& c) {
    c = cached_consent;
    if (fails(Fault::READBACK)) ++c.checksum;
    const bool loaded = !fails(Fault::LOAD);
    ++phase;
    return loaded;
  }
  bool boot_authorized() const { return profile_has_consent(disk_profile, true, disk_consent); }
};

int main() {
  const auto disable = make_profile(1, oq_odu::Variant::V1_5, 123, false);
  const auto enable = make_profile(1, oq_odu::Variant::V1_5, 123, true);
  for (const auto fault : {Fault::SAVE, Fault::SYNC, Fault::LOAD, Fault::READBACK}) {
    // Fail every transaction phase using the actual production transaction.
    for (unsigned phase = 1; phase <= 3; ++phase) {
      Store s;
      s.failure_phase = phase;
      s.fault = fault;
      const auto desired = phase == 3 ? enable : disable;
      const auto result = save_profile_transaction(s, desired);
      assert(result != ProfileSaveResult::SAVED);
      if (phase == 1) {
        assert(result == ProfileSaveResult::REVOKE_FAILED);
        // Total first-write/sync failure cannot erase old flash consent. The
        // caller must expose this unconfirmed revocation, not promise durability.
        assert(s.boot_authorized() == (fault == Fault::SAVE || fault == Fault::SYNC));
      } else if (phase == 2) {
        assert(result == ProfileSaveResult::PROFILE_FAILED);
        assert(!s.boot_authorized());  // Old enabled profile cannot resurrect.
      } else {
        assert(result == ProfileSaveResult::ACTIVATE_FAILED);
        assert(!s.boot_authorized());  // Cleanup revokes queued AND durable ACTIVE.
        s.fault = Fault::NONE;
        assert(s.sync() && !s.boot_authorized());  // An unrelated later sync cannot re-arm.
      }
    }
  }
  // Partial sync / lost acknowledgement at each phase, followed by reboot.
  for (unsigned phase = 1; phase <= 3; ++phase) {
    Store s;
    s.failure_phase = phase;
    s.fault = Fault::SYNC;
    s.commit_despite_sync_failure = true;
    const auto desired = phase == 3 ? enable : disable;
    assert(save_profile_transaction(s, desired) != ProfileSaveResult::SAVED);
    assert(!s.boot_authorized());
  }
  // If cleanup itself fails, report unconfirmed revocation instead of claiming
  // a reboot-safe inhibit. Cover queued ACTIVE and already-durable ACTIVE.
  for (const auto activation_fault : {Fault::SYNC, Fault::LOAD}) {
    for (const auto cleanup_fault : {Fault::SAVE, Fault::SYNC, Fault::LOAD, Fault::READBACK}) {
      Store cleanup;
      cleanup.failure_phase = 3;
      cleanup.fault = activation_fault;
      cleanup.cleanup_failure_phase = 4;
      cleanup.cleanup_fault = cleanup_fault;
      assert(save_profile_transaction(cleanup, enable) == ProfileSaveResult::REVOKE_FAILED);
      if (cleanup_fault == Fault::SAVE || (activation_fault == Fault::LOAD && cleanup_fault == Fault::SYNC))
        assert(cleanup.boot_authorized());  // Honest boundary: revocation could not be confirmed.
      else
        assert(!cleanup.boot_authorized());
      cleanup.fault = cleanup.cleanup_fault = Fault::NONE;
      assert(cleanup.sync());
      assert(cleanup.boot_authorized() == (cleanup_fault == Fault::SAVE));
    }
  }
  Store s;
  s.failure_phase = 2;
  s.fault = Fault::SYNC;
  assert(save_profile_transaction(s, disable) == ProfileSaveResult::PROFILE_FAILED);
  assert(!s.boot_authorized());
  // A later unrelated flush cannot revive the previous ACTIVE journal.
  s.fault = Fault::NONE;
  assert(s.sync() && !s.boot_authorized());
  assert(save_profile_transaction(s, enable) == ProfileSaveResult::SAVED);
  assert(s.boot_authorized());  // Explicit re-enable restores authority.
  assert(save_profile_transaction(s, disable) == ProfileSaveResult::SAVED);
  assert(!s.boot_authorized());
  assert(save_profile_transaction(s, disable) ==
         ProfileSaveResult::SAVED);  // Same-value consent repair is not skipped.
  assert(!s.boot_authorized());

  // Legacy profile upgrades and missing/corrupt/wrong-schema/stale journal fail closed.
  const auto old = make_profile(4, oq_odu::Variant::V1_5, 123, true);
  Consent missing;
  assert(!profile_has_consent(old, false, missing));
  assert(std::strcmp(profile_boot_state(old, false, missing), "CONSENT_REQUIRED") == 0);
  auto journal = make_consent(old, true);
  assert(profile_has_consent(old, true, journal));
  ++journal.magic;
  assert(!profile_has_consent(old, true, journal));
  journal = make_consent(old, true);
  ++journal.checksum;
  assert(!profile_has_consent(old, true, journal));
  journal = make_consent(enable, true);
  assert(!profile_has_consent(old, true, journal));
  journal = make_consent(old, false);
  assert(!profile_has_consent(old, true, journal));
  assert(!profile_reapply_enabled(old, profile_boot_state(old, true, journal)));
  assert(std::strcmp(profile_state_after_identity("CONSENT_REQUIRED", old, oq_odu::Variant::V1_5, 123),
                     "CONSENT_REQUIRED") == 0);
  assert(std::strcmp(profile_state_after_identity("REVOKE_FAILED", old, oq_odu::Variant::V1_5, 123), "REVOKE_FAILED") ==
         0);
  assert(!profile_reconcile_ready(old, "REVOKE_FAILED", oq_odu::Variant::V1_5, 123, true, true, true, false, false));
}
