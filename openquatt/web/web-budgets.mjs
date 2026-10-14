// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes passive learning, trend goals, the curve editor and air-purge status.
    // With daily-learning UI and legacy compatibility, plus the performance invitation.
    // Includes topology-aware Single rendering and historical heating-stop reasons from dev.
    // Controlled warmup adds its controls, diagnostics and controller-backed durations.
    // Merged dev 1d97bef7: 1,341,385 B raw versus dev 1,330,911 B on Node 24.14.1.
    // Warmup gzip growth is +3,340 B; the relative gzip growth limit remains unchanged.
    raw: 1_345_000,
    gzipBaselineCeiling: 362_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  { file: "css/openquatt-app.css", raw: 209_000 },
];
