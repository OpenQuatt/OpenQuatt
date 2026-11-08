import { invalidateWeatherForecast, refreshWeatherForecast } from "./weather-forecast.js";
import { state } from "../core/state.js";
import { beginEntityWriteAction, isEntityWriteActionCurrent, ownsBusyAction, startEntityWriteAction } from "../core/entity-sync.js";
import { render } from "../core/render-scheduler.js";
import { saveWeatherLocation } from "../core/weather-location.js";
import { t } from "../i18n/index.js";
import { hasEntity } from "../core/entity-store.js";

export async function commitWeatherLocation(value) {
  if (state.busyAction) return false;
  if (!hasEntity("weatherLocation") || !hasEntity("weatherEnabled")) {
    beginEntityWriteAction();
    state.controlError = t("weather.unavailable");
    render();
    return false;
  }
  const weatherRevision = ++state.weatherConfigRevision;
  const busyAction = "save-weatherLocation";
  const actionRevision = startEntityWriteAction(busyAction);
  const isCurrent = () => isEntityWriteActionCurrent(actionRevision) && weatherRevision === state.weatherConfigRevision;
  invalidateWeatherForecast();
  state.controlNotice = state.controlError = "";
  const submitted = String(value ?? "");
  state.inputDrafts.weatherLocation = submitted;
  render();
  try {
    const { normalized, payload } = await saveWeatherLocation(value);
    if (!isCurrent()) return false;
    state.entities.weatherLocation = { ...(state.entities.weatherLocation || {}), ...payload, value: normalized, state: normalized };
    const newerDraft = state.inputDrafts.weatherLocation !== submitted;
    if (state.drafts.weatherLocation === normalized) delete state.drafts.weatherLocation;
    if (!newerDraft) delete state.inputDrafts.weatherLocation;
    state.controlNotice = t(newerDraft ? "weather.locationSavedWithDraft" : "weather.locationSaved");
    return true;
  } catch (error) {
    if (!isCurrent()) return false;
    state.controlError = t("weather.saveFailed", { error: error.message });
    return false;
  } finally {
    if (ownsBusyAction(actionRevision, busyAction)) state.busyAction = "";
    render();
    if (isCurrent()) void refreshWeatherForecast({ force: true });
  }
}
