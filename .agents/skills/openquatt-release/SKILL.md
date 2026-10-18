---
name: openquatt-release
description: Prepare or execute OpenQuatt dev-to-main firmware releases, draft Dutch GitHub, Tweakers and Discord notes, or perform a requested post-release dev version bump.
---

# OpenQuatt Release

Use the current OpenQuatt checkout, not a personal absolute path. Read its
`AGENTS.md` and the candidate branch's [release process](../../../docs/development/releaseproces.md).
That document owns the checklist, gates, exception policy, recovery and verifier
commands. Read [release notes](../../../docs/development/release-notes.md)
when preparing public text. Do not maintain a second copy of those procedures.

## Requested scope

- **Prepare or assess:** compare the preceding stable release with current dev,
  inspect merged PRs and validation for the exact candidate SHA, and save draft
  notes plus findings. No branch promotion, tag, publication or version bump.
- **Release:** explicit user authorization permits the documented fast-forward
  route, without a release PR by default. Publication belongs to Release Build;
  apply the final notes afterwards and verify the assets and post-release Pages.
- **Bump dev:** only when requested. Change only `project_version` to the agreed
  next version, preserving main and stable tags. No local firmware compile is
  normally needed for a version-only change.

Use the installed GitHub connector for supported reads/edits, local git/SSH for
commits and pushes, and authenticated `gh` for missing capabilities such as
artifact downloads. The standalone verifier accepts JSON snapshots from either
the connector or `gh`; it does not publish anything.

Do not start physical HIL by default. Distinguish existing functional evidence,
runtime memory qualification, and successful compilation. Missing evidence is
not a passed check. Apply the documented exception policy if the release owner
explicitly accepts a reported risk; never carry that acceptance to another SHA
or waive a failed technical publication gate.

Preserve dirty work, other tasks' branches and immutable tags. Keep the ESPHome
pin unchanged unless requested. Stop on unexpected ref movement or a failed
publication/verification gate; inspect before retrying. No automatic dev bump,
cleanup, monitor or separate task after the requested release.

Keep updates concise and decision-focused. Report the actual release SHA,
outcome, note links and remaining accepted risks, without claiming unrun tests.
