# Release verification

`scripts/verify_release.py` replaces per-version local verification scripts. It
is read-only: no builds, branch/tag changes, release uploads, publication or
cleanup. It checks downloaded release assets/symbols against fresh GitHub JSON
snapshots and streams the public Pages binaries for byte-level comparison.
It is **not** a replacement for pre-release review, CI or hardware qualification.

Use Python 3.12+ with the project's pinned dependencies (`PyYAML` is included).
The `--source-tree` must be a clean checkout at the full release SHA, not today's
dev or a working copy with edited configuration. The verifier loads that tree's
target helper, matrix, ESPHome pin and symbol retention. Use only a trusted
OpenQuatt checkout; no code from downloaded firmware/metadata is executed.

## Collect evidence after publication

Read the successful Release Build and its subsequent `workflow_run` Pages Deploy.
Record their IDs and expected full commit SHA. A green Pages run from the earlier
main push does not qualify. Prefer the installed GitHub connector for supported
reads; the following standalone CLI examples use authenticated `gh` with read
access. Save the API's actual JSON, not a summary or manually fabricated metadata.

Set these values for the release being checked; do not copy an old run ID:

```bash
TAG=vMAJOR.MINOR.PATCH
RELEASE_SHA=<full-release-commit-sha>
RELEASE_RUN=<successful-release-build-id>
PAGES_RUN=<successful-post-release-pages-id>
SOURCE_TREE=<clean-checkout-at-release-sha>
REPO=OpenQuatt/OpenQuatt
OUT=".tmp/releases/${TAG}-verify-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT/metadata"
```

Replace placeholders before running; execute commands individually and stop on
failure. Use a fresh export directory to avoid mixing retries or releases. The
connector may supply the same four JSON files without using the CLI:

```bash
gh api "repos/${REPO}/releases/tags/${TAG}" > "$OUT/metadata/release.json"
gh api "repos/${REPO}/actions/runs/${RELEASE_RUN}" > "$OUT/metadata/release-run.json"
gh api "repos/${REPO}/actions/runs/${RELEASE_RUN}/artifacts?per_page=100" --paginate --slurp > "$OUT/metadata/artifact-pages.json"
gh api "repos/${REPO}/actions/runs/${PAGES_RUN}" > "$OUT/metadata/pages-run.json"

gh release download "$TAG" --repo "$REPO" --dir "$OUT/assets"
SYMBOL_ARTIFACT=$(jq -r --arg name "openquatt-q-debug-symbols-${TAG}" '.[].artifacts[] | select(.name == $name) | .id' "$OUT/metadata/artifact-pages.json")
gh api "repos/${REPO}/actions/artifacts/${SYMBOL_ARTIFACT}/zip" > "$OUT/symbols.zip"
```

`artifact-pages.json` is an array of API page objects, each with `artifacts`.
When using the connector, collect all pages and wrap even a single page in an
array. The other three files are the raw API objects. Downloads require the
CLI or another authorized artifact downloader; JSON alone is not the firmware.

## Run the check

Run the verifier from the tooling checkout, pointing it at the release checkout.
This also allows checking older releases that predate the verifier itself:

```bash
python3 scripts/verify_release.py "$TAG" \
  --source-sha "$RELEASE_SHA" --source-tree "$SOURCE_TREE" \
  --metadata-dir "$OUT/metadata" \
  --assets-dir "$OUT/assets" --symbols-archive "$OUT/symbols.zip" \
  --repo "$REPO"
```

Exit 0 means all checks passed; exit 1 means verification failed or evidence is
missing. `--help` describes the inputs. `--pages-base-url` can select another
HTTPS-hosted installer mirror; it must include the `firmware/main` path. Default
is the repository's public GitHub Pages location. There is no success mode that
silently omits the installer or missing metadata.

The verifier checks:

- Published stable tag metadata; exact matrix-derived asset set including
  connection aliases and legacy EOL manifests; no duplicate/missing/extra assets.
- Downloaded file size and GitHub SHA256 digest; correct OTA profile, chip,
  version, URLs and MD5; aliases byte-identical to the unified OTA binary.
- Legacy manifests remain pinned to their documented last-supported firmware,
  not the new Q binary. Versions/counts come from the release's target helper.
- Successful Release Build for the expected tag and full SHA; exact Q symbol
  artifact run/commit, expiry and configured retention. The original ZIP size
  and SHA256 must match GitHub's artifact metadata; its index, ELF build hashes,
  maps, target entrypoints and pinned ESPHome version are checked without
  extracting or executing archive contents. A rebuilt ZIP is not equivalent.
- Successful later Pages `workflow_run` on that SHA; live `version.json` and
  byte-identical factory binaries for every enabled target. Streaming is bounded
  by the expected binary sizes; a partial or oversized response fails.

Snapshots must be freshly obtained from GitHub. The verifier checks their
internal consistency, not their authenticity or freshness; it is not a signature
verifier. Confirm remote refs and exact-SHA Dev Build/main CI separately using
the [release checklist](releaseproces.md). For a historical release, the live
installer may already serve a newer version; that is not a successful current
installer verification for the older tag.

## Record and recover

Attach the command/result, full SHA and run IDs to the release report, alongside
any explicitly accepted residual risks. Do not commit firmware, debug symbols,
credentials or local logs. Keep `.tmp` working exports local; retain decisions
and the outcome in a durable issue/PR discussion or attached handover report.

On failure, inspect the reported invariant. Wrong SHA/version/digest requires
investigation, not editing JSON to match. For a transient network/CDN error,
retry with fresh metadata after the successful Pages deployment has propagated.
An expired/missing exact symbol artifact is missing evidence, not permission to
substitute a locally rebuilt ELF. No retry moves a stable tag or weakens a check.
