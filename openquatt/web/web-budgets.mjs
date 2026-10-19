// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes passive learning, trend goals, the curve editor and air-purge status.
    // With daily-learning UI and legacy compatibility, plus the performance invitation.
    // Includes topology-aware Single rendering and historical heating-stop reasons from dev.
    // Aux relay (R2), recorder retries, optional CV views and controlled warmup
    // share this raw ceiling; the relative gzip growth limit remains unchanged.
    // Merged dev 0c48306e: raw 1,347,973 B versus dev 1,335,715 B (Node 24.14.1).
    // Warmup gzip growth is +3,729 B (+1.00%), below the existing +7,680 B limit.
    raw: 1_350_000,
    gzipBaselineCeiling: 362_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  { file: "css/openquatt-app.css", raw: 209_000 },
];
