# Web Browser Smoke Matrix

Follow the [development guide](README.md). After changes to shared rendering, CSS,
state, modals, or entity actions, cover all affected consumers in this matrix;
complete the matrix before release. Record the tested commit/build, browser/version,
viewport, themes and any skipped checks. Documentation-only changes do not require
a browser run.

Use `dev.html` with local mock data and test both light and dark themes. Also verify
the production JS/CSS as described below; the default preview alone is insufficient.

| Surface | Desktop light | Desktop dark | Mobile light | Mobile dark | Required interaction |
|---|---|---|---|---|---|
| Overview | Required | Required | Required | Required | Switch overview controls and open silent settings |
| Energy | Required | Required | Required | Required | Change period and history view |
| Settings | Required | Required | Required | Required | Change group, select, number, switch, and time controls |
| Service / experimental controls | Required | Required | Required | Required | Load values, validate input and verify each service's write gates |
| Firmware modals | Required | Required | Required | Required | Open, close, switch advanced mode, and verify errors |
| History import/export | Required | Required | Required | Required | Open storage modal, preserve scroll, and validate file state |

For every cell verify:

- no horizontal page overflow;
- no overlapping controls or clipped text;
- visible focus and working close/backdrop behavior;
- preserved drafts, cursor/selection and native dropdowns during live updates;
- preserved modal scroll and stick-to-bottom behavior where applicable;
- no unexpected console errors, extra polling, or repeated requests;
- explicit busy/error/uncertain states under slow, failed or stale responses; no false success.

Automated prerequisites:

```sh
npm run check:web
npm run smoke:web
node openquatt/web/build-assets.mjs --check
```

`check:web` builds production and preview assets; `smoke:web` rebuilds the ignored
preview assets required by `dev.html` and checks the generated output. Neither
production nor preview bundles are committed.

For the production check, use an isolated local mock test page based on `dev.html`
with the same mock scripts, but load `js/openquatt-app.js` and
`css/openquatt-app.css` instead of the preview pair. Do not mix production JS with
preview CSS: symbol mappings can differ. Keep the test page temporary and reload
after every rebuild. Test actual-device writes or OTA only with explicit permission.

For native input, focus, scrolling or mobile layout changes, include Safari/iOS
checks where applicable. Desktop Safari is not evidence of an iOS test; state any
coverage limitation explicitly.

The matrix is a release checklist, not a replacement for the automated source,
bundle, import-boundary, scroll, state, and action contracts in `smoke-web.mjs`.
