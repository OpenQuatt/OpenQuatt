// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes passive learning, trend goals, the curve editor and air-purge status.
    // With daily-learning UI and legacy compatibility: ~1.298 MB raw / 361 kB gzip on Node 24.19.0.
    // Controlled warmup (#784) and deferred UI updates: approximately +8.3 kB raw / +2.3 kB gzip against dev.
    // Controller-backed warmup durations and end results add ~1.6 kB raw / 0.5 kB gzip.
    // After rebasing onto dev 6058e271: measured 1.318 MB raw; gzip growth stays bounded.
    raw: 1_325_000,
    gzipBaselineCeiling: 362_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  { file: "css/openquatt-app.css", raw: 209_000 },
];
