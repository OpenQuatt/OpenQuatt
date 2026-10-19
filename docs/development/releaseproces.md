# Release Process

This project uses GitHub Actions for automated validation, firmware compilation, and release asset publishing.
Style and documentation consistency are validated locally via `python3 scripts/dev.py validate`. The current style scope is documented in `CONTRIBUTING.md`.

This document is the shared source of truth, for human maintainers and agents.
Use [release notes](release-notes.md) for GitHub/Tweakers/Discord formatting and
[release verification](release-verification.md) for the read-only final check.
The repository skill lives in
[`.agents/skills/openquatt-release`](../../.agents/skills/openquatt-release/SKILL.md)
and links here instead of duplicating the procedure. It travels with a clone;
no personal filesystem paths or global skill installation are required. Retire
older personal copies or reduce them to a pointer to these repository sources.
The layout follows [OpenAI's repo-local skill guidance](https://developers.openai.com/blog/skills-agents-sdk).

## Branch Model

- `main`: stable branch for tagged releases and the default OTA/update channel. Small docs/ops fixes may be committed directly to `main` when appropriate.
- `dev`: integration branch for testers. Validate it like `main`, but do not cut stable tags from it.
- feature branches: short-lived branches merged into `dev`; promote `dev` into `main` only after validation/soak.
- if `main` and `dev` diverge after a release (for example because of a direct `main` fix or a non-fast-forward promotion), realign `dev` with the released `main` commit before starting the next development cycle. Then apply the next dev-version bump as a new commit on top.

Firmware should expose its channel explicitly via `release_channel` so Home Assistant and the ESPHome web UI show whether a device is running `main` or `dev`.

## Workflows

- `/.github/workflows/ci-build.yml`
  - Trigger: push to `main`, pull requests
  - Actions:
    - validate and compile every enabled target from `build_targets.yaml`
    - upload compiled firmware artifacts per enabled target
- `/.github/workflows/release-build.yml`
  - Trigger: tag push `v*`, or manual dispatch from an existing semantic-version tag
  - Actions:
    - validate + compile every enabled target from `build_targets.yaml` without compiler cache
    - publish target-specific OTA/factory release assets
    - generate target-specific `*-ota.manifest.json` files for OTA update checks
    - create the GitHub Release as a draft and publish it only after every expected asset is present
    - attach release firmware binaries and OTA manifests to the release
- `/.github/workflows/dev-build.yml`
  - Trigger: push to `dev` except docs-only changes, manual dispatch
  - Actions:
    - compile every enabled target with `release_channel=dev`
    - override `project_version` to `${base_version}-dev.<run_number>+<shortsha>`
    - move the mutable `dev-latest` tag to the newest `dev` commit
    - publish/update a prerelease that contains binaries + OTA manifests for the dev channel
- `/.github/workflows/pages-deploy.yml`
  - Trigger: matching docs/install pushes to `main`, completion of `Release Build` (jobs run only after success), manual dispatch
  - Actions:
    - check out the `main` branch docs as the Pages site source
    - download the latest stable `*.firmware.factory.bin` assets from GitHub Releases
    - mirror those first-install binaries into the Pages artifact under `/firmware/main/`
    - deploy the resulting site via GitHub Pages Actions

## ESPHome Version Pinning

- CI/release build with a pinned ESPHome version from `/.github/requirements-esphome.txt`.
- Keep release builds deterministic by updating this pin via PR (instead of using floating `latest`).

## Deferred ESPHome Security Features

ESPHome 2026.7 adds optional NVS HMAC encryption and OTA downgrade protection. OpenQuatt does not enable either feature as part of the compatibility migration:

- NVS HMAC encryption requires provisioning an HMAC key in eFuse. Burning that key is irreversible, support differs across the ESP32 target matrix, and factory/recovery procedures have not yet been designed.
- OTA downgrade protection is only effective for signed firmware. OpenQuatt release artifacts are not yet signed and there is no validated downgrade and recovery procedure for every hardware target.

Treat both as separate hardware-security projects with their own provisioning, signing, recovery and hardware-validation plan.

## Release Versioning

Use semantic versioning tags:

- `vMAJOR.MINOR.PATCH`
- Examples: `v0.12.3`, `v0.13.0`

Recommended increments:

- Patch: bug fixes, docs, CI changes
- Minor: new backward-compatible functionality
- Major: breaking changes

Keep the source-controlled `project_version` aligned with the next intended stable release. Published dev-channel artifacts should override that at build time to `${project_version}-dev.<run_number>+<shortsha>` while also setting `release_channel=dev`. The monotonically increasing prerelease number is important: SemVer ignores everything after `+`, so SHA-only build metadata does not count as a newer OTA version on its own.

## Channel Metadata

- `project_version` in `openquatt/oq_substitutions_common.yaml` remains the user-facing firmware version.
- `release_channel` in `openquatt/oq_substitutions_common.yaml` identifies the running channel (`main` or `dev`).
- `release_manifest_url` selects which OTA manifest the built-in update entity uses.
- `Firmware Update` checks the selected OTA manifest every `${oq_firmware_periodic_check_interval}` (default `4h`) through the explicit OpenQuatt scheduler; the ESPHome update component's own boot-time scheduler is disabled.
- The first automatic manifest check waits for `${oq_firmware_initial_check_delay_s}` (default 300s) of stable network connectivity without an active OTA so it does not overlap the boot-time API, web and usage-telemetry publication waves.
- `Firmware Update Channel` is a runtime select that switches the OTA manifest between the baked-in `main` and `dev` URLs. Restored boot state uses the delayed initial check; a real runtime change refreshes the update entity immediately.
- `Check Firmware Updates` always requests an immediate refresh and cancels a still-pending delayed initial check.

The running firmware exposes both `OpenQuatt Version` and `OpenQuatt Release Channel` in diagnostics, while `Firmware Update Channel` controls which OTA track the device should follow next.

## Building a Dev Channel Firmware

Reuse a matrix entrypoint and override channel-specific substitutions at build time:

```bash
BASE_VERSION="$(awk -F'\"' '/^project_version: / { print $2 }' openquatt/oq_substitutions_common.yaml)"
DEV_STAMP="$(date -u +%Y%m%d%H%M%S)"
DEV_VERSION="${BASE_VERSION}-dev.${DEV_STAMP}+local"

esphome \
  -s project_version "${DEV_VERSION}" \
  -s release_channel dev \
  -s release_manifest_url https://github.com/OpenQuatt/OpenQuatt/releases/download/dev-latest/openquatt-heatpump-controller-q-duo-ota.manifest.json \
  compile configs/heatpump_controller_q/duo.yaml
```

Use `python3 scripts/build_targets.py list-configs --status enabled` to inspect the enabled target list. This keeps the topology/hardware/connection matrix independent from release channel selection.

The repository now backs that URL with `/.github/workflows/dev-build.yml`, which publishes a mutable prerelease/tag named `dev-latest`.

## How To Cut a Release

### Prepare and freeze

1. Read this process and the candidate's workflows, matrix and ESPHome pin.
   Confirm the intended version and preceding stable comparison tag. Inspect
   merged PRs, relevant discussions and changes since preparation; draft all
   three [notes formats](release-notes.md). Preparation alone never authorizes a
   promotion, tag or release.
2. Fetch only the branch refs, preserving mutable and immutable local tags:

```bash
git fetch origin main dev --no-tags
git status --short --branch
git worktree list
```

3. Preserve dirty work and other checkouts. Use a clean task-specific worktree
   when needed, following `AGENTS.md` and local worktree-location preferences.
   Update `project_version` on dev if needed; do not change the ESPHome pin as
   incidental release work. Freeze the full candidate SHA and version in the
   release report. If main is not an ancestor, merge main into dev normally,
   resolve conflicts and validate the new combined SHA. Never reset/force-push
   dev to discard its commits.
4. Check existing local/remote stable tags and the GitHub Release before changing
   branches. A mismatched tag is a stop condition, not permission to replace it.
   If the tag already matches, inspect and resume its existing run instead of
   starting a duplicate release. Recheck any changed source refs before proceeding.
5. Wait for **Dev Build on the exact candidate SHA**. CI runs on PRs and main,
   not direct dev pushes. A docs-only push may need manual Dev Build dispatch;
   an older green build is not evidence for a new SHA. Check NVS budgets for all
   enabled profiles with `scripts/check_nvs_budget.py`, using the current
   threshold. Build web assets before config checks. Inspect independent review
   and applicable control, upgrade/recovery and runtime-memory evidence. Desktop
   tests and successful compilation do not prove ESP32 heap/stack margin. Do not
   start new physical HIL as an automatic release step; report missing evidence.

### Promote and publish

6. Obtain explicit release authorization and resolve the gates below. Record
   `TAG=vMAJOR.MINOR.PATCH` and the frozen `RELEASE_SHA`. Refresh refs and confirm
   dev still points there. Promote only that SHA with fast-forward semantics,
   without a release PR by default:

```bash
git fetch origin main dev --no-tags
test "$(git rev-parse origin/dev)" = "$RELEASE_SHA"
git merge-base --is-ancestor origin/main "$RELEASE_SHA"
git push origin "${RELEASE_SHA}:refs/heads/main"
```

   Run commands individually and stop on any failure. Never use force to bypass
   a concurrent main update. A release PR is optional, not the normal promotion.
7. Wait for green **main CI on that exact SHA**. Recheck remote main, the source
   `project_version`, and tag/release absence. Create the immutable tag only from
   that validated main SHA:

```bash
git ls-remote origin refs/heads/main "refs/tags/${TAG}" "refs/tags/${TAG}^{}"
git tag -a "$TAG" "$RELEASE_SHA" -m "OpenQuatt $TAG"
git push origin "refs/tags/${TAG}"
```

8. **Do not create the GitHub Release manually.** Release Build owns the
   cache-free compilation, draft, upload, expected-asset check and publication.
   Wait for its success before applying the final GitHub notes. Do not publish
   its draft early or replace generated text while publication is running.
9. Run the [generic verifier](release-verification.md) against the exact source,
   downloads and fresh metadata. It checks matrix-derived assets/aliases and
   legacy EOL manifests, sizes/SHA256, OTA versions/URLs/MD5, exact Q ELF/map
   symbols and retention, and the **post-release** Pages version and byte-identical
   factory binaries. An earlier main-push Pages success is not the final check.
10. Confirm the published notes match the final text and provide the release,
    Tweakers and Discord links. Record the SHA, run IDs, verification result and
    any accepted risks. Do not call an unfinished/failed release complete.

### Gates and explicit exceptions

The release owner decides whether a documented residual risk is acceptable;
an agent must not silently interpret a merge or a green compile as clearance.

- Missing runtime qualification or an unresolved review finding blocks by
  default. Explicit owner acceptance may allow release, but must identify the
  finding, consequences, missing evidence, full candidate SHA and version. A
  general "continue" is not an exception. If the owner's response is broad,
  confirm exactly which already reported risks it covers before proceeding.
- Keep a durable decision in a release-preparation issue/PR discussion or an
  attached report, including who accepted it and when. `.tmp/releases/` holds
  working copies, not the only audit record. Acceptance is not a fix or a test
  pass and does not apply automatically to later SHAs/releases.
- Do **not** waive technical identity/publication gates: no non-fast-forward
  promotion, moved stable tag, tag/version/SHA mismatch, failed/missing exact-SHA
  Dev Build or main CI, failed cache-free Release Build, or failed asset/symbol/
  installer verification. These require repair/revalidation, not risk acceptance.
- This is an operational checklist, not a claim that Actions enforces every
  pre-tag gate. Release Build currently guards the semantic tag/publication
  state and builds/assets; the operator must check the other preconditions.

### Failure, resumption and next dev version

Stop at a failed gate and report the affected SHA/run. Inspect logs before a
targeted retry. Preserve recoverable drafts and artifacts; do not delete a public
release, force refs, fabricate assets or weaken checks to make a retry pass.
Never move a stable tag. A code fix needs a new candidate and validation; if the
stable tag already exists, use a new version rather than replacing it. A network
or Pages propagation error may be retried without changing firmware, but a
dispatch acknowledgement is not successful publication or verification.

After fast-forward promotion, main and dev already share the release commit.
Only when requested, change `project_version` on dev to the agreed next version,
review the one-line diff and push normally. No local firmware compile is normally
needed for a version-only bump; the usual dev workflow may still run. If main has
new unique commits, merge main back into dev and validate the combined result;
never erase new dev work. Cleanup of branches/worktrees is a separate request.

## Notes

- Enabled target configs under `configs/` are secrets-free and suitable for CI builds.
- First-install UX now lives on the GitHub Pages installer at `https://openquatt.github.io/OpenQuatt/install/`, which builds ESP Web Tools manifests dynamically in the browser against same-origin stable factory binaries mirrored onto Pages.
- Target-specific `*-ota.manifest.json` files are intended for OTA update flows.
- Stable releases also publish four legacy EOL manifests. They point only to the immutable v0.50.0 Waveshare/Heatpump Listener OTA binaries, never to a Q-edition binary; `dev-latest` intentionally has no legacy bridge.
- Each firmware reads `${release_manifest_url}` from its selected config entrypoint.
- OTA manifests and OTA binaries remain on GitHub Releases; only first-install factory binaries are mirrored onto Pages for Web Serial/CORS compatibility.
- Workflow files must remain directly under `.github/workflows/` (GitHub does not load workflows from nested subfolders).
