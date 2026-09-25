// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Includes both offline catalogues after build-time key compaction. Keep
    // enough margin for ordinary UI work without accepting the uncompressed form again.
    // PR #742 Linux CI: 1,228,950 B raw / 341,500 B gzip for the defrost UI.
    // Calibrate byte budgets on the Linux CI build; local Windows/CRLF measurements
    // are not comparable. Keep only a small margin above the measured PR bundle.
    raw: 1_230_000,
    gzipBaselineCeiling: 343_000,
  },
  // Responsive run-extension group: ~203.3 kB raw.
  { file: "css/openquatt-app.css", raw: 204_000 },
];
