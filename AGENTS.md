# OpenQuatt Codex Instructions

## Repo Map

- `configs/`: ESPHome target entrypoints for hardware/topology/connection combinations.
- `openquatt/`: shared ESPHome packages, profiles, topology, connection and webserver YAML.
- `components/`: custom ESPHome Python/C++ components.
- `openquatt/web/`: embedded web UI source and generated bundles.
- `docs/`: user docs, dashboard/install pages and generated site inputs.
- `scripts/`: local validation, release, docs and build tooling.

## Editing Rules

- Do not make functional code changes unless the user requested them for the task.
- Do not perform broad refactors, architecture changes, or cleanup passes unless explicitly asked.
- Do not make formatting-only changes.
- Preserve existing style, naming, YAML structure, and generated-file conventions.
- Keep edits inside the smallest relevant module, package, component, or document.
- Treat `secrets.yaml` and local reference material as sensitive/local; do not quote values.

## Avoid Reading

- `.git/`, `.esphome/`, `.pio/`, `node_modules/`, `.venv*/`, `.cache/`, `.tmp/`, `tmp/`, `output/`, `__pycache__/`.
- Generated web bundles: `openquatt/web/js/openquatt-app.js` and `openquatt/web/css/openquatt-app.css`.
- Large logs, recordings, debug dumps, firmware binaries, release exports, and local references unless directly requested.

## Tests And Builds

- Run the smallest check that matches the change.
- C/C++ code must conform to the repository `.clang-format` configuration. For C/C++ changes, run `npm run check:cpp-format`; if required, run `npm run fix:cpp-format` and verify that no unrelated files changed.
- For firmware/YAML changes, prefer `python3 scripts/dev.py validate --config-only --config <target.yaml>`.
- Run ESPHome compile or full `scripts/dev.py validate` only when explicitly requested or clearly necessary.
- For web source changes, run `npm run build:web`.
- For docs/tooling changes, prefer the specific script touched by the task.
- Summarize build/test output by command, pass/fail, and the few relevant error lines.

## Embedded Memory Safety

- Treat internal DRAM as a constrained runtime resource required by Wi-Fi, lwIP, TLS, DMA and FreeRTOS task stacks; total free memory or free PSRAM alone is not evidence of safety.
- Distinguish current free heap from the cumulative minimum-since-boot watermark. Also inspect the largest internal free block, fragmentation and relevant task-stack high-watermarks.
- Put large, long-lived state, history and request scratch buffers in PSRAM with an explicit failure policy. Do not silently fall back to internal RAM when preserving internal heap is part of the safety budget.
- Remember that globals, `new`, `std::string` and `std::vector` do not automatically use PSRAM; objects stored in PSRAM may still own internal allocations.
- Keep ISR/DMA data, cache-disabled paths and low-level Wi-Fi/TLS/ESP-MQTT stacks in compatible internal memory unless the platform contract and HIL prove external-stack use is safe.
- Prefer a persistent worker task over repeated task create/delete cycles. Avoid globally changing malloc-placement thresholds as a substitute for targeted ownership.
- For memory-affecting changes, compare identical baseline and candidate firmware through cold boot and realistic HA/web/API/MQTT/Modbus/OpenTherm/OTA load. A compile-time RAM report is not sufficient.
- Treat unexplained regressions, allocation failures or a minimum/largest-block result without demonstrated margin for the worst simultaneous allocation as release-blocking. Maintain profile-specific budgets once healthy HIL baselines are established.

## High-Risk Changes

- For heat-pump control safety, embedded memory ownership, concurrency, or lifecycle timing, obtain an independent cold review before publishing.

## GitHub And PRs

- Do not prefix PR titles with `[codex]`; use a concise human-readable title instead.
- Write PR titles in Dutch unless the user explicitly requests another language.
- Use `.github/pull_request_template.md` for PR bodies and fill every relevant section.
- In PRs, issues and review comments, write canonical project commands without personal shell wrappers.
