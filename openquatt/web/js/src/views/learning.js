import { hasWeatherSettings, patchWeatherSettings, renderWeatherSettings } from "../settings/weather.js";
import { hasEntity } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { escapeHtml } from "../core/html.js";
import { setViewPatchControls } from "../core/view-patch-controls.js";
import { renderSettingsFeedback, syncSettingsFeedback } from "../features/settings-feedback.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { patchHouseLearningSettingsStatus, renderHouseLearningSettings, renderHouseLearningStatusMarkup } from "../settings/house-learning.js";

// Messages appear as toasts, like in Settings; a patch picks up a message that arrives between renders.
export function patchLearningView() {
  if (!state.root?.querySelector("[data-oq-feedback-stack]")) return false;
  syncSettingsFeedback(state.root);
  const tabs = state.root?.querySelector("[data-oq-learning-panels]");
  const weather = hasWeatherSettings();
  if (Boolean(tabs) !== weather) return false;
  return weather && state.learningForecastOpen ? patchWeatherSettings() : patchHouseLearningSettingsStatus();
}

function renderLearningPanelButton(id, label, icon, active) {
  return `<button class="oq-settings-group-button${active ? " is-active" : ""}" type="button" data-oq-action="select-learning-panel" data-learning-panel="${id}" aria-pressed="${active}">${renderOqIcon(icon, "oq-settings-group-button-icon")}<span class="oq-settings-group-button-label">${escapeHtml(label)}</span></button>`;
}

// Same layout as Settings: the panels sit in the side navigation, the content beside it.
export function renderLearningView() {
  const weather = hasWeatherSettings();
  const forecast = weather && state.learningForecastOpen;
  const panel = forecast ? renderWeatherSettings() : renderHouseLearningSettings();
  const loading = state.loadingEntities || (hasEntity("houseLearningEnabled") && !state.houseLearningEndpointChecked);
  const statusError = state.houseLearningStatusError ? renderHouseLearningStatusMarkup() : "";
  return `<section class="oq-settings-view oq-learning-view">
    <aside class="oq-settings-view-aside">
      <nav class="oq-settings-group-nav" aria-label="${escapeHtml(t("learningWorkspace.navLabel"))}"${weather ? " data-oq-learning-panels" : ""}>
        ${renderLearningPanelButton("measurements", t("navigation.learning"), "home-cog", !forecast)}
        ${weather ? renderLearningPanelButton("forecast", t("learningWorkspace.forecastTab"), "sun", forecast) : ""}
      </nav>
      <p class="oq-settings-view-note">${escapeHtml(t("learning.copy"))}</p>
    </aside>
    <div class="oq-settings-view-main">
      ${panel || statusError || `<p class="oq-settings-action-note" role="status">${escapeHtml(t(loading ? "common.loading" : "learning.unavailable"))}</p>`}
    </div>
    ${renderSettingsFeedback()}
  </section>`;
}

setViewPatchControls({ patchLearningDom: patchLearningView });
