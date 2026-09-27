// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes compact NL/EN catalogues and passive-learning status/chart/export.
    // PR #728 with current dev: ~1.271 MB raw / 355 kB gzip.
    raw: 1_285_000,
    gzipBaselineCeiling: 362_000,
  },
  // Includes the passive-learning chart and responsive cards: ~204.6 kB raw.
  { file: "css/openquatt-app.css", raw: 207_000 },
];
