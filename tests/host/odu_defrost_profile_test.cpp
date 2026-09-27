#include <cassert>
#include <cstring>
#include "includes/odu/oq_odu_defrost_profile.h"

int main() {
  using namespace oq_defrost;
  const auto variant = oq_odu::Variant::V1_5;
  auto p = make_profile(4, variant, 123, true);
  assert(valid_profile(p));
  // A restart loads only a checksummed, versioned, explicit-consent profile.
  Profile restart = p;
  assert(profile_commit_confirmed(true, true, true, restart, p));
  assert(!profile_commit_confirmed(false, true, true, restart, p));
  assert(!profile_commit_confirmed(true, false, true, restart, p));  // Cached readback is not durable proof.
  assert(!profile_commit_confirmed(true, true, false, restart, p));
  assert(profile_matches(restart, variant, 123) && restart.flags == 1);
  assert(!profile_matches(restart, variant, 124));
  assert(!profile_matches(restart, oq_odu::Variant::V1, 123));
  assert(!valid_profile(make_profile(4, oq_odu::Variant::V1, 123, true)));
  assert(!valid_profile(make_profile(2, variant, 123, true)));
  assert(!valid_profile(make_profile(0, variant, 0, true)));
  auto disabled = make_profile(4, variant, 123, false);
  assert(!profile_commit_confirmed(true, true, true, disabled, p));
  assert(valid_profile(disabled) && disabled.flags == 0);
  // Failed disable: cached readback must not restore the previous auto consent,
  // even after offline, replacement identity, then successful re-identification.
  const bool disable_committed = profile_commit_confirmed(true, false, true, disabled, disabled);
  assert(!disable_committed);
  const char* state = disable_committed ? "PENDING" : "PERSIST_FAILED";
  state = profile_state_after_identity(state, restart, oq_odu::Variant::UNKNOWN, 0);
  assert(std::strcmp(state, "PERSIST_FAILED") == 0 && !profile_reapply_enabled(restart, state));
  state = profile_state_after_identity(state, restart, variant, 124);
  assert(std::strcmp(state, "PERSIST_FAILED") == 0 && !profile_reapply_enabled(restart, state));
  state = profile_state_after_identity(state, restart, variant, 123);
  assert(std::strcmp(state, "PERSIST_FAILED") == 0 && !profile_reapply_enabled(restart, state));
  assert(profile_commit_confirmed(true, true, true, disabled, disabled));
  state = "PENDING";  // Only an explicit successful save clears the inhibit.
  assert(!profile_reapply_enabled(disabled, state));
  assert(profile_reapply_enabled(restart, state));
  // Boot: a valid enabled profile waits for fresh identity. Disabled consent
  // never reconciles. A queued manual request always wins the idle interleave.
  assert(!profile_reconcile_ready(restart, "PENDING", oq_odu::Variant::UNKNOWN, 0, false, false, false, false, false));
  assert(profile_reconcile_ready(restart, "PENDING", variant, 123, true, true, true, false, false));
  assert(!profile_reconcile_ready(disabled, "PENDING", variant, 123, true, true, true, false, false));
  assert(!profile_reconcile_ready(restart, "PENDING", variant, 123, true, true, true, false, true));
  assert(!profile_reconcile_ready(restart, "PENDING", variant, 123, true, true, true, true, false));
  assert(!profile_reconcile_ready(restart, "PERSIST_FAILED", variant, 123, true, true, true, false, false));
  p.mode = 0;  // Corrupted/stale storage never gains automatic authority.
  assert(!valid_profile(p));
  p = restart;
  p.version = 2;
  assert(!valid_profile(p));
  p = restart;
  p.flags = 2;
  p.checksum = profile_checksum(p);
  assert(!valid_profile(p));

  Guard g{true, true, true, true, false, false, false, false, 0, 0, false};
  assert(std::strcmp(mode_save_error(g, 4, 0, 0, true, true, variant), "READY") == 0);
  g.incident = true;
  assert(std::strcmp(mode_save_error(g, 4, 4, 4, true, true, variant), "INCIDENT_BLOCK") == 0);
  g.incident = false;
  g.hz = 20;
  assert(std::strcmp(mode_save_error(g, 4, 0, 0, true, true, variant), "COMPRESSOR_RUNNING") == 0);
  g.hz = 0;
  g.fresh = false;
  assert(std::strcmp(mode_save_error(g, 4, 0, 0, true, true, variant), "OFFLINE") == 0);
}
