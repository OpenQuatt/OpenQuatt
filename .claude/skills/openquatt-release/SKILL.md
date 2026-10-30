---
name: openquatt-release
description: Prepare or execute an OpenQuatt firmware release (dev fast-forward to main, tag, Release Build), draft Dutch GitHub/Tweakers/Discord release notes, verify a published release, or bump dev to the next version. Use when the user asks to "release maken", "nieuwe versie uitbrengen", "release voorbereiden", "release notes", "taggen", "dev bumpen" or similar.
argument-hint: "[voorbereiden|release|bump] [vX.Y.Z]"
---

# OpenQuatt Release (Claude Code)

This is a pointer for Claude Code, which does not load `.agents/skills/`. The
repository skill and docs are the authority; do not copy their procedure here.

Read first, from the current checkout:

1. [`.agents/skills/openquatt-release/SKILL.md`](../../../.agents/skills/openquatt-release/SKILL.md): scopes and guardrails.
2. [`docs/development/releaseproces.md`](../../../docs/development/releaseproces.md): checklist, gates, exception policy, recovery.
3. [`docs/development/release-notes.md`](../../../docs/development/release-notes.md): GitHub, Tweakers and Discord formats.
4. [`docs/development/release-verification.md`](../../../docs/development/release-verification.md): `scripts/verify_release.py` usage.

Route in one line: dev → fast-forward push to main → green main CI on that SHA →
annotated tag push → Release Build publishes → final notes → verify incl.
post-release Pages. No release PR and never a manual GitHub Release.

## Arguments

`$ARGUMENTS` selects the scope. No argument or `voorbereiden` means prepare only.
`release vX.Y.Z` is the user's intent to release, but still confirm the frozen
full SHA, version and every reported gate or risk with the user before the push
to main and again before the tag push. Both are outward-facing and the tag is
immutable. `bump vX.Y.Z` changes only `project_version` in
`openquatt/oq_substitutions_common.yaml` on dev.

## Claude Code specifics

- There is no GitHub connector here; use `gh` (`gh run list/view/watch`,
  `gh release view/edit`, `gh api`) and local git for refs and pushes.
- Wait on long Actions runs with a background `gh run watch <id> --exit-status`
  instead of polling loops.
- Keep working drafts in `.tmp/releases/` (gitignored), e.g.
  `vX.Y.Z-notes.md`, `vX.Y.Z-tweakers.txt`, `vX.Y.Z-discord.txt`.
- Report in Dutch to the user; keep release notes in the documented Dutch format.
