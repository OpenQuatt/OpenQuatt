// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 4_608, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes HCQ R2 settings, bounded usage-telemetry confirmation polling,
    // source-bound supply-temperature calibration status and results,
    // its read-only sensor-correction summary, calibration backup/restore,
    // the read-only ODU EEPROM service export, API ingress source controls,
    // and concise CM100 boiler test phase copy (FLOW_SETTLING/BOILER_SETTLING/MEASURING/COOLDOWN).
    raw: 902_000,
    // One-time migration ceiling for structured incident monitoring, replay,
    // the CSRF-protected deferred recovery actions, and their compact editor.
    // Once this bundle is the base, the normal gzip growth limit applies again.
    gzipBaselineCeiling: 238_000,
  },
  { file: "css/openquatt-app.css", raw: 277_000 },
];
