import { hasWeatherSettings, patchWeatherSettings, renderWeatherSettings } from "../settings/weather.js";
import { hasEntity } from "../core/app-shared.js";
import { escapeHtml } from "../core/html.js";
import { setViewPatchControls } from "../core/view-patch-controls.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { patchHouseLearningSettingsStatus, renderHouseLearningSettings, renderHouseLearningStatusMarkup } from "../settings/house-learning.js";

export function renderLearningView() {
  const weather = hasWeatherSettings();
  const forecast = weather && state.learningForecastOpen;
  const panel = forecast ? renderWeatherSettings() : renderHouseLearningSettings();
  const loading = state.loadingEntities || (hasEntity("houseLearningEnabled") && !state.houseLearningEndpointChecked);
  const statusError = state.houseLearningStatusError ? renderHouseLearningStatusMarkup() : "";
  return `<section class="oq-helper-panel">
    <h2 class="oq-helper-section-title">${escapeHtml(t("navigation.learning"))}</h2>
    <p class="oq-helper-section-copy">${escapeHtml(t("learning.copy"))}</p>
    ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
    ${state.controlNotice ? `<p class="oq-helper-notice" role="status">${escapeHtml(state.controlNotice)}</p>` : ""}
    ${weather ? `<div class="oq-learning-panels" data-oq-learning-panels>
      <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="select-learning-panel" data-learning-panel="measurements" aria-pressed="${!forecast}">${escapeHtml(t("navigation.learning"))}</button>
      <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="select-learning-panel" data-learning-panel="forecast" aria-pressed="${forecast}">${escapeHtml(t("learningWorkspace.forecastTab"))}</button>
    </div>` : ""}
    ${panel || statusError || `<p class="oq-settings-action-note" role="status">${escapeHtml(t(loading ? "common.loading" : "learning.unavailable"))}</p>`}
  </section>`;
}

setViewPatchControls({ patchLearningDom: () => {
  const tabs = state.root?.querySelector("[data-oq-learning-panels]");
  const weather = hasWeatherSettings();
  if (Boolean(tabs) !== weather) return false;
  return weather && state.learningForecastOpen ? patchWeatherSettings() : patchHouseLearningSettingsStatus();
} });
