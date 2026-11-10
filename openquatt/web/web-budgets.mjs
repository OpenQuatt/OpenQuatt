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
    // Complete heating/cooling inputs: +314 B raw versus dev (Node 24.19.0).
    // Grouped start sequences, truthful historical evidence and confirmed compressor history:
    // Includes stop sequences, observed operating periods, concise details and recorded restart waits.
    // +23,809 B raw / +7,071 B gzip versus #882 on Node 24.19.0; relative gzip limits unchanged.
    // Usage telemetry water-supply source adds 238 B raw / 125 B gzip on Node 24.14.1.
    // Plus the unchanged 13,452 B allowance for warmup controls and diagnostics.
    // dev 9b8dfa57 raw budget 1,363,500 B; relative gzip limits stay unchanged.
    // Measured raw: dev 1,363,238 B, PR 1,375,748 B (Node 24.14.1); gzip +3,779 B (+0.99%).
    // Passive-learning progress, readiness and both locales: +10,701 B raw / ~3.1 kB gzip
    // versus dev 388f6f9d on Node 24.14.1; preserves the existing 1,204 B raw headroom.
    // Delta T flow control settings, status and copy in both locales:
    // measured raw dev 1,375,784 B, PR 1,379,267 B (+3,483 B; gzip +1,275 B, Node 24.19.0).
    // Shared allowance for recorder copy preparation (+2,000 B) and DHW-only diagnostics (+1,161 B).
    // Keep PRs #897 and #899 compatible in either merge order; relative gzip limits stay unchanged.
    // Daily recovery reasons and conditional gap-budget copy in both locales:
    // +2,378 B raw / +702 B gzip versus dev 5ae3951f (Node 24.14.1).
    // Preserve the existing 411 B raw headroom and unchanged relative gzip limits.
    // Retained daily quality, server weights and truthful restart help (Node24.14.1):
    // +4,705 B raw / +1,365 B gzip versus dev2575a93d.
    // Preserve444 B raw headroom; all relative gzip limits remain unchanged.
    // Learning page, two measurement bars and retained-day progress in both locales:
    // +6,919 B raw / +1,750 B gzip versus dev 6dd491f5 (Node 24.14.1).
    // Bounded readback, export and focus handling add 556 B raw / 134 B gzip.
    // Preserve 444 B of raw headroom and the existing relative gzip limits.
    // Provisional curve ranges and firmware fit progress: +1,860 B raw / +550 B gzip
    // versus b18f4d15 on Node 24.14.1, preserving 444 B raw headroom.
    // All-days display preview and NL/EN explanation: +1,300 B raw vs664f45c.
    // Preserve444 B raw headroom; relative gzip limits remain unchanged.
    // Outlook, localized settings, atomic scenario and the interactive weather charts.
    // On current PR1: +30,871 B raw / +9,824 B gzip (Node24.14.1).
    // Keep the existing 9,922 B migration allowance.
    // Inherit only PR1's 588 B raw budget increase; merge guards leave 122 B raw headroom.
    // Merged raw/gzip: 1,443,352 / 405,092 B; PR1 gzip base: 395,270 B (Node24.14.1).
    // Move the absolute ceiling with that base, preserving the feature allowance.
    // Demand comparison retains its existing 5,763 B raw allowance over PR2.
    // Measured delta over PR2: 5,762 B raw / 2,074 B gzip (Node24.14.1).
    // Until the parent PR merges, the dev-targeted stack includes that exact gzip delta.
    // Preserve the inherited 100 B migration margin and the regular relative limits.
    // Bounded expiry refresh and write invalidation: +1,501 B raw / +469 B gzip
    // versus 63002f64 on Node24.14.1; keep the existing raw/gzip headroom.
    // Optional outside source: +1,243 B raw / +350 B gzip versus PR2 (Node24.14.1).
    // Add that measured delta on top of dev's ceilings; relative limits stay unchanged.
    // Settings change log (settings group, table, posting entries and copy in both locales):
    // measured raw 1,400,322 B, +4,053 B versus the feedback-position commit (Node 24.4.1); 80 B raw headroom.
    // Undo for plain settings writes in the feedback toast (#783): +1,286 B raw; 80 B raw headroom.
    // Change-log JSON export, fault outline on heat-pump panels and a fault pill next to the logo:
    // +1,832 B raw; 80 B raw headroom.
    // Combined with dev eef39329+: measured raw 1,459,066 B / gzip 410,912 B (Node 24.4.1),
    // +2,642 B gzip versus dev (within the relative limit); 80 B raw headroom.
    // Leren uses the Settings side navigation for its two panels: +123 B raw; 80 B raw headroom.
    // Results and Diagnose share a Charts tab with a side navigation that remembers the last view:
    // +1,264 B raw; 80 B raw headroom.
    // Settings tabs of their own for Backup and Data sharing, moved out of System: +622 B raw; 80 B raw headroom.
    // A change-log link in the settings toast and stacked toasts (up to three, fanned out on hover,
    // dismissable one by one, undo per toast): +3,790 B raw; 80 B raw headroom.
    // Toasts on Leren and a brain icon for its tab: measured raw 1,464,379 B (Node 24.4.1); 80 B raw headroom.
    // E-mail notifications (integrations section, settings modal with provider fields, save/test polling and
    // copy in both locales): measured raw 1,488,112 B versus dev 1,464,360 B (+23,752 B; ~8 kB of it locale
    // copy), gzip +6,943 B (+1.68%, within the relative limit; Node 24.4.1); 80 B raw headroom.
    raw: 1_488_192,
    gzipBaselineCeiling: 408_085,
  },
  // Includes passive-learning cards and the curve editor: ~207.9 kB raw.
  // Dark-theme contrast fixes (accent tokens, heating advice, tooltips, badges and inputs) add ~4.5 kB raw / ~0.8 kB gzip.
  // Passive-learning layout: +2,969 B raw / +493 B gzip versus dev 388f6f9d
  // on Node 24.14.1; preserves the existing 1,581 B raw headroom.
  // Outlook and weather graphs add 4,367 B raw and 823 B gzip.
  // Heat-pump schematic motion moves from a per-frame JS loop to CSS animations:
  // +408 B CSS raw while the JS bundle shrinks by 1,392 B raw (Node 24.4.1);
  // preserves the existing 80 B raw headroom.
  // A top or bottom position for settings feedback, chosen in the interface menu: +105 B CSS raw;
  // preserves the 80 B raw headroom.
  // Settings change log table and its stacked phone layout: +973 B CSS raw; preserves the 80 B raw headroom.
  // Edge fades on sideways-scrolling tab strips, driven by a scroll timeline: +939 B CSS raw;
  // preserves the 80 B raw headroom.
  // Touch-safe sliders and the undo action in the feedback toast (#783): +870 B CSS raw;
  // preserves the 80 B raw headroom.
  // Fault outline, fault notice tone, header fault pill and wrapping section actions: +1,066 B CSS raw;
  // preserves the 80 B raw headroom.
  // The calmer UI restyles every view at once: +1,607 B gzip versus dev 6dd491f5 (+3.8%), above the 3%
  // relative limit. This ceiling admits that once with ~180 B gzip headroom; after the merge the base
  // reaches the ceiling, so later changes fall back to the regular relative limit.
  // Re-measured after merging dev with the weather graphs: raw 225,789 B, gzip 44,805 B versus
  // dev 43,167 B (+1,638 B, +3.8%; Node 24.4.1); ~180 B gzip and 80 B raw headroom.
  // Charts tab: energy-history controls stack by panel width and the side-navigation column shrinks its
  // content: +161 B CSS raw; preserves the 80 B raw headroom.
  { file: "css/openquatt-app.css", raw: 225_975, gzipBaselineCeiling: 44_985 },
];
