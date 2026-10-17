// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes passive learning, trend goals, the curve editor and air-purge status.
    // With daily-learning UI and legacy compatibility, plus the performance invitation.
    // Includes topology-aware Single rendering and historical heating-stop reasons from dev.
    // Aux relay (R2) run-on settings and controlled warmup add their controls and diagnostics.
    // Merged dev 745db226: raw 1,346,490 B versus dev 1,334,232 B on Node 24.19.0.
    // Warmup gzip growth is +3,777 B; the relative gzip growth limit remains unchanged.
    raw: 1_350_000,
    gzipBaselineCeiling: 362_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  { file: "css/openquatt-app.css", raw: 209_000 },
];
