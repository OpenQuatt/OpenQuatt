// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes both offline catalogues after build-time key compaction. Keep
    // enough margin for ordinary UI work without accepting the uncompressed form again.
    // Defrost form, restart profile and variant-aware method help (NL/EN).
    // Keep a bounded raw margin; the existing gzip ceiling remains sufficient.
    // Supply-target source explanation in both offline catalogues (+1.3 kB raw).
    raw: 1_239_000,
    gzipBaselineCeiling: 343_000,
  },
  // Responsive run-extension group: ~203.3 kB raw.
  { file: "css/openquatt-app.css", raw: 204_000 },
];
