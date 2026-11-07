import { escapeHtml } from "../core/html.js";
import { formatNumber, t } from "../i18n/index.js";
import { renderSettingsInfoToggle } from "./controls.js";

// Current firmware defaults in oq_ph_learning_fit.h. This explains the loaded
// measurements; it does not run the fit or grant permission to apply a model.
const TRAIN_DAYS = 6;
const CHECK_DAYS = 3;
const REQUIRED_SPREAD_C = 6;
const MIN_PER_BIN = 2;
const MAX_BIN_FRACTION = 0.67;
const number = (value) => formatNumber(value, { maximumFractionDigits: 2 });

export function selectDisplayedHouseLine(learned = {}) {
  return learned.ready !== true && learned.preview !== undefined ? learned.preview || {} : learned;
}

function temperatureRange(days) {
  const temperatures = days.map(record => record.outsideC).sort((a, b) => a - b);
  const p10 = temperatures.length ? temperatures[Math.floor((temperatures.length - 1) / 10)] : null;
  const p90 = temperatures.length ? temperatures[Math.ceil(9 * (temperatures.length - 1) / 10)] : null;
  return { temperatures, p10, p90, spread: p10 == null ? null : p90 - p10 };
}

export function getHouseLearningReadiness(records) {
  // The firmware marks full days from monotonic duration; UTC timestamps may differ.
  const days = records.filter((record) => record.recordKind === 1 && Number.isFinite(record.outsideC))
    .slice().sort((a, b) => a.startEpoch - b.startEpoch);
  const checkCount = Math.min(CHECK_DAYS, days.length);
  const train = days.slice(0, days.length - checkCount);
  const { temperatures, p10, p90, spread } = temperatureRange(train);
  const bins = [0, 0, 0];
  if (spread > 0) {
    for (const temperature of temperatures) {
      const bin = Math.max(0, Math.min(2, Math.floor(3 * (temperature - p10) / spread)));
      bins[bin] += 1;
    }
  }
  return {
    count: days.length, trainCount: train.length, checkCount, p10, p90, spread, bins,
    missingDays: Math.max(0, TRAIN_DAYS + CHECK_DAYS - days.length),
    missingSpread: spread == null ? null : Math.max(0, REQUIRED_SPREAD_C - spread),
    distributionReady: spread > 0 && bins.every((count) => count >= MIN_PER_BIN
      && count / train.length <= MAX_BIN_FRACTION),
  };
}

const BATCH_REASON_KEYS = {
  stale_data: "houseLearning.readiness.staleRecords",
  mixed_context: "houseLearning.readiness.mixedContext",
  time_discontinuity: "houseLearning.readiness.invalidRecords",
  invalid_measurement: "houseLearning.readiness.invalidRecords",
  invalid_configuration: "houseLearning.readiness.invalidConfiguration",
  invalid_active_model: "houseLearning.readiness.invalidActiveModel",
  fit_failed: "houseLearning.readiness.fitFailed",
  candidate_implausible: "houseLearning.readiness.fitImplausible",
  unstable_fit: "houseLearning.readiness.fitUnstable",
  no_holdout_improvement: "houseLearning.readiness.noImprovement",
};

export function renderHouseLearningReadiness(records, learned = {}, observedRecords = records) {
  let progress = getHouseLearningReadiness(records);
  if (learned.fit) {
    const fit = learned.fit;
    const spread = fit.p10 == null || fit.p90 == null ? null : fit.p90 - fit.p10;
    progress = {
      ...progress, count: fit.trainCount + fit.checkCount, trainCount: fit.trainDays, checkCount: fit.checkDays,
      p10: fit.p10, p90: fit.p90, spread,
      missingDays: Math.max(0, TRAIN_DAYS - fit.trainDays) + Math.max(0, CHECK_DAYS - fit.checkDays),
      missingChecks: Math.max(0, CHECK_DAYS - fit.checkDays),
      missingSpread: spread == null ? null : Math.max(0, REQUIRED_SPREAD_C - spread),
      distributionReady: !["insufficient_spread", "day_dominance"].includes(learned.batchStatus),
    };
  }
  const line = selectDisplayedHouseLine(learned);
  const available = Number.isFinite(line.h) && line.h > 0 && Number.isFinite(line.t0);
  const previewCopy = learned.preview && available && !learned.ready
    ? t("houseLearning.readiness.previewAllDays", { count: number(learned.preview.count) })
    : t("houseLearning.readiness.provisionalCopy");
  const reasonKey = Object.hasOwn(BATCH_REASON_KEYS, learned.batchStatus) ? BATCH_REASON_KEYS[learned.batchStatus] : null;
  let copy;
  if (reasonKey) copy = t(reasonKey);
  else if (available && learned.ready) copy = t("houseLearning.readiness.readyCopy");
  else if (progress.trainCount >= TRAIN_DAYS && progress.missingChecks) copy = t("houseLearning.readiness.needChecks", { count: number(progress.missingChecks) });
  else if (progress.missingDays) copy = t(progress.missingDays === 1 ? "houseLearning.readiness.needDay" : "houseLearning.readiness.needDays", { count: number(progress.missingDays) });
  else if (progress.missingSpread > 0) copy = t("houseLearning.readiness.needSpread", { value: number(progress.missingSpread) });
  else if (!progress.distributionReady) copy = t("houseLearning.readiness.needDistribution");
  else if (learned.batchStatus === "insufficient_data") copy = t("houseLearning.readiness.controllerNeedsDays");
  else if (learned.batchStatus === "insufficient_spread" || learned.batchStatus === "day_dominance") copy = t("houseLearning.readiness.controllerNeedsSpread");
  else copy = t("houseLearning.readiness.furtherChecks");
  if (available && !learned.ready) copy = previewCopy + " " + copy;
  const title = t(available ? learned.ready ? "houseLearning.readiness.readyTitle" : "houseLearning.readiness.provisionalTitle" : "houseLearning.readiness.waitingTitle");
  const metric = (label, value, required, complete, state, note, fraction) => `<div class="oq-house-learning-readiness-metric${complete ? " is-complete" : ""}">
    <dt>${escapeHtml(label)}</dt><dd>${escapeHtml(value)} <span>${escapeHtml(required)}</span></dd>
    <div class="oq-house-learning-readiness-track" aria-hidden="true"><span style="width:${Math.max(0, Math.min(100, fraction * 100))}%"></span></div>
    ${state ? `<strong>${complete ? "✓ " : ""}${escapeHtml(state)}</strong>` : ""}${note ? `<small>${escapeHtml(note)}</small>` : ""}
  </div>`;
  // Legacy quality may train, but cannot supply independent checking days.
  // Do not reproduce the server's chronology/policy with a naive last-three split.
  const observedDays = observedRecords.filter(record => record.recordKind === 1 && Number.isFinite(record.outsideC));
  const unknownQuality = observedDays.some(record => record.quality?.valid
    && [record.quality.heatCoverageBp, record.quality.temperatureCoverageBp, record.quality.heatUncertaintyMeanW].includes(null));
  if (unknownQuality && !learned.fit) {
    const knownCount = observedDays.filter(record => record.quality?.valid
      && record.quality.heatCoverageBp !== null && record.quality.temperatureCoverageBp !== null
      && record.quality.heatUncertaintyMeanW !== null).length;
    const { p10, p90, spread } = temperatureRange(observedDays);
    const observedSpread = observedDays.length > 1 ? spread : null;
    const requiredDays = TRAIN_DAYS + CHECK_DAYS;
    const serverCopy = reasonKey ? t(reasonKey) : available
      ? learned.ready ? t("houseLearning.readiness.readyCopy") : previewCopy : "";
    return `<section class="oq-house-learning-readiness" aria-label="${escapeHtml(t("houseLearning.readiness.label"))}">
      <div class="oq-house-learning-readiness-head"><h5>${escapeHtml(title)}</h5></div>
      ${serverCopy ? `<p>${escapeHtml(serverCopy)}</p>` : ""}
      <p>${escapeHtml(t("houseLearning.readiness.qualitySplit", { count: number(knownCount) }))}</p>
      <p>${escapeHtml(t("learning.observedCopy"))}</p>
      <dl class="oq-house-learning-readiness-metrics">
        ${metric(t("learning.observedDays"), number(observedDays.length), t("learning.observedDaysTarget", { count: number(requiredDays) }),
          false, "", "", observedDays.length / requiredDays)}
        ${metric(t("learning.observedSpread"), observedSpread == null ? "—" : `${number(observedSpread)} °C`, t("learning.observedSpreadTarget", { value: number(REQUIRED_SPREAD_C) }),
          false, "", observedSpread == null ? "" : t("learning.observedRange", { low: number(p10), high: number(p90) }), (observedSpread ?? 0) / REQUIRED_SPREAD_C)}
      </dl>
    </section>`;
  }
  const countState = t(progress.missingDays === 1 ? "houseLearning.readiness.missingDay" : progress.missingDays ? "houseLearning.readiness.missingDays" : "houseLearning.readiness.sufficient", { count: number(progress.missingDays) });
  const spreadState = t(progress.spread == null ? "houseLearning.readiness.notAvailable" : progress.missingSpread > 0 ? "houseLearning.readiness.missingSpread" : "houseLearning.readiness.sufficient", { value: number(progress.missingSpread ?? 0) });
  const help = [t("houseLearning.readiness.method"), progress.spread > 0
    ? t("houseLearning.readiness.distribution", { cold: number(progress.bins[0]), middle: number(progress.bins[1]), warm: number(progress.bins[2]), minimum: number(MIN_PER_BIN) }) : "",
    t("houseLearning.readiness.loadedCopy")].filter(Boolean).join("\n\n");
  return `<section class="oq-house-learning-readiness" aria-label="${escapeHtml(t("houseLearning.readiness.label"))}">
    <div class="oq-house-learning-readiness-head"><h5>${escapeHtml(title)}</h5>${renderSettingsInfoToggle("house-learning-readiness", t("houseLearning.readiness.label"), help)}</div>
    <p>${escapeHtml(copy)}</p>
    <dl class="oq-house-learning-readiness-metrics">
      ${metric(t("houseLearning.readiness.days"), number(progress.count), t("houseLearning.readiness.requiredDays", { count: number(TRAIN_DAYS + CHECK_DAYS) }), !progress.missingDays, countState,
        t("houseLearning.readiness.split", { train: number(progress.trainCount), check: number(progress.checkCount) }), learned.fit
          ? (Math.min(progress.trainCount, TRAIN_DAYS) + Math.min(progress.checkCount, CHECK_DAYS)) / (TRAIN_DAYS + CHECK_DAYS)
          : progress.count / (TRAIN_DAYS + CHECK_DAYS))}
      ${metric(t("houseLearning.readiness.spread"), progress.spread == null ? "—" : `${number(progress.spread)} °C`, t("houseLearning.readiness.requiredSpread", { value: number(REQUIRED_SPREAD_C) }), progress.spread != null && !progress.missingSpread, spreadState,
        progress.p10 == null ? t("houseLearning.readiness.waitTrainDays") : t("houseLearning.readiness.range", { low: number(progress.p10), high: number(progress.p90), count: number(progress.trainCount) }), (progress.spread ?? 0) / REQUIRED_SPREAD_C)}
    </dl>
  </section>`;
}
