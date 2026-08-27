// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 4_608, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes HCQ R2 settings, bounded usage-telemetry confirmation polling,
    // source-bound supply-temperature calibration status and results,
    // its read-only sensor-correction summary, calibration backup/restore,
    // the read-only ODU EEPROM service export, API ingress source controls,
    // concise CM100 boiler test phase copy (FLOW_SETTLING/BOILER_SETTLING/MEASURING/COOLDOWN),
    // advisory per-ODU generation detection with single bulk-detect button for onboarding and installation settings,
    // the explicitly confirmed dev-to-main firmware downgrade flow,
    // selectable cooling restart by water temperature or minimum off-time,
    // toelichting lokale historie in Quick Start, plus fase-2 flash-I/O observability,
    // strategie-afhankelijke warmtetoestemming-advies (Power House vs stooklijn, OT-voorkeur, centrale modal, auto-set in Quick Start),
    // plus hervatbare, fail-closed Quick Start-OTA met main-/doelcontrole en duurzaam post-bootbewijs.
    raw: 956_000,
    // One-time migration ceiling for structured incident monitoring, replay,
    // the CSRF-protected deferred recovery actions, and their compact editor.
    // Once this bundle is the base, the normal gzip growth limit applies again.
    gzipBaselineCeiling: 238_000,
  },
  // Includes the compact, dark-safe ODU generation picker with unified header action and distinct badge/button,
  // plus the warmtetoestemming-advies modal (3 summary cards, comparison, matrix, sticky footer).
  { file: "css/openquatt-app.css", raw: 295_000 },
];
