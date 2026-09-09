import { escapeHtml } from "../core/html.js";

const REQUIRED_COLUMNS = ["start_epoch_s", "end_epoch_s", "mean_outside_c", "mean_heat_w"];
const WIDTH = 720;
const HEIGHT = 320;
const PADDING = { top: 26, right: 24, bottom: 42, left: 52 };

const finite = (value) => Number.isFinite(value) ? value : null;
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const format = (value, digits = 1) => Number(value).toLocaleString("nl-NL", { maximumFractionDigits: digits, minimumFractionDigits: 0 });
const formatDate = (epoch) => new Date(epoch * 1000).toLocaleString("nl-NL", { day: "numeric", month: "short", year: "numeric", hour: "2-digit", minute: "2-digit" });
const formatDuration = (seconds) => {
  const minutes = Math.max(0, Math.round(seconds / 60));
  return minutes >= 60 ? `${Math.floor(minutes / 60)} u ${minutes % 60} min` : `${minutes} min`;
};

export function normalizeHouseLearningExport(payload = {}) {
  if (Number(payload.schema) !== 1 || payload.mode !== "passive" || !Array.isArray(payload.record_columns) || !Array.isArray(payload.records)) {
    throw new Error("onbekend exportformaat");
  }
  const columns = payload.record_columns.map((column) => String(column));
  if (!REQUIRED_COLUMNS.every((column) => columns.includes(column))) throw new Error("export mist meetkolommen");
  const index = Object.fromEntries(columns.map((column, position) => [column, position]));
  return payload.records.map((row) => {
    if (!Array.isArray(row)) return null;
    const startEpoch = finite(row[index.start_epoch_s]);
    const endEpoch = finite(row[index.end_epoch_s]);
    const outsideC = finite(row[index.mean_outside_c]);
    const heatW = finite(row[index.mean_heat_w]);
    if (startEpoch == null || endEpoch == null || startEpoch <= 0 || endEpoch <= startEpoch || outsideC == null || heatW == null || heatW < 0) return null;
    return { startEpoch, endEpoch, outsideC, heatW };
  }).filter(Boolean);
}

export function getHouseLearningChartModel(records, configured = {}, learned = {}, width = WIDTH) {
  const config = { coldC: finite(configured.coldC), zeroC: finite(configured.zeroC), ratedW: finite(configured.ratedW) };
  const fit = { h: finite(learned.h), t0: finite(learned.t0), ready: learned.ready === true };
  const configuredValid = config.coldC != null && config.zeroC != null && config.ratedW != null && config.zeroC > config.coldC && config.ratedW > 0;
  const learnedValid = fit.h != null && fit.t0 != null && fit.h > 0;
  const minX = -10;
  const maxX = 20;
  const learnedPower = (temperature) => learnedValid ? Math.max(0, fit.h * (fit.t0 - temperature)) : 0;
  const configuredPowerAtMin = configuredValid ? Math.max(0, config.ratedW * ((config.zeroC - minX) / (config.zeroC - config.coldC))) : 0;
  const maxY = Math.max(500, ...records.map((record) => record.heatW), configuredPowerAtMin, learnedPower(minX), learnedPower(maxX));
  const yStep = Math.max(500, Math.ceil(maxY / 5000) * 1000);
  const axisMaxY = Math.ceil(maxY / yStep) * yStep;
  const plotWidth = width - PADDING.left - PADDING.right;
  const plotHeight = HEIGHT - PADDING.top - PADDING.bottom;
  const x = (temperature) => PADDING.left + ((temperature - minX) / Math.max(1, maxX - minX)) * plotWidth;
  const y = (power) => PADDING.top + (1 - clamp(power / axisMaxY, 0, 1)) * plotHeight;
  return { width, ...config, ...fit, configuredValid, learnedValid, minX, maxX, axisMaxY, yStep, x, y, plotWidth, plotHeight };
}

function pathFor(model, from, to, power, zeroC) {
  const points = [from, zeroC, to].filter((t, i, values) => t >= from && t <= to && values.indexOf(t) === i).sort((a, b) => a - b);
  return points.map((t, i) => `${i ? "L" : "M"} ${model.x(t).toFixed(1)} ${model.y(power(t)).toFixed(1)}`).join(" ");
}

function chartTooltip(record) {
  return [
    formatDate(record.startEpoch),
    `Duur: ${formatDuration(record.endEpoch - record.startEpoch)}`,
    `Buiten: ${format(record.outsideC)} °C`,
    `Warmte: ${format(record.heatW, 0)} W`,
  ].join("\n");
}

export function renderHouseLearningChart(records, configured, learned) {
  const compact = typeof window !== "undefined" && window.innerWidth < 640;
  const model = getHouseLearningChartModel(records, configured, learned, compact ? 360 : WIDTH);
  if (!records.length && !model.configuredValid) return '<div class="oq-house-learning-chart-empty"><strong>Nog geen grafiek beschikbaar</strong><span>Er zijn geen geaccepteerde batchperioden en de Power House-instellingen zijn onvolledig.</span></div>';
  const measuredMin = records.length ? Math.min(...records.map((record) => record.outsideC)) : null;
  const measuredMax = records.length ? Math.max(...records.map((record) => record.outsideC)) : null;
  const yGrid = Array.from({ length: Math.round(model.axisMaxY / model.yStep) + 1 }, (_, index) => index * model.yStep);
  const xGrid = compact ? [-10, 0, 10, 20] : [-10, -5, 0, 5, 10, 15, 20];
  const configuredPower = (temperature) => model.configuredValid
    ? Math.max(0, model.ratedW * ((model.zeroC - temperature) / (model.zeroC - model.coldC)))
    : 0;
  const learnedPower = (temperature) => Math.max(0, model.h * (model.t0 - temperature));
  const learnedStart = measuredMin == null ? model.minX : clamp(measuredMin, model.minX, model.maxX);
  const learnedEnd = measuredMax == null ? model.maxX : clamp(measuredMax, model.minX, model.maxX);
  const learnedRanges = records.length && learnedEnd > learnedStart
    ? [[model.minX, learnedStart, true], [learnedStart, learnedEnd, false], [learnedEnd, model.maxX, true]]
    : [[model.minX, model.maxX, true]];
  const learnedLine = model.learnedValid ? learnedRanges.filter(([from, to]) => to > from).map(([from, to, dashed]) =>
    `<path d="${pathFor(model, from, to, learnedPower, model.t0)}" class="oq-house-learning-chart-line ${dashed ? "oq-house-learning-chart-line--learned-dashed" : "oq-house-learning-chart-line--learned"}"/>`).join("") : "";
  const visibleRecords = records.filter((record) => record.outsideC >= model.minX && record.outsideC <= model.maxX);
  const tooltip = visibleRecords.map((record) => `<g class="oq-house-learning-chart-point" data-oq-house-learning-tip="${escapeHtml(chartTooltip(record))}" tabindex="0" role="button" aria-label="${escapeHtml(chartTooltip(record))}"><title>${escapeHtml(chartTooltip(record))}</title><circle cx="${model.x(record.outsideC).toFixed(1)}" cy="${model.y(record.heatW).toFixed(1)}" r="12" class="oq-house-learning-chart-hit"/><circle cx="${model.x(record.outsideC).toFixed(1)}" cy="${model.y(record.heatW).toFixed(1)}" r="3.2" class="oq-house-learning-chart-dot"/></g>`).join("");
  const notes = [
    "De blauwe lijn toont de ingestelde basiswarmtevraag, vóór kamercorrectie en vermogensbegrenzing.",
    !records.length ? `Nog geen geaccepteerde stabiele meetperioden.${model.learnedValid ? " De groene lijn is volledig een extrapolatie." : ""}` : `${records.length} meetperioden. Gestippeld: buiten het gemeten temperatuurbereik.`,
    visibleRecords.length < records.length ? `${records.length - visibleRecords.length} meetperioden liggen buiten de getoonde as (−10 tot 20 °C).` : "",
    !model.configuredValid ? "De ingestelde Power House-lijn is onvolledig." : "",
    !model.learnedValid ? "Nog geen batchschatting voor H en T₀." : !model.ready ? "De groene H/T₀-lijn is voorlopig en nog niet gevalideerd." : "",
  ].filter(Boolean);
  return `
    <div class="oq-house-learning-chart-legend"><span><i class="oq-house-learning-chart-swatch oq-house-learning-chart-swatch--configured"></i>Ingestelde woninglijn</span><span><i class="oq-house-learning-chart-swatch oq-house-learning-chart-swatch--records"></i>Meetpunten</span><span><i class="oq-house-learning-chart-swatch oq-house-learning-chart-swatch--learned"></i>${model.ready ? "Geleerde woninglijn" : "Geleerde woninglijn · voorlopig"}</span></div>
    <div class="oq-house-learning-chart-wrap">
      <svg class="oq-house-learning-chart" viewBox="0 0 ${model.width} ${HEIGHT}" role="img" aria-label="Power House-lijn en geaccepteerde batchmetingen">
        ${records.length && learnedEnd > learnedStart ? `<rect x="${model.x(learnedStart)}" y="${PADDING.top}" width="${model.x(learnedEnd) - model.x(learnedStart)}" height="${model.plotHeight}" class="oq-house-learning-chart-range"/>` : ""}
        ${xGrid.map((value) => `<line x1="${model.x(value)}" x2="${model.x(value)}" y1="${PADDING.top}" y2="${HEIGHT - PADDING.bottom}" class="oq-house-learning-chart-grid"/>`).join("")}
        ${yGrid.map((value) => `<line x1="${PADDING.left}" y1="${model.y(value).toFixed(1)}" x2="${model.width - PADDING.right}" y2="${model.y(value).toFixed(1)}" class="oq-house-learning-chart-grid"/><text x="${PADDING.left - 9}" y="${(model.y(value) + 4).toFixed(1)}" text-anchor="end" class="oq-house-learning-chart-axis">${escapeHtml(format(value / 1000))}</text>`).join("")}
        ${xGrid.map((value) => `<text x="${model.x(value).toFixed(1)}" y="${HEIGHT - 16}" text-anchor="middle" class="oq-house-learning-chart-axis">${escapeHtml(format(value))}°</text>`).join("")}
        <text x="${PADDING.left}" y="16" class="oq-house-learning-chart-unit">kW</text><text x="${model.width - PADDING.right}" y="${HEIGHT - 3}" text-anchor="end" class="oq-house-learning-chart-unit">Buitentemperatuur (°C)</text>
        ${model.configuredValid ? `<path d="${pathFor(model, model.minX, model.maxX, configuredPower, model.zeroC)}" class="oq-house-learning-chart-line oq-house-learning-chart-line--configured"/>` : ""}
        ${learnedLine}
        ${tooltip}
      </svg>
      <div class="oq-house-learning-chart-tooltip" aria-hidden="true"></div>
    </div>

    ${notes.map((note) => `<p class="oq-house-learning-chart-note">${escapeHtml(note)}</p>`).join("")}
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
