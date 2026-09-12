# OpenQuatt brand candidate — Open Q

This directory contains the isolated Stage 1 candidate identity for OpenQuatt. It is a review package, not a production rebrand. No web-app, documentation-site, favicon or PWA integration is performed here.

## Build the complete kit

Requirements: Node.js 20 or newer.

```bash
cd brand/tools
npm ci
npm run build
```

The build regenerates all vector and raster exports, validates the result and writes `brand/dist/openquatt-brand-kit-v1.zip`. Dependencies are development-only and remain inside `brand/tools/`; font binaries in `node_modules` are never copied into the kit.

Individual commands:

```bash
npm run export
npm run validate
npm run bundle
```

## Review the candidate

Serve the repository root and open `brand/preview/index.html`:

```bash
python3 -m http.server 4173
```

Then visit `http://localhost:4173/brand/preview/`. The board includes small-size, mask, header, monochrome, hardware and current-versus-candidate checks.

## Quick usage

- Use `logos/svg/openquatt-logo-horizontal-*.svg` for normal headers and documents.
- Use `logos/svg/openquatt-logo-compact-*.svg` at 28–36 px high.
- Use `logos/svg/openquatt-symbol-micro.svg` only at 16–20 px.
- Use the platform-specific exports in `icons/`; do not crop the transparent symbol ad hoc.
- Use `hardware/openquatt-symbol-engraving.svg` or a monochrome hardware lockup for single-colour production.

Variant names describe the intended background: `dark` is for dark backgrounds and `light` is for light backgrounds.

Status: **production candidate pending human design and brand-use approval**.
