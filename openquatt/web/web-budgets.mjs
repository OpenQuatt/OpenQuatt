// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // plus temporary Matrix UI-takeover effects (frontend-only, read-only):
    // measured +17.2 kB raw / +6.5 kB gzip over the pre-egg dev baseline.
    // plus #608 Power House run extension settings card (switch + stop margin
    // + derived stop/restart thresholds + firmware status): measured ~961.7 kB
    // raw after compacting the card onto shared settings components.
    raw: 962_000,
    gzipBaselineCeiling: 238_000,
  },
  { file: "css/openquatt-app.css", raw: 203_000 },
];
