# Contributing

This guide is intended for repo maintenance and reviews. Its goal is not to enforce generic YAML style, but to keep ESPHome packages predictable and quick to review.

## Purpose

- Keep high-churn control packages logically organized.
- Make runtime state and ownership easy to find.
- Enforce only rules that create little noise and genuinely help reviews.

## Scope

The automatic style check is intentionally narrow, but now fully applies to all ESPHome YAML entrypoints and package-related files:

- For all main configs, base configs, profile files, and package YAML files:
  - no tabs
  - no trailing whitespace
  - consistent banner header at the top
- For all package YAML files in `openquatt/*.yaml`:
  - fixed top-level group order
  - structured lambda layout for larger blocks
- For all main configs and base configs:
  - fixed top-level order of `substitutions`, `packages`, `esphome`, `esp32` where applicable
  - fixed child order inside small wiring mappings such as `packages:`
- For package include manifests and substitution files:
  - fixed include order in `oq_packages*.yaml`
  - fixed section order in `oq_substitutions_common.yaml`
  - fixed key order in hardware-profile `substitutions:`

## Package Layout

Use this group order when a package contains these blocks:

1. banner + responsibility block
2. infrastructure such as `logger:`, `api:`, `ota:`, `wifi:`, `http_request:`, `uart:`, `modbus:`, `modbus_controller:`, `modbus_server:`, `one_wire:`, `time:`, `web_server:`
3. internal state via `globals:`
4. control/helper blocks such as `script:`, `output:`, `climate:`, `switch:`, `select:`, `number:`, `datetime:`, `text:`, `button:`
5. published entities via `binary_sensor:`, `sensor:`, `text_sensor:`, `update:`
6. periodic loop via `interval:`

Rationale:

- infrastructure at the top makes external dependencies and board/runtime setup immediately visible
- `globals:` early in the file makes latches, timers, and ownership easy to find
- control/helper blocks come before published entities, so the package first shows what it drives and manages
- `datetime`, `climate`, and `output` explicitly belong in the control/helper layer, not scattered through the file
- `interval:` at the bottom keeps the main loop close to the executing logic

Within a group, exact order matters less than readability and cohesion.

## Review Rules

Not everything is checked automatically. These rules remain leading during reviews:

- Keep lambdas phased: read inputs, compute state, apply outputs, publish diagnostics.
- Use short section headers above top-level blocks.
- Keep single-writer ownership explicit; avoid hidden writers to the same state.
- Add comments only where intent is not immediately clear.
- Write new and changed code comments in English, including YAML comments and `//` comments inside ESPHome lambdas.

## Embedded Memory and Heap

Internal DRAM is a safety and availability budget, not interchangeable with free PSRAM. Wi-Fi, lwIP, TLS, ESP-MQTT, DMA and several FreeRTOS paths still require internal memory and often a sufficiently large contiguous block.

For new runtime features and material changes:

- Prefer strict PSRAM allocation for large, long-lived state, history, payload and response scratch buffers when the access path is PSRAM-safe.
- Do not silently fall back to internal RAM when that would consume reserved network or control headroom. Allocation failure must degrade explicitly and safety-relevant control must fail closed.
- Remember that globals, `new`, `std::string` and `std::vector` do not automatically allocate in PSRAM. A containing object in PSRAM can still own internal allocations.
- Keep ISR/DMA buffers, cache-disabled code paths and platform-restricted task stacks in compatible internal memory.
- Reuse bounded buffers and persistent worker tasks instead of per-request large copies or repeated task create/delete cycles, but do not keep MQTT/TLS/socket resources alive merely to preserve a worker.
- Use targeted placement before changing global options such as `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`.
- Review the maximum simultaneous peak across components, not only each component in isolation.

Validate memory-sensitive work with identical baseline and candidate firmware. Record at least:

- current internal free heap;
- cumulative minimum internal heap since boot;
- largest internal free block and fragmentation;
- free PSRAM;
- relevant task-stack high-watermarks;
- whether values recover after the tested operation.

Measure during cold boot and realistic combined load, including the applicable HA, web, API, MQTT, Modbus, OpenTherm and OTA/flash paths. Avoid high-frequency diagnostic polling that materially changes the result. The linker RAM summary is useful for static deltas, but does not expose transient task stacks, TLS buffers or allocation peaks.

An unexplained regression, allocation failure, or minimum/largest-block result without measured margin for the worst simultaneous allocation is release-blocking until investigated. Establish healthy HIL baselines per hardware profile and turn those into explicit profile budgets rather than relying on one global magic number. See `docs/system-overview.md` for the project memory model.

## Lambda Style

For ESPHome lambdas, we use a narrow but strict style:

- always use `lambda: |-`, not inline one-liners
- keep the lambda body clearly indented below the `lambda:` line
- use at most one blank line between logical blocks
- add at least one short `//` phase or intent comment to lambdas with 12 or more non-empty lines

For larger lambdas, prefer this structure:

1. read / validate inputs
2. internal calculation or state updates
3. publish output or return value

If a lambda has multiple responsibilities, give those blocks explicit names with short comments.

## Local Validation

Use the project helper for normal local checks:

- `python3 scripts/dev.py bootstrap`
- `python3 scripts/dev.py validate`
- `python3 scripts/dev.py validate --config-only`
- `python3 scripts/dev.py validate --jobs 2`

For C/C++ source files, install the exact version from `.clang-format-version` and use the repository formatting commands. The cross-platform `uv` tool installer provides the pinned binary:

- `uv tool install --force clang-format==22.1.8`
- `clang-format --version`
- `npm run check:cpp-format`
- `npm run fix:cpp-format`

The formatting commands reject any version other than the one pinned in `.clang-format-version` and include both tracked and new, non-ignored C/C++ files. If `clang-format` is installed but not in `PATH`, set `CLANG_FORMAT_BIN` to the executable path before running the npm command. CI enforces the same version and formatting, so run the local fix command before opening or updating a pull request.

For quick iterations, the standalone checks remain useful:

- `python3 scripts/check_style_consistency.py`
- `python3 scripts/check_docs_consistency.py`

The checker is intended as a local quality gate. Add new rules only once the current codebase can satisfy them consistently.

## Generated Web Bundles

Read the [web-app development guide](openquatt/web/README.md) for component reuse,
styling, lifecycle rules, bundle constraints and the validation/browser checklist.

The files `openquatt/web/js/openquatt-app.js` and `openquatt/web/css/openquatt-app.css` are generated artifacts and are no longer committed. CI and local validate rebuild them before firmware compilation.

When you change web sources, validate the generated output locally with:

- `rtk npm run build:web`
- `rtk npm run check:web`

CI runs the same checks and rebuilds bundles during the pipeline.
