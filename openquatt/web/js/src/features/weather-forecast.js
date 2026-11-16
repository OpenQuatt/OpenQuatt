import { patchForecastProgress } from "../settings/forecast-progress.js";
import { createCancellationController, fetchWithTimeout } from "../core/browser-utils.js";
import { getEntityValue, hasEntity } from "../core/entity-store.js";
import { state } from "../core/state.js";
import { getWeatherForecastEndpoint, normalizeWeatherForecast, weatherConsentBlocksForecast, weatherLocationReceipt } from "../core/weather-forecast.js";
import { patchWeatherForecast } from "../settings/weather-forecast.js";

let pending = null;
let controller = null;
let revision = 0;
let lastFetch = -Infinity;

export function shouldRefreshWeatherForecast() {
  return !document.hidden && !state.nativeOpen && state.appView === "learning"
    && state.learningPanel === "forecast" && hasEntity("weatherLocation") && hasEntity("weatherEnabled");
}

export function suspendWeatherForecast() {
  revision += 1;
  controller?.abort();
  lastFetch = -Infinity;
}

export function invalidateWeatherForecast() {
  suspendWeatherForecast();
  state.weatherForecast = null;
  state.weatherForecastError = "";
  state.weatherForecastIndex = null;
  patchWeatherForecast();
  patchForecastProgress();
}

export async function refreshWeatherForecast({ force = false } = {}) {
  if (weatherConsentBlocksForecast() || ["save-weatherLocation", "switch-weatherEnabled"].includes(state.busyAction) || getEntityValue("weatherEnabled") !== true) {
    if (pending || state.weatherForecast) invalidateWeatherForecast();
    return false;
  }
  if (!shouldRefreshWeatherForecast()) {
    if (pending) suspendWeatherForecast();
    return false;
  }
  if (pending) return pending;
  if (!force && performance.now() - lastFetch < 10000) return false;
  const requestRevision = revision;
  const startedAt = Date.now(), startedMono = performance.now();
  const location = String(getEntityValue("weatherLocation") ?? "");
  const locationReceipt = weatherLocationReceipt();
  const endpoint = getWeatherForecastEndpoint();
  const requestController = createCancellationController();
  let retainCached = true;
  controller = requestController;
  const stillCurrent = () => requestRevision === revision && shouldRefreshWeatherForecast() && !weatherConsentBlocksForecast()
    && !["save-weatherLocation", "switch-weatherEnabled"].includes(state.busyAction) && state.drafts.weatherEnabled !== false
    && getEntityValue("weatherEnabled") === true && String(getEntityValue("weatherLocation") ?? "") === location
    && weatherLocationReceipt() === locationReceipt
    && endpoint === getWeatherForecastEndpoint();
  pending = (async () => {
    try {
      const data = await fetchWithTimeout(endpoint, { cache: "no-store", signal: requestController.signal },
        8000, "weather-forecast-timeout", async (response) => {
          if (response.status === 404) return null;
          if (!response.ok) {
            retainCached = response.status === 408 || response.status === 429 || response.status >= 500;
            throw new Error(`HTTP ${response.status}`);
          }
          return normalizeWeatherForecast(await response.json(), startedAt, startedMono);
        });
      if (data === null) {
        if (stillCurrent()) { state.weatherForecast = null; state.weatherForecastError = "unsupported"; }
        return false;
      }
      if (!stillCurrent()) return false;
      if (data.configured_location !== location) {
        state.weatherForecast = null;
        state.weatherForecastError = "failed";
        return false;
      }
      data.locationReceipt = locationReceipt;
      data.endpoint = endpoint; // Request identity is local metadata, never wire input.
      state.weatherForecast = data;
      state.weatherForecastError = "";
      return true;
    } catch (_error) {
      if (!stillCurrent()) return false;
      // Keep the last validated snapshot on a transport/body interruption, but
      // never mask an authoritative invalid frame, unsupported endpoint, denied
      // access or a complete response that violates the data contract.
      if (!retainCached || ["weather-forecast-format", "weather-scenario-format"].includes(_error?.message)) state.weatherForecast = null;
      state.weatherForecastError = "failed";
      return false;
    } finally {
      if (controller === requestController) controller = null;
      if (requestRevision === revision) lastFetch = performance.now();
      patchWeatherForecast();
      patchForecastProgress();
    }
  })();
  try { return await pending; }
  finally { pending = null; }
}

// These listeners have the app lifetime; each request's deadline is cleared in
// finally. Navigation also invalidates any response from the previous surface.
globalThis.document?.addEventListener?.("visibilitychange", () => { if (document.hidden) suspendWeatherForecast(); });
globalThis.window?.addEventListener?.("pagehide", suspendWeatherForecast);
globalThis.window?.addEventListener?.("popstate", suspendWeatherForecast);
