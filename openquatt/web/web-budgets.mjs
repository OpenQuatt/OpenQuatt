// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes the complete embedded Dutch and English catalogues. Both must
    // remain available offline because the controller serves one self-contained bundle.
    raw: 1_460_000,
    gzipBaselineCeiling: 390_000,
  },
  { file: "css/openquatt-app.css", raw: 203_000 },
];
