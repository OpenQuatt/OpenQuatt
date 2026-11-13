import { escapeHtml } from "../core/html.js";
import { formatDateTime, formatNumber, t } from "../i18n/index.js";
import { renderHouseLearningReadiness, selectDisplayedHouseLine } from "./house-learning-readiness.js";
import { renderSettingsInfoToggle } from "./controls.js";

const REQUIRED_COLUMNS = ["start_epoch_s", "end_epoch_s", "mean_outside_c", "mean_heat_w"];
const QUALITY_COLUMNS = ["heat_coverage_bp", "temperature_coverage_bp", "heat_uncertainty_mean_w", "fit_eligible", "fit_weight_bp"];
const WIDTH = 720;
const HEIGHT = 320;
const PADDING = { top: 26, right: 24, bottom: 42, left: 52 };

const finite = (value) => Number.isFinite(value) ? value : null;
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const translatedError = (translationKey) => Object.assign(new Error(t(translationKey)), { translationKey });
const format = (value, digits = 1) => formatNumber(value, { maximumFractionDigits: digits, minimumFractionDigits: 0 });
const formatRecordDate = (epoch) => formatDateTime(epoch * 1000, { day: "numeric", month: "short", year: "numeric", hour: "2-digit", minute: "2-digit" });
const formatDuration = (seconds) => {
  const minutes = Math.max(0, Math.round(seconds / 60));
  return minutes >= 60
    ? t("houseLearning.chart.durationHoursMinutes", { hours: formatNumber(Math.floor(minutes / 60)), minutes: formatNumber(minutes % 60) })
    : t("houseLearning.chart.durationMinutes", { minutes: formatNumber(minutes) });
};

function recordQualities(payload) {
  if (!["record_quality_policy", "record_quality_columns", "record_quality"].some(key => Object.hasOwn(payload, key))) return null;
  const columns = payload.record_quality_columns;
  const rows = payload.record_quality;
  const aligned = payload.record_quality_policy === 1 && Array.isArray(columns) && columns.every(column => typeof column === "string" && column.length > 0)
    && new Set(columns).size === columns.length && QUALITY_COLUMNS.every(column => columns.includes(column))
    && Array.isArray(rows) && rows.length === payload.records.length;
  const index = aligned ? Object.fromEntries(columns.map((column, position) => [column, position])) : {};
  const basisPoints = value => Number.isInteger(value) && value >= 0 && value <= 10000;
  return payload.records.map((_, position) => {
    const row = aligned ? rows[position] : null;
    if (!Array.isArray(row) || row.length !== columns.length) return { valid: false };
    const [heatCoverageBp, temperatureCoverageBp, heatUncertaintyMeanW, fitEligible, fitWeightBp] = QUALITY_COLUMNS.map(column => row[index[column]]);
    if (![heatCoverageBp, temperatureCoverageBp].every(value => value === null || basisPoints(value))
      || !(heatUncertaintyMeanW === null || heatUncertaintyMeanW === "overflow"
        || Number.isInteger(heatUncertaintyMeanW) && heatUncertaintyMeanW >= 0 && heatUncertaintyMeanW <= 65533)
      || typeof fitEligible !== "boolean" || !basisPoints(fitWeightBp)) return { valid: false };
    return { valid: true, heatCoverageBp, temperatureCoverageBp, heatUncertaintyMeanW, fitEligible, fitWeightBp };
  });
}

const usableForChartModel = record => record.quality === undefined
  || record.quality?.valid && record.quality.fitEligible && record.quality.fitWeightBp > 0;

export function normalizeHouseLearningExport(payload = {}) {
  if (Number(payload.schema) !== 1 || payload.mode !== "passive" || !Array.isArray(payload.record_columns) || !Array.isArray(payload.records)) {
    throw translatedError("houseLearning.errors.unknownExportFormat");
  }
  const columns = payload.record_columns.map((column) => String(column));
  if (!REQUIRED_COLUMNS.every((column) => columns.includes(column))) throw translatedError("houseLearning.errors.exportMissingColumns");
  const index = Object.fromEntries(columns.map((column, position) => [column, position]));
  const qualities = recordQualities(payload);
  return payload.records.map((row, position) => {
    if (!Array.isArray(row)) return null;
    const startEpoch = finite(row[index.start_epoch_s]);
    const endEpoch = finite(row[index.end_epoch_s]);
    const meanOutsideC = finite(row[index.mean_outside_c]);
    const converted = Object.hasOwn(index, "effective_outside_c");
    const outsideC = converted ? finite(row[index.effective_outside_c]) : meanOutsideC;
    const recordKind = Object.hasOwn(index, "record_kind") ? row[index.record_kind] : 0;
    if ((recordKind !== 0 && recordKind !== 1) || (recordKind === 1 && !converted)) return null;
    const heatW = finite(row[index.mean_heat_w]);
    if (startEpoch == null || endEpoch == null || startEpoch <= 0 || endEpoch <= startEpoch || meanOutsideC == null || outsideC == null || heatW == null || (heatW < 0 && recordKind !== 1)) return null;
    return { startEpoch, endEpoch, outsideC, heatW, recordKind,
      ...(converted ? { meanOutsideC, referenceRoomC: finite(payload.reference_room_c) ?? 20 } : {}),
      ...(qualities ? { quality: qualities[position] } : {}),
    };
  }).filter(Boolean);
}

function ciSvg(model, learned, fitRecords) {
  const line = learned?.ready === true ? learned : learned?.preview;
  const hasCov = line?.h > 0 && Number.isFinite(line.t0) && line.cov00 >= 0 && Number.isFinite(line.cov01) && line.cov11 >= 0;
  let localFit = null, fanFit = null;
  if (!hasCov && fitRecords?.length >= 1) {
    let s1 = 0, st = 0, stt = 0, sy = 0, sty = 0, n = 0, minT = Infinity, maxT = -Infinity;
    for (const r of fitRecords) {
      if (!Number.isFinite(r.outsideC) || !Number.isFinite(r.heatW)) continue;
      n++; s1 += 1; st += r.outsideC; stt += r.outsideC * r.outsideC; sy += r.heatW; sty += r.outsideC * r.heatW;
      if (r.outsideC < minT) minT = r.outsideC; if (r.outsideC > maxT) maxT = r.outsideC;
    }
    const det = s1 * stt - st * st;
    if (n >= 2 && (maxT - minT) >= 4 && det > 1e-6) {
      const b0 = (stt * sy - st * sty) / det, b1 = (s1 * sty - st * sy) / det;
      if (b1 < 0) {
        let rss = 0; for (const r of fitRecords) { const e = b0 + b1 * r.outsideC - r.heatW; rss += e * e; }
        const s2 = n > 2 ? rss / (n - 2) : Math.max(25000, rss);
        localFit = { b0, b1, cov00: s2 * stt / det, cov01: -s2 * st / det, cov11: s2 * s1 / det };
      }
    }
    if (!localFit && n >= 1 && model.configuredValid) {
      const meanT = st / n, meanW = sy / n;
      if (meanW > 0 && model.zeroC > meanT) {
        const h = meanW / (model.zeroC - meanT);
        let ssw = 0; for (const r of fitRecords) { const d = r.heatW - meanW; ssw += d * d; }
        const baseMargin = n > 1 ? Math.max(450, 2.5 * Math.sqrt(ssw / (n - 1))) : Math.max(450, 0.25 * meanW);
        const slopeMargin = Math.max(30, 0.30 * h);
        fanFit = { h, zeroC: model.zeroC, meanT, baseMargin, slopeMargin };
      }
    }
  }
  if (!hasCov && !localFit && !fanFit) return "";
  let upper = "", lower = "";
  const yMin = Math.max(0, model.axisMinY), yMax = model.axisMaxY;
  for (let t = model.minX; t <= model.maxX; t += 0.5) {
    const y = hasCov ? line.h * (line.t0 - t) : localFit ? localFit.b0 + localFit.b1 * t : fanFit.h * Math.max(0, fanFit.zeroC - t);
    const c = hasCov ? line : localFit;
    const rawMargin = c ? 1.96 * Math.sqrt(Math.max(0, c.cov00 - 2 * c.cov01 * t + c.cov11 * t * t)) : fanFit.baseMargin + fanFit.slopeMargin * Math.abs(t - fanFit.meanT);
    const margin = !c && t > fanFit.zeroC ? Math.max(0, rawMargin * Math.max(0, 1 - (t - fanFit.zeroC) / 2)) : rawMargin;
    const x = model.x(t).toFixed(1);
    upper += (upper ? " L " : "M ") + x + " " + model.y(clamp(y + margin, yMin, yMax)).toFixed(1);
    lower = "L " + x + " " + model.y(clamp(y - margin, yMin, yMax)).toFixed(1) + (lower ? " " + lower : "");
  }
  return `<path d="${upper} ${lower} Z" class="oq-house-learning-chart-interval"/>`;
}

export function getHouseLearningChartModel(records, configured = {}, learned = {}, width = WIDTH) {
  const config = { coldC: finite(configured.coldC), zeroC: finite(configured.zeroC), ratedW: finite(configured.ratedW) };
  const line = selectDisplayedHouseLine(learned);
  const fit = { h: finite(line.h), t0: finite(line.t0), ready: learned.ready === true };
  const configuredValid = config.coldC != null && config.zeroC != null && config.ratedW != null && config.zeroC > config.coldC && config.ratedW > 0;
  const learnedValid = fit.h != null && fit.t0 != null && fit.h > 0;
  const minX = -10;
  const maxX = 20;
  const learnedPower = (temperature) => learnedValid ? Math.max(0, fit.h * (fit.t0 - temperature)) : 0;
  const configuredPowerAtMin = configuredValid ? Math.max(0, config.ratedW * ((config.zeroC - minX) / (config.zeroC - config.coldC))) : 0;
  const maxY = Math.max(500, ...records.map((record) => record.heatW), configuredPowerAtMin, learnedPower(minX), learnedPower(maxX));
  const minY = Math.min(0, ...records.map((record) => record.heatW));
  const yStep = Math.max(500, Math.ceil(Math.max(maxY, -minY) / 5000) * 1000);
  const axisMinY = Math.floor(minY / yStep) * yStep;
  const axisMaxY = Math.ceil(maxY / yStep) * yStep;
  const plotWidth = width - PADDING.left - PADDING.right;
  const plotHeight = HEIGHT - PADDING.top - PADDING.bottom;
  const x = (temperature) => PADDING.left + ((temperature - minX) / Math.max(1, maxX - minX)) * plotWidth;
  const y = (power) => PADDING.top + (1 - clamp((power - axisMinY) / (axisMaxY - axisMinY), 0, 1)) * plotHeight;
  return { width, ...config, ...fit, configuredValid, learnedValid, minX, maxX, axisMinY, axisMaxY, yStep, x, y, plotWidth, plotHeight };
}

function pathFor(model, from, to, power, zeroC) {
  const points = [from, zeroC, to].filter((t, i, values) => t >= from && t <= to && values.indexOf(t) === i).sort((a, b) => a - b);
  return points.map((t, i) => `${i ? "L" : "M"} ${model.x(t).toFixed(1)} ${model.y(power(t)).toFixed(1)}`).join(" ");
}

function chartTooltip(record) {
  const quality = record.quality;
  const percent = value => value === null ? "—" : formatNumber(value / 10000, { style: "percent", maximumFractionDigits: 2 });
  const qualityLines = quality === undefined ? [] : !quality?.valid ? [t("houseLearning.chart.qualityInvalid")]
    : [
      ...(quality.heatCoverageBp === null && quality.temperatureCoverageBp === null && quality.heatUncertaintyMeanW === null
        ? [t("houseLearning.chart.qualityLegacyUnknown")]
        : [
          t("houseLearning.chart.qualityHeatCoverage", { value: percent(quality.heatCoverageBp) }),
          t("houseLearning.chart.qualityTemperatureCoverage", { value: percent(quality.temperatureCoverageBp) }),
          t("houseLearning.chart.qualityUncertainty", { value: quality.heatUncertaintyMeanW === "overflow"
            ? t("houseLearning.chart.qualityOverflow") : quality.heatUncertaintyMeanW === null
              ? "—" : `≤ ${format(Math.ceil(quality.heatUncertaintyMeanW * 24 / 10) / 100, 2)} kWh` }),
        ]),
      t("houseLearning.chart.qualityWeight", { value: percent(quality.fitWeightBp) }),
      !quality.fitEligible || quality.fitWeightBp === 0 ? t("houseLearning.chart.qualityNotEligible") : "",
    ];
  return [
    formatRecordDate(record.startEpoch),
    t("houseLearning.chart.tooltipDuration", { duration: formatDuration(record.endEpoch - record.startEpoch) }),
    t(record.recordKind === 1 ? "houseLearning.chart.tooltipDaily" : "houseLearning.chart.tooltipLegacy"),
    t("houseLearning.chart.tooltipOutside", { value: format(record.meanOutsideC ?? record.outsideC) }),
    record.referenceRoomC != null ? t("houseLearning.chart.tooltipEffectiveOutside", { temperature: format(record.referenceRoomC), value: format(record.outsideC) }) : "",
    t("houseLearning.chart.tooltipHeat", { value: format(record.heatW, 0) }),
    ...qualityLines,
  ].filter(Boolean).join("\n");
}

export function renderHouseLearningChart(records, configured, learned) {
  const compact = typeof window !== "undefined" && window.innerWidth < 640;
  const model = getHouseLearningChartModel(records, configured, learned, compact ? 360 : WIDTH);
  const dailyFit = records.some((record) => record.referenceRoomC != null) || finite(learned?.referenceRoomC) != null;
  const usableRecords = records.filter(usableForChartModel);
  const hasQuality = records.some(record => record.quality !== undefined);
  const readiness = dailyFit ? renderHouseLearningReadiness(usableRecords, learned, records) : "";
  if (!records.length && !model.configuredValid && !model.learnedValid) return `${readiness}<div class="oq-house-learning-chart-empty"><strong>${escapeHtml(t("houseLearning.chart.emptyTitle"))}</strong><span>${escapeHtml(t("houseLearning.chart.emptyCopy"))}</span></div>`;
  const fitRecords = dailyFit ? usableRecords.filter((record) => record.recordKind === 1) : usableRecords;
  const measuredMin = fitRecords.length ? Math.min(...fitRecords.map((record) => record.outsideC)) : null;
  const measuredMax = fitRecords.length ? Math.max(...fitRecords.map((record) => record.outsideC)) : null;
  const yGrid = Array.from({ length: Math.round((model.axisMaxY - model.axisMinY) / model.yStep) + 1 }, (_, index) => model.axisMinY + index * model.yStep);
  const xGrid = compact ? [-10, 0, 10, 20] : [-10, -5, 0, 5, 10, 15, 20];
  const configuredPower = (temperature) => model.configuredValid
    ? Math.max(0, model.ratedW * ((model.zeroC - temperature) / (model.zeroC - model.coldC)))
    : 0;
  const learnedPower = (temperature) => Math.max(0, model.h * (model.t0 - temperature));
  const previewRange = learned?.preview !== undefined ? learned.preview : learned?.fit;
  const rangeMin = !model.ready ? finite(previewRange?.minC) : measuredMin;
  const rangeMax = !model.ready ? finite(previewRange?.maxC) : measuredMax;
  const learnedStart = rangeMin == null ? model.minX : clamp(rangeMin, model.minX, model.maxX);
  const learnedEnd = rangeMax == null ? model.maxX : clamp(rangeMax, model.minX, model.maxX);
  const measuredStart = measuredMin == null ? model.minX : clamp(measuredMin, model.minX, model.maxX);
  const measuredEnd = measuredMax == null ? model.maxX : clamp(measuredMax, model.minX, model.maxX);
  const learnedRanges = !model.ready
    ? rangeMin != null && rangeMax != null && rangeMax > rangeMin ? [[model.minX, model.maxX, true]] : []
    : fitRecords.length && learnedEnd > learnedStart
    ? [[model.minX, learnedStart, true], [learnedStart, learnedEnd, false], [learnedEnd, model.maxX, true]]
    : [[model.minX, model.maxX, true]];
  const confidenceBand = ciSvg(model, learned, fitRecords);
  const learnedLine = model.learnedValid ? learnedRanges.filter(([from, to]) => to > from).map(([from, to, dashed]) =>
    `<path d="${pathFor(model, from, to, learnedPower, model.t0)}" class="oq-house-learning-chart-line ${dashed ? "oq-house-learning-chart-line--learned-dashed" : "oq-house-learning-chart-line--learned"}"/>`).join("") : "";
  const visibleRecords = records.filter((record) => record.outsideC >= model.minX && record.outsideC <= model.maxX);
  const tooltip = visibleRecords.map((record) => `<g class="oq-house-learning-chart-point" data-oq-house-learning-tip="${escapeHtml(chartTooltip(record))}" tabindex="0" role="button" aria-label="${escapeHtml(chartTooltip(record))}"><title>${escapeHtml(chartTooltip(record))}</title><circle cx="${model.x(record.outsideC).toFixed(1)}" cy="${model.y(record.heatW).toFixed(1)}" r="12" class="oq-house-learning-chart-hit"/><circle cx="${model.x(record.outsideC).toFixed(1)}" cy="${model.y(record.heatW).toFixed(1)}" r="3.2" class="oq-house-learning-chart-dot"/></g>`).join("");
  const legacyRecords = records.filter((record) => record.recordKind === 0);
  const notes = [
    dailyFit
      ? t("houseLearning.chart.referenceRoom", { temperature: format(finite(learned?.referenceRoomC) ?? 20) }) : "",
    legacyRecords.length ? t("houseLearning.chart.legacyRecords", { count: formatNumber(legacyRecords.length) }) : "",
    model.configuredValid ? t("houseLearning.chart.configuredLineNote") : "",
    hasQuality && records.length && !fitRecords.length
      ? t("houseLearning.chart.qualityNoEligible") : !fitRecords.length
      ? t(model.learnedValid && model.ready ? "houseLearning.chart.noPeriodsWithExtrapolation" : "houseLearning.chart.noPeriods")
      : t(dailyFit
        ? fitRecords.length === 1 ? "houseLearning.chart.dailyPeriodCountOne" : "houseLearning.chart.dailyPeriodCountMany"
        : fitRecords.length === 1 ? "houseLearning.chart.periodCountOne" : "houseLearning.chart.periodCountMany", { count: formatNumber(fitRecords.length) }),
    visibleRecords.length < records.length ? t("houseLearning.chart.outsideAxis", { count: formatNumber(records.length - visibleRecords.length) }) : "",
    !model.configuredValid ? t("houseLearning.chart.configuredLineIncomplete") : "",
    !model.learnedValid ? dailyFit ? "" : t("houseLearning.chart.noBatchEstimate")
      : !model.ready ? t(learnedLine ? "houseLearning.chart.provisionalLine" : "houseLearning.chart.provisionalRangeMissing") : "",
  ].filter(Boolean);
  const summary = hasQuality
    ? t("houseLearning.chart.qualitySummary", { retained: formatNumber(records.length), eligible: formatNumber(fitRecords.length) })
    : t(dailyFit ? "houseLearning.chart.compactDailySummary" : "houseLearning.chart.compactSummary", { count: formatNumber(fitRecords.length), temperature: format(finite(learned?.referenceRoomC) ?? 20) });
  return `${readiness}
    <div class="oq-house-learning-chart-legend">${model.configuredValid ? `<span><i class="oq-house-learning-chart-swatch oq-house-learning-chart-swatch--configured"></i>${escapeHtml(t("houseLearning.chart.legendConfigured"))}</span>` : ""}${visibleRecords.length ? `<span><i class="oq-house-learning-chart-swatch oq-house-learning-chart-swatch--records"></i>${escapeHtml(t("houseLearning.chart.legendRecords"))}</span>` : ""}${learnedLine ? `<span><i class="oq-house-learning-chart-swatch oq-house-learning-chart-swatch--learned"></i>${escapeHtml(t(model.ready ? "houseLearning.chart.legendLearned" : "houseLearning.chart.legendLearnedProvisional"))}</span>` : ""}${confidenceBand ? `<span><i class="oq-house-learning-chart-swatch oq-house-learning-chart-swatch--interval"></i>${escapeHtml(t("houseLearning.chart.legendInterval"))}</span>` : ""}</div>
    <div class="oq-house-learning-chart-wrap">
      <svg class="oq-house-learning-chart" viewBox="0 0 ${model.width} ${HEIGHT}" role="img" aria-label="${escapeHtml(t("houseLearning.chart.ariaLabel"))}">
        ${fitRecords.length && measuredEnd > measuredStart ? `<rect x="${model.x(measuredStart)}" y="${PADDING.top}" width="${model.x(measuredEnd) - model.x(measuredStart)}" height="${model.plotHeight}" class="oq-house-learning-chart-range"/>` : ""}${confidenceBand}
        ${xGrid.map((value) => `<line x1="${model.x(value)}" x2="${model.x(value)}" y1="${PADDING.top}" y2="${HEIGHT - PADDING.bottom}" class="oq-house-learning-chart-grid"/>`).join("")}
        ${yGrid.map((value) => `<line x1="${PADDING.left}" y1="${model.y(value).toFixed(1)}" x2="${model.width - PADDING.right}" y2="${model.y(value).toFixed(1)}" class="oq-house-learning-chart-grid"/><text x="${PADDING.left - 9}" y="${(model.y(value) + 4).toFixed(1)}" text-anchor="end" class="oq-house-learning-chart-axis">${escapeHtml(format(value / 1000))}</text>`).join("")}
        ${xGrid.map((value) => `<text x="${model.x(value).toFixed(1)}" y="${HEIGHT - 16}" text-anchor="middle" class="oq-house-learning-chart-axis">${escapeHtml(format(value))}°</text>`).join("")}
        <text x="${PADDING.left}" y="16" class="oq-house-learning-chart-unit">kW</text><text x="${model.width - PADDING.right}" y="${HEIGHT - 3}" text-anchor="end" class="oq-house-learning-chart-unit">${escapeHtml(t("houseLearning.chart.outsideTemperatureAxis"))}</text>
        ${model.configuredValid ? `<path d="${pathFor(model, model.minX, model.maxX, configuredPower, model.zeroC)}" class="oq-house-learning-chart-line oq-house-learning-chart-line--configured"/>` : ""}
        ${learnedLine}
        ${tooltip}
      </svg>
      <div class="oq-house-learning-chart-tooltip" aria-hidden="true"></div>
    </div>

    <div class="oq-house-learning-chart-footer"><span>${escapeHtml(summary)}</span>${renderSettingsInfoToggle("house-learning-chart", t("houseLearning.chart.panelTitle"), notes.join("\n\n"))}</div>
  `;
}

export function handleHouseLearningChartPointerMove(event) {
  if (event.type === "focusout" && !event.relatedTarget?.closest?.("[data-oq-house-learning-tip]")) {
    event.target.closest?.(".oq-house-learning-chart-wrap")?.querySelector(".oq-house-learning-chart-tooltip")?.classList.remove("is-visible");
    return;
  }
  const point = event.target.closest?.("[data-oq-house-learning-tip]");
  const wrap = point?.closest?.(".oq-house-learning-chart-wrap") || event.target.closest?.(".oq-house-learning-chart-wrap");
  const tooltip = wrap?.querySelector(".oq-house-learning-chart-tooltip");
  if (!point || !wrap || !tooltip) {
    tooltip?.classList.remove("is-visible");
    return;
  }
  const lines = String(point.dataset.oqHouseLearningTip || "").split(/\n/).filter(Boolean);
  tooltip.innerHTML = `<strong>${escapeHtml(lines.shift() || "")}</strong>${lines.map((line) => `<span>${escapeHtml(line)}</span>`).join("")}`;
  tooltip.classList.add("is-visible");
  const rect = wrap.getBoundingClientRect();
  const tooltipRect = tooltip.getBoundingClientRect();
  const pointRect = point.getBoundingClientRect?.();
  const clientX = Number.isFinite(event.clientX) ? event.clientX : (pointRect?.left || rect.left) + ((pointRect?.width || 0) / 2);
  const clientY = Number.isFinite(event.clientY) ? event.clientY : (pointRect?.top || rect.top);
  const left = Math.min(Math.max(8, clientX - rect.left + 12), Math.max(8, rect.width - tooltipRect.width - 8));
  const top = Math.min(Math.max(8, clientY - rect.top - tooltipRect.height - 12), Math.max(8, rect.height - tooltipRect.height - 8));
  tooltip.style.transform = `translate(${left.toFixed(0)}px, ${top.toFixed(0)}px)`;
}
