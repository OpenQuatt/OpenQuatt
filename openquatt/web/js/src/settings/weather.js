import { getBinaryEntityState } from "../core/app-shared.js";
import { hasEntity } from "../core/entity-store.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { renderSettingsCompactSwitchControl } from "./controls.js";
import { getSettingsSwitchModel } from "./field-models.js";
import { patchForecastProgress, renderForecastProgress } from "./forecast-progress.js";
import { patchWeatherForecast, renderWeatherForecast } from "./weather-forecast.js";

export const hasWeatherSettings = () => hasEntity("weatherLocation") && hasEntity("weatherEnabled");

export function renderWeatherSettings() {
  if (!hasWeatherSettings()) return "";
  const enabled = getBinaryEntityState("weatherEnabled") === true;
  const busy = state.loadingEntities || Boolean(state.busyAction);
  return `<section class="oq-settings-section" data-oq-weather-settings>
    <div class="oq-settings-section-head"><h3>${escapeHtml(t("learningWorkspace.forecastTab"))}</h3></div>
    <div class="oq-settings-section-body">
      <div class="oq-settings-grid oq-weather-controls">
        <div><strong>${escapeHtml(t("weather.enableLabel"))}</strong><p>${escapeHtml(t("weather.enableCopy"))}</p>
          ${renderSettingsCompactSwitchControl("weatherEnabled", t("weather.enableLabel"), enabled, busy, t("common.on"), t("common.off"))}</div>
        <label><strong>${escapeHtml(t("weather.locationLabel"))}</strong><p>${escapeHtml(t("weather.locationCopy"))}</p>
          <input class="oq-helper-input" type="text" data-oq-field="weatherLocation" aria-label="${escapeHtml(t("weather.locationLabel"))}"
            value="${escapeHtml(String(getInputDraftValue("weatherLocation") ?? ""))}" placeholder="${escapeHtml(t("weather.locationPlaceholder"))}"
            maxlength="64" autocomplete="off" spellcheck="false" ${busy ? "disabled" : ""}></label>
      </div>
      <div data-oq-weather-diagnostics>${renderForecastProgress()}</div>
      <div data-oq-weather-forecast>${renderWeatherForecast()}</div>
    </div>
  </section>`;
}

export function patchWeatherSettings() {
  const panel = state.root?.querySelector("[data-oq-weather-settings]");
  if (!panel || !hasWeatherSettings()) return false;
  const input = panel.querySelector('[data-oq-field="weatherLocation"]');
  const toggle = panel.querySelector('[data-control-key="weatherEnabled"]');
  const pill = panel.querySelector('[data-oq-switch-pill="weatherEnabled"]');
  if (!input || !toggle || !pill) return false;
  const busy = state.loadingEntities || Boolean(state.busyAction);
  const model = getSettingsSwitchModel("weatherEnabled", { title: t("weather.enableLabel"),
    enabled: getBinaryEntityState("weatherEnabled") === true, busy });
  if (document.activeElement !== input) input.value = String(getInputDraftValue("weatherLocation") ?? "");
  input.disabled = toggle.disabled = busy;
  toggle.setAttribute("aria-checked", String(model.enabled));
  toggle.setAttribute("aria-label", model.ariaLabel);
  toggle.dataset.controlState = model.nextState;
  toggle.classList.toggle("is-on", model.enabled);
  pill.classList.toggle("is-on", model.enabled);
  pill.textContent = model.label;
  patchForecastProgress();
  patchWeatherForecast();
  return true;
}
