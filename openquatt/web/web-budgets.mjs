// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes passive learning, trend goals, the curve editor and air-purge status.
    // With daily-learning UI and legacy compatibility, plus the performance invitation.
    // dev baseline: 1,308,178 B; invitation adds ~7.5 kB raw / ~2.6 kB gzip on Node 24.14.1.
    // Includes topology-aware Single rendering from dev.
    // Historical heating-stop reasons and both locale catalogues add ~10.6 kB raw.
    // Aux relay (R2) run-on settings, copy in both locales and legacy-backup migration add ~2.2 kB raw / ~0.7 kB gzip.
    // The existing relative gzip growth limit remains unchanged.
    // EEPROM service flow adds about 12 kB raw / 4 kB gzip (Node 24.19.0).
    raw: 1_351_000,
    gzipBaselineCeiling: 362_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  { file: "css/openquatt-app.css", raw: 209_000 },
];
