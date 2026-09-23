// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes both offline catalogues after build-time key compaction. Keep
    // enough margin for ordinary UI work without accepting the uncompressed form again.
    // 2026-09: 1_218_000. Defrost diagnostics + manual trigger add ~17 kB vs dev
    // (1,198,452 -> 1,215,321 measured); the defrost mode editor adds ~1,8 kB more
    // (measured 1,217,080). 11 unused keys removed and label tables
    // compacted to offset. Gzip 338,463 stays under the 340,000 ceiling.
    raw: 1_218_000,
    gzipBaselineCeiling: 340_000,
  },
  // Responsive run-extension group: ~203.3 kB raw.
  { file: "css/openquatt-app.css", raw: 204_000 },
];
