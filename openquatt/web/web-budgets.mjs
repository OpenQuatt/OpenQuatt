// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes passive learning, trend goals, the curve editor and air-purge status.
    // With daily-learning UI and legacy compatibility, plus the performance invitation.
    // Includes upstream recorder, CV visibility, automatic theme and control copy,
    // plus the warmup controls and diagnostics. Relative gzip limits stay unchanged.
    // dev 0f217b00 raw budget 1,338,200 B + the unchanged 13,452 B warmup allowance.
    // Measured raw: dev 1,338,151 B, PR 1,350,413 B (Node 24.14.1).
    // Warmup gzip growth: +3,664 B (+0.98%), below the existing +7,680 B limit.
    raw: 1_351_652,
    gzipBaselineCeiling: 362_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  // Dark-theme contrast fixes (accent tokens, heating advice, tooltips, badges and inputs) add ~4.5 kB raw / ~0.8 kB gzip.
  { file: "css/openquatt-app.css", raw: 213_515 },
];
