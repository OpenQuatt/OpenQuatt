# Release Process

This project uses GitHub Actions for automated validation, firmware compilation, and release asset publishing.
Style and documentation consistency are validated locally via `./scripts/validate_local.sh`. The current style scope is documented in `CONTRIBUTING.md`.

## Branch Model

- `main`: stable branch for tagged releases and the default OTA/update channel. Small docs/ops fixes may be committed directly to `main` when appropriate.
- `dev`: integration branch for testers. Validate it like `main`, but do not cut stable tags from it.
- feature branches: short-lived branches merged into `dev`; promote `dev` into `main` only after validation/soak.
- if `main` and `dev` diverge after a release (for example because of a direct `main` fix or a non-fast-forward promotion), realign `dev` with the released `main` commit before starting the next development cycle. Then apply the next dev-version bump as a new commit on top.

Firmware should expose its channel explicitly via `release_channel` so Home Assistant and the ESPHome web UI show whether a device is running `main` or `dev`.

## Workflows

- `/.github/workflows/ci-build.yml`
  - Trigger: push to `main` or `dev`, pull requests
  - Actions:
    - validate and compile every enabled target from `build_targets.yaml`
    - upload compiled firmware artifacts per enabled target
- `/.github/workflows/release-build.yml`
  - Trigger: tag push `v*` and manual dispatch
  - Actions:
    - validate + compile every enabled target from `build_targets.yaml`
    - publish target-specific OTA/factory release assets
    - generate target-specific `*-ota.manifest.json` files for OTA update checks
    - create/update GitHub Release
    - attach release firmware binaries and OTA manifests to the release
- `/.github/workflows/dev-build.yml`
  - Trigger: push to `dev`, manual dispatch
  - Actions:
    - compile every enabled target with `release_channel=dev`
    - override `project_version` to `${base_version}-dev.<run_number>+<shortsha>`
    - move the mutable `dev-latest` tag to the newest `dev` commit
    - publish/update a prerelease that contains binaries + OTA manifests for the dev channel
- `/.github/workflows/pages-deploy.yml`
  - Trigger: push to `main` when docs/install assets change, published stable release, successful `Release Build`, manual dispatch
  - Actions:
    - check out the `main` branch docs as the Pages site source
    - download the latest stable `*.firmware.factory.bin` assets from GitHub Releases
    - mirror those first-install binaries into the Pages artifact under `/firmware/main/`
    - deploy the resulting site via GitHub Pages Actions

## ESPHome Version Pinning

- CI/release build with a pinned ESPHome version from `/.github/requirements-esphome.txt`.
- Keep release builds deterministic by updating this pin via PR (instead of using floating `latest`).

## Firmware build identity and crash symbolization

CI derives the build identity from the checkout itself: `git rev-parse HEAD` supplies the full
commit and the commit timestamp supplies `SOURCE_DATE_EPOCH`. The repository, commit, target and
epoch are compiled into the firmware as `build_source_repository`, `build_source_commit`,
`build_target` and `build_epoch`. Effective `project_version`, `release_channel` and
`release_manifest_url` overrides are part of the same build input.

Every firmware build produces one small provenance record named
`<artifact>.<source-commit>.<build-id>.build.json`; `firmware.elf` is never published. The record
contains the full application ELF SHA-256 (`build_id`), effective firmware version/channel, all
substitutions and the hashes or captured contents needed to verify the deterministic wrapper,
Python/npm locks, generated web assets, ESP-IDF component manifest/dependency lock and sdkconfig.
Runner-specific source paths and the derived IDF manifest hash are represented portably; the
reconstructor expands them for its checkout and compares the generated dependency contract after
normalizing those two values again. The OTA application descriptor contains the same full ELF
SHA-256.

Stable, dev and PR publication persist these JSON records on the
`firmware-build-metadata` branch under
`records/<source-owner>/<source-repository>/<source-commit>/<build-id>/`. A path that already exists
is never overwritten: an identical rebuild contract is an idempotent retry (diagnostic run fields
may differ), while different reconstruction inputs fail the publisher. Concurrent publishers
re-read and retry GitHub API conflicts. Mutable `dev-latest` and
`pr-*` releases therefore do not own the rebuild record, PR close does not delete it, and metadata
does not consume the finite release-asset count.

The canonical download URL has this form:

```text
https://raw.githubusercontent.com/OpenQuatt/OpenQuatt/firmware-build-metadata/records/<source-owner>/<source-repository>/<source-commit>/<build-id>/<artifact>.<source-commit>.<build-id>.build.json
```

Use the manually dispatched `Reconstruct and Symbolize Crash` workflow with the canonical raw URL
of that record and every captured build field: build ID, source repository/commit, build epoch,
target, firmware version and release channel. The workflow validates their syntax before any
checkout, cross-checks all values against the downloaded JSON, verifies the exact checkout before
package setup/install and verifies captured source inputs before using them. A PR build must use
its captured fork `source_repository`; executing a fork checkout requires the workflow's explicit
untrusted-repository acknowledgement. The workflow rebuilds in an ephemeral runner and does not
upload the ELF.

Local support tooling can use a downloaded build record directly:

```bash
python3 scripts/reconstruct_firmware_elf.py \
  --source-root /path/to/exact-checkout \
  --metadata openquatt-example.<commit>.<build-id>.build.json \
  --output /tmp/firmware.elf \
  --result-json /tmp/rebuild-result.json

python3 scripts/symbolize_firmware_crash.py \
  --rebuild-result /tmp/rebuild-result.json \
  --expected-build-id <captured-64-hex-build-id> \
  0x42001234 0x40370000
```

The reconstruction uses only the substitutions from the record. It verifies the captured source
wrapper and requirements/npm lock before installation, seeds the captured ESP-IDF manifest and
dependency lock, and verifies the generated locks, manifest, sdkconfig and web assets after the
build. It is accepted only when the rebuilt ELF file hash, the SHA embedded in its OTA image and
the captured `build_id` are byte-for-byte equal. A mismatch stops before `addr2line` is started.
This final hash check is the proof of an exact reconstruction; source and epoch alone are not that
proof.

The record is durable metadata, not a fully hermetic archived toolchain. GitHub and the relevant
package registries must still provide the captured commit and packages. In particular, metadata
for a PR fork remains after PR close, but a deleted fork or unreachable rewritten commit can no
longer be rebuilt. Administrative deletion or force-rewrite of the metadata branch is also outside
the workflow's guarantees; a tampered record still cannot pass the final captured build-ID check.

The analytics broker, transport and crash-topic base are a durable privacy protocol, not ordinary
mutable configuration. Firmware stores an endpoint generation before the first retained crash
session. Changing the broker, port, TLS mode or topic base requires a generation migration that can
still publish a retained tombstone to every older endpoint before clearing local cleanup evidence.

The analytics broker, transport and crash-topic base are a durable privacy protocol, not ordinary
mutable configuration. Firmware stores an endpoint generation before the first retained crash
session. Changing the broker, port, TLS mode or topic base requires a generation migration that can
still publish a retained tombstone to every older endpoint before clearing local cleanup evidence.

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
  -s release_manifest_url https://github.com/OpenQuatt/OpenQuatt/releases/download/dev-latest/openquatt-waveshare-duo-wifi-ota.manifest.json \
  compile configs/waveshare/duo_wifi.yaml
```

Use `python3 scripts/build_targets.py list-configs --status enabled` to inspect the enabled target list. This keeps the topology/hardware/connection matrix independent from release channel selection.

The repository now backs that URL with `/.github/workflows/dev-build.yml`, which publishes a mutable prerelease/tag named `dev-latest`.

## How To Cut a Release

1. Update `project_version` in `openquatt/oq_substitutions_common.yaml` on `dev`.
2. Push `dev` and wait for `CI` and `Dev Build` to go green.
3. Promote the validated `dev` commit to `main`. Recommended path: fast-forward `main` to `origin/dev`:

```bash
git fetch origin
git checkout main
git reset --hard origin/main
git merge --ff-only origin/dev
git push origin main
```

You may still use a release PR when you want a reviewed release summary on GitHub, but the ruleset no longer requires that path.

4. Wait for `CI` on `main` to go green.
5. Create and push a tag from the merged `main` commit:

```bash
git fetch origin
git tag v0.13.0 origin/main
git push origin v0.13.0
```

6. Check GitHub Actions:
   - CI should be green.
   - Release workflow should publish artifacts.
7. Verify GitHub Release contains:
   - one `*-ota.manifest.json` per enabled target
   - one `*.firmware.ota.bin` per enabled target
   - one `*.firmware.factory.bin` per enabled target
8. If `main` and `dev` no longer point to the same release content, realign `dev` with the released `main` commit before bumping to the next development version:

```bash
git fetch origin
git checkout dev
git reset --hard origin/main
git push --force-with-lease origin dev

# then bump to the next dev version, commit, and push
```

If you used the recommended fast-forward promotion and did not add extra `main`-only commits afterwards, `dev` and `main` already align and you can skip this reset.

## Notes

- Enabled target configs under `configs/` are secrets-free and suitable for CI builds.
- First-install UX now lives on the GitHub Pages installer at `https://openquatt.github.io/OpenQuatt/install/`, which builds ESP Web Tools manifests dynamically in the browser against same-origin stable factory binaries mirrored onto Pages.
- Target-specific `*-ota.manifest.json` files are intended for OTA update flows.
- Each firmware reads `${release_manifest_url}` from its selected config entrypoint.
- OTA manifests and OTA binaries remain on GitHub Releases; only first-install factory binaries are mirrored onto Pages for Web Serial/CORS compatibility.
- Workflow files must remain directly under `.github/workflows/` (GitHub does not load workflows from nested subfolders).
