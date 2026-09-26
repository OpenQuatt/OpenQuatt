// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes both offline catalogues after build-time key compaction. Keep
    // enough margin for ordinary UI work without accepting the uncompressed form again.
    raw: 1_212_000,
    gzipBaselineCeiling: 340_000,
  },
  // Responsive run-extension group: ~203.3 kB raw.
  { file: "css/openquatt-app.css", raw: 204_000 },
];
