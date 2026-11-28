import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { getEntityValue } from "../core/entity-store.js";
import { weatherForecastCanDisplay, weatherCurrentModelIsCurrent } from "../core/weather-forecast.js";
import { formatDateTime, formatNumber, t } from "../i18n/index.js";

const renderedMarkup = new WeakMap();
const renderedReadout = new WeakMap();
let resizeTarget;
const resizeObserver = typeof ResizeObserver === "function" ? new ResizeObserver(() => patchWeatherForecastCursor()) : null;
const format = (value) => formatNumber(value, { maximumFractionDigits: 1 });
const time = (epoch) => formatDateTime(epoch * 1000, { weekday: "short", hour: "2-digit", minute: "2-digit" });
const charts = {
  temperature: { field: "temperature_c", label: "weather.graphTemperature", unit: "°C" },
  wind: { field: "wind_mps", label: "weather.graphWind", unit: "m/s" },
  solar: { field: "solar_wm2", label: "weather.graphSolar", unit: "W/m²" },
};

function windHeading(data, index) {
  const degrees = data.wind_direction_deg?.[index];
  return Number.isFinite(degrees) && data.wind_mps[index] > 0 ? degrees : null;
}

function windDirection(degrees, parentheses = false) {
  if (degrees === null) return "";
  const compass = t("weather.windCompass").split(" ")[Math.round(degrees / 45) % 8];
  const angle = `${format(degrees)}°`;
  return `${compass} ${parentheses ? `(${angle})` : angle}`;
}

function windArrow(heading) {
  return `<svg class="oq-weather-wind-arrow" aria-hidden="true" viewBox="-7 -7 14 14" style="--oq-wind-angle:${(heading + 180) % 360}deg"><path d="M0 5V-5M-3 -2L0 -5L3 -2"/></svg>`;
}

const position = (index) => `${index / 35 * 100}%`;
const arrowPosition = (index) => `clamp(7px, ${position(index)}, calc(100% - 7px))`;

function chartHours(start) {
  const ticks = [], days = [];
  for (let index = 0; index < 36; index++) {
    const epoch = start + index * 3600;
    const date = new Date(epoch * 1000), hour = date.getHours();
    if (hour % 6 === 0) {
      const label = [hour, date.getMinutes()].map(value => String(value).padStart(2, "0")).join(":");
      ticks.push({ index, label, midnight: hour === 0 });
    }
    if (hour === 0 || (index === 0 && hour < 22)) days.push({ index, label: formatDateTime(epoch * 1000, { weekday: "short" }) });
  }
  return { ticks, days };
}

function windPlotArrows(data, ticks) {
  const arrows = ticks.map(({ index }) => {
    const heading = windHeading(data, index);
    return heading === null || index === 0 || index === 35 ? "" : `<span style="left:${arrowPosition(index)}" data-oq-wind-hour="${index}" data-calm="${data.wind_mps[index] < 1}">${windArrow(heading)}</span>`;
  }).join("");
  return `<div class="oq-weather-wind-overlay" aria-hidden="true">${arrows}<span data-oq-weather-selected-wind hidden>${windArrow(0)}</span></div>`;
}

function roundedStep(target) {
  const magnitude = 10 ** Math.floor(Math.log10(target));
  return magnitude * [1, 2, 4, 5, 10].find((step) => step * magnitude >= target);
}

export function getWeatherChartModel(values, metric = "temperature", width = 480) {
  const finite = values.filter(Number.isFinite);
  const minimum = metric === "temperature" ? Math.min(...finite) - 1 : 0;
  const peak = Math.max(0, ...finite);
  const maximum = metric === "temperature" ? Math.max(...finite) + 1
    : metric === "wind" ? Math.max(5, peak / 0.76) : Math.max(100, peak);
  const step = roundedStep(Math.max(metric === "temperature" ? 1 : 0, (maximum - minimum) / 3));
  const bottom = Math.floor(minimum / step), top = Math.ceil(maximum / step);
  const low = bottom * step, upper = top * step;
  const height = 180;
  return { width, height, low, upper, ticks: Array.from({ length: top - bottom + 1 }, (_, i) => (top - i) * step),
    x: (index) => 42 + index / 35 * (width - 58),
    y: (value) => 18 + (upper - value) / (upper - low) * (height - 58),
  };
}

export function weatherChartPaths(values, model) {
  const lines = [], fills = [];
  const point = (i, value) => `${model.x(i).toFixed(2)} ${model.y(value).toFixed(2)}`;
  const slope = (i) => {
    const left = values[i - 1], right = values[i + 1], value = values[i];
    if (!Number.isFinite(left)) return Number.isFinite(right) ? right - value : 0;
    if (!Number.isFinite(right)) return value - left;
    const a = value - left, b = right - value;
    return a * b > 0 ? 2 * a * b / (a + b) : 0;
  };
  let start;
  let path = "";
  for (let i = 0; i < values.length; i++) {
    const value = values[i];
    if (!Number.isFinite(value)) continue;
    if (!Number.isFinite(values[i - 1])) {
      start = i;
      path = `M${point(i, value)}`;
    } else {
      path += `C${point(i - 2 / 3, values[i - 1] + slope(i - 1) / 3)} ${point(i - 1 / 3, value - slope(i) / 3)} ${point(i, value)}`;
    }
    if (!Number.isFinite(values[i + 1])) {
      lines.push(start === i ? `${path}h0` : path);
      if (start !== i) fills.push(`${path}L${point(i, 0)}L${point(start, 0)}Z`);
    }
  }
  return { line: lines.join(""), fill: fills.join("") };
}

function chart(data, metric, hours, showWindDirection) {
  const { field, label, unit } = charts[metric];
  const values = data[field];
  const model = getWeatherChartModel(values, metric);
  const paths = weatherChartPaths(values, model);
  const bottom = model.height - 40;
  const wind = metric === "wind" && showWindDirection;
  const title = t(wind ? "weather.graphWindWithDirection" : label);
  return `<figure class="oq-weather-chart" data-oq-weather-chart="${metric}"><figcaption>${escapeHtml(title)} <span>${unit}</span></figcaption>
    <div class="oq-weather-plot"><div class="oq-weather-y" aria-hidden="true">${model.ticks.map(v => `<span style="top:${(model.upper - v) / (model.upper - model.low) * 100}%">${escapeHtml(format(v))}</span>`).join("")}</div>
    <div class="oq-weather-plane"><svg viewBox="42 18 ${model.width - 58} ${model.height - 58}" preserveAspectRatio="none" tabindex="0" role="slider" aria-label="${escapeHtml(title)}" aria-valuemin="0" aria-valuemax="35" aria-valuenow="0" aria-valuetext="${escapeHtml(t("weather.graphInspect"))}" data-oq-weather-graph="${metric}" data-oq-focus-key="weather-${metric}">
    ${model.ticks.map(v => `<line x1="42" x2="${model.width - 16}" y1="${model.y(v)}" y2="${model.y(v)}"/>`).join("")}
    ${hours.ticks.map(({ index, midnight }) => `<line${midnight ? ' class="oq-weather-day-line"' : ""} x1="${model.x(index)}" x2="${model.x(index)}" y1="18" y2="${bottom}"/>`).join("")}
    ${metric === "solar" ? `<path class="oq-weather-solar-fill" d="${paths.fill}"/>` : ""}
    <path class="oq-weather-chart-line" d="${paths.line}"/>
    <g data-oq-weather-cursor hidden><line class="oq-weather-cursor-line" x1="0" x2="0" y1="18" y2="${bottom}"/><ellipse class="oq-weather-cursor-dot" cx="0" cy="0" rx="4" ry="4"/></g>
    </svg>${wind ? windPlotArrows(data, hours.ticks) : ""}</div>
    <div class="oq-weather-x" aria-hidden="true">
    ${hours.ticks.map(({ index, label }) => `<span style="left:${position(index)}">${label}</span>`).join("")}
    ${hours.days.map(({ index, label }) => `<strong style="left:${position(index)}">${escapeHtml(label)}</strong>`).join("")}
    </div></div></figure>`;
}

export function renderWeatherForecast() {
  const data = state.weatherForecast;
  const enabled = getEntityValue("weatherEnabled") === true;
  const current = weatherForecastCanDisplay(data);
  const solar = current && data.solar_wm2?.some(Number.isFinite);
  const direction = current && data.wind_direction_deg?.some((value, i) => Number.isFinite(value) && data.wind_mps[i] > 0);
  const hours = current ? chartHours(data.start_epoch) : null;
  const emptyKey = !enabled ? "weather.graphDisabled" : state.weatherForecastError === "unsupported" ? "weather.graphUnsupported"
    : state.weatherForecastError ? "weather.graphFailed" : "weather.graphWaiting";
  const body = !current ? `<p class="oq-weather-chart-note">${escapeHtml(t(emptyKey))}</p>` : `
    <div class="oq-weather-charts${solar ? " oq-weather-charts--solar" : ""}">${chart(data, "temperature", hours)}${chart(data, "wind", hours, direction)}${solar ? chart(data, "solar", hours) : ""}
    <div class="oq-weather-legend">
      <span><i class="oq-weather-key-selection" aria-hidden="true"></i>${escapeHtml(t("weather.graphSelection"))}</span>
      ${direction ? `<span>${windArrow(270)}${escapeHtml(t("weather.windArrowLegend"))}</span>` : ""}
    </div></div>
    <div class="oq-weather-footer"><p class="oq-weather-inspect" data-oq-weather-inspect role="status" aria-live="polite">${escapeHtml(t("weather.graphInspect"))}</p>
    <p class="oq-weather-chart-note">${escapeHtml(weatherCurrentModelIsCurrent(data)
      ? t("weather.modelDewPoint", { value: format(data.current.dew_point_c), time: time(data.current.time_epoch) })
      : t("weather.modelDewPointUnavailable"))}</p></div>
    <details class="oq-weather-hourly"${state.weatherForecastHourlyOpen ? " open" : ""}><summary data-oq-action="toggle-weather-hourly">${escapeHtml(t("weather.hourlyValues"))}</summary>
      ${solar ? `<p class="oq-weather-chart-note">${escapeHtml(t("weather.graphSolarMean"))}</p>` : ""}
      <div class="oq-weather-table-wrap"><table><thead><tr><th scope="col">${escapeHtml(t("weather.hourlyTime"))}</th>
        <th scope="col">${escapeHtml(t("weather.graphTemperature"))} (°C)</th><th scope="col">${escapeHtml(t("weather.graphWind"))} (m/s)</th>
        ${direction ? `<th scope="col">${escapeHtml(t("weather.windDirection"))}</th>` : ""}
        ${solar ? `<th scope="col">${escapeHtml(t("weather.graphSolar"))} (W/m²)</th>` : ""}</tr></thead>
      <tbody>${data.temperature_c.map((value, i) => `<tr><th scope="row">${escapeHtml(time(data.start_epoch + i * 3600))}</th>
        <td>${escapeHtml(format(value))}</td><td>${escapeHtml(format(data.wind_mps[i]))}</td>
        ${direction ? `<td>${escapeHtml(windDirection(windHeading(data, i)) || "—")}</td>` : ""}
        ${solar ? `<td>${data.solar_wm2[i] === null ? "—" : escapeHtml(format(data.solar_wm2[i]))}</td>` : ""}</tr>`).join("")}</tbody></table></div>
    </details>`;
  return `<div class="oq-weather-forecast"><header class="oq-weather-header"><div><h4>${escapeHtml(t("weather.graphTitle"))}</h4><p class="oq-weather-chart-note">${escapeHtml(t("weather.graphCopy"))}</p></div>${current ? `<div class="oq-weather-meta"><strong title="${escapeHtml(data.location)}">${escapeHtml(data.configured_location || data.location)}</strong><span>${escapeHtml(t("weather.graphFetched", { time: formatDateTime(data.fetched_epoch * 1000, { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" }) }))}</span></div>` : ""}</header>${body}</div>`;
}

export function patchWeatherForecast() {
  const node = state.root?.querySelector("[data-oq-weather-forecast]");
  if (!node) return false;
  const markup = renderWeatherForecast();
  // SVG self-closing tags are normalized by innerHTML. Compare our previous
  // render instead so an unchanged poll keeps the live disclosure and focus.
  if (renderedMarkup.get(node) === markup) { patchWeatherForecastCursor(); return true; }
  const wasOpen = node.querySelector("details")?.open ?? state.weatherForecastHourlyOpen;
  state.weatherForecastHourlyOpen = wasOpen;
  const focusedSummary = node.querySelector("summary") === document.activeElement;
  const focusedGraph = document.activeElement?.dataset?.oqWeatherGraph;
  const scrollLeft = node.querySelector(".oq-weather-table-wrap")?.scrollLeft || 0;
  const scrollTop = node.querySelector(".oq-weather-table-wrap")?.scrollTop || 0;
  node.innerHTML = markup;
  renderedMarkup.set(node, markup);
  const details = node.querySelector("details");
  if (details) details.open = Boolean(wasOpen);
  const table = node.querySelector(".oq-weather-table-wrap");
  if (table) { table.scrollLeft = scrollLeft; table.scrollTop = scrollTop; }
  if (focusedSummary) node.querySelector("summary")?.focus({ preventScroll: true });
  if (Object.hasOwn(charts, focusedGraph)) node.querySelector(`[data-oq-weather-graph="${focusedGraph}"]`)?.focus({ preventScroll: true });
  patchWeatherForecastCursor();
  return true;
}

export function getWeatherPointIndex(svgX, width) {
  return Math.max(0, Math.min(35, Math.round((svgX - 42) / (width - 58) * 35)));
}

function patchWindArrow(figure, index, heading, scale) {
  const marker = figure.querySelector("[data-oq-weather-selected-wind]");
  if (!marker) return;
  marker.hidden = heading === null;
  if (heading !== null) {
    marker.style.left = arrowPosition(index);
    marker.querySelector("svg").style.setProperty("--oq-wind-angle", `${(heading + 180) % 360}deg`);
  }
  for (const arrow of figure.querySelectorAll("[data-oq-wind-hour]")) {
    const distance = Math.abs(Number(arrow.dataset.oqWindHour) - index);
    arrow.hidden = heading !== null && distance * scale < 14;
  }
}

export function patchWeatherForecastCursor() {
  const target = state.root?.querySelector?.("[data-oq-weather-forecast]");
  if (resizeObserver && target !== resizeTarget) {
    resizeObserver.disconnect();
    if (target) resizeObserver.observe(target);
    resizeTarget = target;
  }
  const data = state.weatherForecast;
  const index = state.weatherForecastIndex;
  const selected = Number.isInteger(index) && index >= 0 && index < 36 && weatherForecastCanDisplay(data);
  const heading = selected ? windHeading(data, index) : null;
  let label = t("weather.graphInspect");
  let readoutHtml = escapeHtml(label);
  if (selected) {
    label = t("weather.graphPoint", { time: time(data.start_epoch + index * 3600), temperature: format(data.temperature_c[index]), wind: format(data.wind_mps[index]) });
    const direction = windDirection(heading, true), solar = data.solar_wm2?.[index];
    const from = direction ? ` ${t("weather.windFrom", { direction })}` : "";
    const light = Number.isFinite(solar) ? ` · ${t("weather.graphSolar")} ${format(solar)} W/m²` : "";
    readoutHtml = escapeHtml(label) + (direction ? windArrow(heading) : "") + escapeHtml(from + light);
    label += from + light;
  }
  for (const figure of state.root?.querySelectorAll?.("[data-oq-weather-chart]") || []) {
    const svg = figure.querySelector("svg"), cursor = figure.querySelector("[data-oq-weather-cursor]");
    if (!svg || !cursor) continue;
    svg.setAttribute("aria-valuenow", String(selected ? index : 0));
    svg.setAttribute("aria-valuetext", label);
    const metric = figure.dataset.oqWeatherChart;
    const plotWidth = svg.viewBox.baseVal.width;
    const matrix = svg.getScreenCTM();
    if (metric === "wind") patchWindArrow(figure, index, heading, plotWidth * (matrix?.a || 1) / 35);
    const values = data?.[charts[metric].field];
    if (!selected || !values?.length) { cursor.setAttribute("hidden", ""); continue; }
    const model = getWeatherChartModel(values, metric, plotWidth + 58);
    cursor.removeAttribute("hidden");
    cursor.setAttribute("transform", `translate(${model.x(index).toFixed(1)} 0)`);
    const dot = cursor.querySelector("ellipse");
    dot.setAttribute("visibility", Number.isFinite(values[index]) ? "visible" : "hidden");
    if (Number.isFinite(values[index])) dot.setAttribute("cy", model.y(values[index]).toFixed(1));
    dot.setAttribute("rx", 4 / (matrix?.a || 1));
    dot.setAttribute("ry", 4 / (matrix?.d || 1));
  }
  const readout = target?.querySelector("[data-oq-weather-inspect]");
  if (readout && renderedReadout.get(readout) !== readoutHtml) {
    readout.innerHTML = readoutHtml;
    renderedReadout.set(readout, readoutHtml);
  }
}

export function handleWeatherChartPointer(event) {
  const figure = event.target?.closest?.("[data-oq-weather-chart]");
  if (!figure || !weatherForecastCanDisplay(state.weatherForecast)) return false;
  const svg = figure.querySelector("svg"), matrix = svg?.getScreenCTM();
  if (!matrix || !(matrix.a > 0) || !Number.isFinite(event.clientX)) return false;
  // The cropped SVG uses plot coordinates; its matrix includes the axis offset.
  const index = getWeatherPointIndex((event.clientX - matrix.e) / matrix.a, svg.viewBox.baseVal.width + 58);
  state.weatherForecastIndex = index;
  patchWeatherForecastCursor();
  return true;
}

export function handleWeatherChartFocus(event) {
  if (event.type !== "focusin" || !event.target?.dataset?.oqWeatherGraph || !weatherForecastCanDisplay(state.weatherForecast)) return;
  if (state.weatherForecastIndex === null) state.weatherForecastIndex = 0;
  patchWeatherForecastCursor();
}

export function handleWeatherChartKeyDown(event) {
  if (!event.target?.dataset?.oqWeatherGraph || !weatherForecastCanDisplay(state.weatherForecast)) return false;
  let index = state.weatherForecastIndex ?? 0;
  if (event.key === "ArrowLeft") index--;
  else if (event.key === "ArrowRight") index++;
  else if (event.key === "Home") index = 0;
  else if (event.key === "End") index = 35;
  else if (event.key === "Escape") index = null;
  else return false;
  event.preventDefault();
  state.weatherForecastIndex = index === null ? null : Math.max(0, Math.min(35, index));
  patchWeatherForecastCursor();
  return true;
}
