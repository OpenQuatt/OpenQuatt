import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { getEntityValue } from "../core/entity-store.js";
import { weatherForecastCanDisplay, weatherCurrentModelIsCurrent } from "../core/weather-forecast.js";
import { formatDateTime, formatNumber, t } from "../i18n/index.js";

const renderedMarkup = new WeakMap();
let resizeTarget;
const resizeObserver = typeof ResizeObserver === "function" ? new ResizeObserver(() => patchWeatherForecastCursor()) : null;
const format = (value) => formatNumber(value, { maximumFractionDigits: 1 });
const time = (epoch) => formatDateTime(epoch * 1000, { weekday: "short", hour: "2-digit", minute: "2-digit" });

export function getWeatherChartModel(values, zeroFloor = false, width = 480) {
  const low = zeroFloor ? 0 : Math.floor(Math.min(...values) - 1);
  const high = Math.max(low + 2, Math.ceil(Math.max(...values) + 1));
  const step = Math.max(1, Math.ceil((high - low) / 4));
  const upper = low + step * 4;
  const height = 180;
  return { width, height, low, upper, step,
    x: (index) => 42 + index / 35 * (width - 58),
    y: (value) => 18 + (upper - value) / (upper - low) * (height - 58),
  };
}

function chart(data, values, label, unit, wind) {
  const model = getWeatherChartModel(values, wind, typeof window !== "undefined" && window.innerWidth < 640 ? 320 : 480);
  const path = values.map((value, i) => `${i ? "L" : "M"}${model.x(i).toFixed(1)} ${model.y(value).toFixed(1)}`).join(" ");
  const ticks = [0, 18, 35];
  const title = t(label);
  const metric = wind ? "wind" : "temperature";
  return `<figure class="oq-weather-chart" data-oq-weather-chart="${metric}"><figcaption>${escapeHtml(title)} <span>${unit}</span></figcaption><div class="oq-weather-plot"><div class="oq-weather-y" aria-hidden="true">${[4, 3, 2, 1, 0].map(i => `<span>${escapeHtml(format(model.low + model.step * i))}</span>`).join("")}</div><svg viewBox="42 18 ${model.width - 58} ${model.height - 58}" preserveAspectRatio="none" tabindex="0" role="slider" aria-label="${escapeHtml(title)}" aria-valuemin="0" aria-valuemax="35" aria-valuenow="0" aria-valuetext="${escapeHtml(t("weather.graphInspect"))}" data-oq-weather-graph="${metric}" data-oq-focus-key="weather-${metric}">
    ${[0, 1, 2, 3, 4].map((i) => {
      const value = model.low + model.step * i, y = model.y(value).toFixed(1);
      return `<line x1="42" x2="${model.width - 16}" y1="${y}" y2="${y}"/>`;
    }).join("")}
    <path class="oq-weather-chart-line ${wind ? "oq-weather-chart-line--wind" : "oq-weather-chart-line--temperature"}" d="${path}"/>
    <g data-oq-weather-cursor hidden><line class="oq-weather-cursor-line" x1="0" x2="0" y1="18" y2="${model.height - 40}"/><ellipse class="oq-weather-cursor-dot" cx="0" cy="0" rx="4" ry="4"/></g>
    </svg><div class="oq-weather-x" aria-hidden="true">${ticks.map(i => `<span>${escapeHtml(time(data.start_epoch + i * 3600))}</span>`).join("")}</div></div></figure>`;
}

export function renderWeatherForecast() {
  const data = state.weatherForecast;
  const enabled = getEntityValue("weatherEnabled") === true;
  const current = weatherForecastCanDisplay(data);
  const emptyKey = !enabled ? "weather.graphDisabled" : state.weatherForecastError === "unsupported" ? "weather.graphUnsupported"
    : state.weatherForecastError ? "weather.graphFailed" : "weather.graphWaiting";
  const body = !current ? `<p class="oq-weather-chart-note">${escapeHtml(t(emptyKey))}</p>` : `
    <div class="oq-weather-charts">${chart(data, data.temperature_c, "weather.graphTemperature", "°C", false)}${Array.isArray(data.wind_mps) ? chart(data, data.wind_mps, "weather.graphWind", "m/s", true) : ""}</div>
    <div class="oq-weather-footer"><p class="oq-weather-inspect" data-oq-weather-inspect role="status" aria-live="polite">${escapeHtml(t("weather.graphInspect"))}</p>
    <p class="oq-weather-chart-note">${escapeHtml(weatherCurrentModelIsCurrent(data)
      ? t("weather.modelDewPoint", { value: format(data.current.dew_point_c), time: time(data.current.time_epoch) })
      : t("weather.modelDewPointUnavailable"))}</p></div>
    <details class="oq-weather-hourly"${state.weatherForecastHourlyOpen ? " open" : ""}><summary data-oq-action="toggle-weather-hourly">${escapeHtml(t("weather.hourlyValues"))}</summary><div class="oq-weather-table-wrap"><table><thead><tr><th scope="col">${escapeHtml(t("weather.hourlyTime"))}</th><th scope="col">${escapeHtml(t("weather.graphTemperature"))} (°C)</th><th scope="col">${escapeHtml(t("weather.graphWind"))} (m/s)</th></tr></thead><tbody>${data.temperature_c.map((value, i) => `<tr><th scope="row">${escapeHtml(time(data.start_epoch + i * 3600))}</th><td>${escapeHtml(format(value))}</td><td>${Array.isArray(data.wind_mps) ? escapeHtml(format(data.wind_mps[i])) : "—"}</td></tr>`).join("")}</tbody></table></div></details>`;
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
  if (["temperature", "wind"].includes(focusedGraph)) node.querySelector(`[data-oq-weather-graph="${focusedGraph}"]`)?.focus({ preventScroll: true });
  patchWeatherForecastCursor();
  return true;
}

export function getWeatherPointIndex(svgX, width) {
  return Math.max(0, Math.min(35, Math.round((svgX - 42) / (width - 58) * 35)));
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
  const label = selected ? t("weather.graphPoint", { time: time(data.start_epoch + index * 3600), temperature: format(data.temperature_c[index]), wind: format(data.wind_mps[index]) })
    : t("weather.graphInspect");
  for (const figure of state.root?.querySelectorAll?.("[data-oq-weather-chart]") || []) {
    const svg = figure.querySelector("svg"), cursor = figure.querySelector("[data-oq-weather-cursor]");
    if (!svg || !cursor) continue;
    svg.setAttribute("aria-valuenow", String(selected ? index : 0));
    svg.setAttribute("aria-valuetext", label);
    if (!selected) { cursor.setAttribute("hidden", ""); continue; }
    const wind = figure.dataset.oqWeatherChart === "wind";
    const values = wind ? data.wind_mps : data.temperature_c;
    if (!Array.isArray(values)) { cursor.setAttribute("hidden", ""); continue; }
    const model = getWeatherChartModel(values, wind, svg.viewBox.baseVal.width + 58);
    cursor.removeAttribute("hidden");
    cursor.setAttribute("transform", `translate(${model.x(index).toFixed(1)} 0)`);
    const dot = cursor.querySelector("ellipse"), matrix = svg.getScreenCTM();
    dot.setAttribute("cy", model.y(values[index]).toFixed(1));
    dot.setAttribute("rx", 4 / (matrix?.a || 1));
    dot.setAttribute("ry", 4 / (matrix?.d || 1));
  }
  const readout = state.root?.querySelector?.("[data-oq-weather-inspect]");
  if (readout && readout.textContent !== label) readout.textContent = label;
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
