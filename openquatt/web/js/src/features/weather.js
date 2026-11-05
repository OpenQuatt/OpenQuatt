import { invalidateWeatherForecast, refreshWeatherForecast } from "./weather-forecast.js";
import { state } from "../core/state.js";
import { render } from "../core/render-scheduler.js";
import { saveWeatherLocation } from "../core/weather-location.js";
import { t } from "../i18n/index.js";
import { hasEntity } from "../core/entity-store.js";

export async function commitWeatherLocation(value) {
  if (state.busyAction) return false;
  if (!hasEntity("weatherLocation") || !hasEntity("weatherEnabled")) {
    state.controlError = t("weather.unavailable");
    render();
    return false;
  }
  state.weatherConfigRevision += 1;
  invalidateWeatherForecast();
  state.busyAction = "save-weatherLocation";
  state.controlNotice = state.controlError = "";
  const submitted = String(value ?? "");
  state.inputDrafts.weatherLocation = submitted;
  render();
  try {
    const { normalized, payload } = await saveWeatherLocation(value);
    state.entities.weatherLocation = { ...(state.entities.weatherLocation || {}), ...payload, value: normalized, state: normalized };
    const newerDraft = state.inputDrafts.weatherLocation !== submitted;
    if (state.drafts.weatherLocation === normalized) delete state.drafts.weatherLocation;
    if (!newerDraft) delete state.inputDrafts.weatherLocation;
    state.controlNotice = t(newerDraft ? "weather.locationSavedWithDraft" : "weather.locationSaved");
    return true;
  } catch (error) {
    state.controlError = t("weather.saveFailed", { error: error.message });
    return false;
  } finally {
    state.busyAction = "";
    render();
    void refreshWeatherForecast({ force: true });
  }
}
