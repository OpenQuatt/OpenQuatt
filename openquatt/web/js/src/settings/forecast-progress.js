import { getEntityValue } from "../core/entity-store.js";
import { getBinaryEntityState } from "../core/app-shared.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { formatDateTime, formatNumber, t } from "../i18n/index.js";
import { weatherForecastIsCurrent, weatherScenarioCanDisplay, weatherScenarioNow } from "../core/weather-forecast.js";

const renderedForecast = new WeakMap();
const HORIZONS = [1800, 3600, 10800];
const STATUS_KEYS = {
  disabled: "weather.status.disabled", paused: "weather.status.paused",
  location_required: "weather.status.locationRequired", location_saving: "weather.status.saving",
  resolving_location: "weather.status.resolving", waiting_for_clock: "weather.status.clock", clock_changed: "weather.status.clock",
  waiting_for_weather: "weather.status.waitingWeather", waiting_for_observation: "weather.status.waitingObservation",
  invalid_observation: "weather.status.waitingObservation", measurement_gap_or_clock_jump: "weather.status.waitingObservation",
  waiting_for_model: "weather.status.waitingModel", invalid_location: "weather.invalidLocation",
  stored_location_invalid: "weather.status.storedInvalid", location_persistence_failed: "weather.status.storageFailed",
  location_not_found: "weather.status.notFound", location_ambiguous: "weather.status.ambiguous",
  invalid_location_response: "weather.status.locationResponse", worker_allocation_failed: "weather.status.memory",
  psram_allocation_failed: "weather.status.memory", tls_allocation_failed: "weather.status.memory",
  invalid_forecast: "weather.status.invalidForecast", request_deadline: "weather.status.timeout", cancelled: "weather.status.paused",
  location_invalid: "weather.invalidLocation", clock_invalid: "weather.status.clock", weather_unavailable: "learningWorkspace.forecast.weatherNeeded",
  waiting_for_scenario: "learningWorkspace.forecast.newCalculation", observation_invalid: "weather.status.waitingObservation",
  model_unavailable: "weather.status.waitingModel", configuration_changed: "learningWorkspace.forecast.newCalculation",
  context_changed: "learningWorkspace.forecast.newCalculation", expired: "learningWorkspace.forecast.newCalculation", snapshot_stale: "learningWorkspace.forecast.newCalculation",
};
const NETWORK_STATES = ["dns_pending", "dns_queue_failed", "dns_failed", "request_size", "tls_connect_failed", "tls_write_failed", "invalid_http_response"];
const time = (epoch) => formatDateTime(epoch * 1000, { weekday: "short", hour: "2-digit", minute: "2-digit" });

// Every displayed horizon is part of one frozen controller calculation.
export function renderForecastProgress({ statusOnly = false } = {}) {
  const enabled = getBinaryEntityState("weatherEnabled") === true;
  const changing = ["save-weatherLocation", "switch-weatherEnabled"].includes(state.busyAction);
  const data = state.weatherForecast;
  const scenario = data?.scenario;
  const now = weatherScenarioNow(data);
  const issued = scenario?.issued_epoch;
  const canShow = enabled && !changing && weatherScenarioCanDisplay(data);
  const issuedValid = Number.isFinite(issued) && issued > 0 && issued <= now;
  const current = weatherForecastIsCurrent(data);
  const entityStatus = String(getEntityValue("weatherStatus") || "");
  const status = statusOnly ? entityStatus : scenario?.valid ? "ready" : scenario?.reason || entityStatus;
  const horizons = HORIZONS.map((seconds, index) => {
    const target = issued + seconds;
    const value = scenario?.temperature_c[index];
    return { seconds, target, value, valid: canShow && issuedValid && scenario.horizon_valid[index] && target > now };
  });
  const available = horizons.some(({ valid }) => valid);
  const cached = available && !current;
  let gapKey = "";
  const consent = state.entities.weatherStatus?.consent_state;
  if (entityStatus === "psram_allocation_failed") gapKey = "weather.status.memory";
  else if (consent === "failed") gapKey = "weather.status.consentFailed";
  else if (consent === "pending") gapKey = "weather.status.consentSaving";
  else if (state.weatherOptOutUnconfirmed) gapKey = "weather.switchUnconfirmed";
  else if (!enabled) gapKey = "learningWorkspace.forecast.enable";
  else if (changing) gapKey = "weather.status.saving";
  else if (!String(getEntityValue("weatherLocation") || "").trim()) gapKey = "weather.status.locationRequired";
  else if (getEntityValue("houseLearningEnabled") !== true) gapKey = "weather.status.learningRequired";
  else if (statusOnly || !available) {
    if (!statusOnly && state.weatherForecastError === "unsupported") gapKey = "weather.graphUnsupported";
    else if (NETWORK_STATES.includes(status)) gapKey = "learningWorkspace.forecast.network";
    else if (Object.hasOwn(STATUS_KEYS, status)) gapKey = STATUS_KEYS[status];
    else gapKey = status && status !== "ready" && status !== "observing" ? "weather.status.unknown" : "learningWorkspace.forecast.newCalculation";
  }
  if (statusOnly) {
    return gapKey && gapKey !== "learningWorkspace.forecast.newCalculation"
      ? `<p class="oq-settings-action-note" role="status">${escapeHtml(t(gapKey))}</p>`
      : "";
  }
  const label = !enabled ? "weather.status.disabled" : cached ? "learningWorkspace.forecast.lastReceived" : available ? "learningWorkspace.forecast.provisional" : "learningWorkspace.forecast.waiting";
  const earlierWeather = available && scenario.weather_generation !== data.generation;
  return `<section class="oq-learning-workspace-scenario" data-oq-learning-scenario data-scenario-freshness="${available ? cached ? "cached" : "current" : "unavailable"}" data-quality="${available ? "provisional" : "unknown"}" aria-label="${escapeHtml(t("learningWorkspace.forecast.title"))}">
    <div class="oq-learning-workspace-result-head"><h4>${escapeHtml(t("learningWorkspace.forecast.title"))}</h4><span class="oq-learning-workspace-quality" data-quality="${available ? "provisional" : "unknown"}">${escapeHtml(t(label))}</span></div>
    ${cached ? `<p class="oq-learning-workspace-scenario-condition" role="status">${escapeHtml(t("learningWorkspace.forecast.cachedCopy"))}</p>` : ""}
    <div class="oq-learning-workspace-targets">${horizons.map(({ seconds, target, value, valid }) => `<article data-scenario-horizon="${seconds}">
      <h5>${escapeHtml(issuedValid ? time(target) : t("learningWorkspace.forecast.afterMinutes", { minutes: seconds / 60 }))}</h5>
      <strong>${escapeHtml(valid ? `${formatNumber(value, { minimumFractionDigits: 1, maximumFractionDigits: 2 })} °C` : "—")}</strong>
      ${valid ? `<p class="oq-learning-workspace-temperature-condition">${escapeHtml(t("learningWorkspace.forecast.condition"))}</p>` : ""}
      ${issuedValid && target <= now ? `<small>${escapeHtml(t("learningWorkspace.forecast.expired"))}</small>` : ""}
    </article>`).join("")}</div>
    ${available ? `<p class="oq-learning-workspace-issued">${escapeHtml(t("learningWorkspace.forecast.issued", { time: time(issued) }))}</p>` : ""}
    <div class="oq-learning-workspace-forecast-notes">
      ${available ? `<p class="oq-weather-chart-note">${escapeHtml(t("learningWorkspace.forecast.estimateExplanation"))}</p>` : ""}
      ${earlierWeather ? `<p class="oq-weather-chart-note">${escapeHtml(t("learningWorkspace.forecast.earlierWeather"))}</p>` : ""}
      ${gapKey ? `<p class="oq-weather-chart-note" role="status">${escapeHtml(t(gapKey))}</p>` : ""}
    </div>
  </section>`;
}

export function patchForecastProgress() {
  let patched = false;
  for (const [selector, render] of [["[data-oq-weather-diagnostics]", renderForecastProgress], ["[data-oq-weather-settings-status]", () => renderForecastProgress({ statusOnly: true })]]) {
    const node = state.root?.querySelector(selector);
    if (!node) continue;
    const markup = render();
    if (renderedForecast.get(node) !== markup) {
      node.innerHTML = markup;
      renderedForecast.set(node, markup);
    }
    patched = true;
  }
  return patched;
}
