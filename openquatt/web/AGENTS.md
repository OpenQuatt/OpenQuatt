# Web UI

- Read and follow [README.md](README.md) before web-app development; it is the canonical development guide.
- Edit source files in `js/src/` and `css/src/`.
- Follow the [Dutch/English guidelines](README.md#nederlands-en-engels): localize visible copy in both catalogues, preserve wire values, and verify locale switching and compact-bundle compatibility.
- Do not edit or commit generated production/preview bundles.
- Reuse existing field models, controls, tokens and modals; keep service-specific write/safety gates separate.
- Preserve overview colors and live input/focus/scroll continuity.
- Keep `dev.html`, `mock-device.js`, and `device-wrapper.js` scoped to local/demo behavior.
- After web source changes, follow the guide's validation workflow and [browser matrix](BROWSER_SMOKE_MATRIX.md), including production-bundle checks. Verify affected surfaces rather than reading all web source files.
- For documentation-only changes, check links/commands and docs contracts; no web build or browser run is required.
