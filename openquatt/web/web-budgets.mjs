// Fail when gzip growth exceeds the smaller of these two limits.
export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 7_680, ratio: 0.03 };

export const WEB_BUNDLE_BUDGETS = [
  {
    file: "js/openquatt-app.js",
    // Passive Power House learning adds the chart/status/export UI on top of
    // the current Matrix-enabled web app. Candidate: 985517 B raw / 282193 B gzip.
    raw: 990_000,
    gzipBaselineCeiling: 286_000,
  },
  // Candidate with the learning panel/chart: 205258 B raw.
  { file: "css/openquatt-app.css", raw: 207_000 },
];
