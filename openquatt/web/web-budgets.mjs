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
    // Explicit recorder retries preserve the pending choice and reject stale responses (+1 KiB raw).
    // Hiding CV energy and decision-log views without an auxiliary heat source adds ~0.8 kB raw / ~0.3 kB gzip.
    // An automatic theme that follows the device light/dark setting adds ~1.3 kB raw / ~0.3 kB gzip.
    // The existing relative gzip growth limit remains unchanged.
    raw: 1_337_852,
    gzipBaselineCeiling: 362_000,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  // Dark-theme contrast fixes (accent tokens, heating advice, tooltips, badges and inputs) add ~4.5 kB raw / ~0.8 kB gzip.
  { file: "css/openquatt-app.css", raw: 213_515 },
];
