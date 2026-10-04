// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes passive learning, trend goals, the curve editor and air-purge status.
    // With revised English terminology: ~1.291 MB raw / 359 kB gzip on Node 24.19.0.
    // Controlled warmup (#784): +5.7 kB raw / +1.4 kB gzip against dev on Node 24.19.0.
    raw: 1_300_000,
    gzipBaselineCeiling: 362_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  { file: "css/openquatt-app.css", raw: 209_000 },
];
