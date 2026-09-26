// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes both offline catalogues after build-time key compaction. Keep
    // enough margin for ordinary UI work without accepting the uncompressed form again.
    // PR #742 plus dev integration: 1,230,389 B raw / 341,953 B gzip (Node 24).
    // The merged dev UI adds 1,439 raw bytes over the prior PR build.
    // Keep a small raw margin; the existing gzip ceiling remains sufficient.
    raw: 1_232_000,
    gzipBaselineCeiling: 343_000,
  },
  // Responsive run-extension group: ~203.3 kB raw.
  { file: "css/openquatt-app.css", raw: 204_000 },
];
