import { hasWeatherSettings, patchWeatherSettings, renderWeatherSettings } from "../settings/weather.js";
import { hasEntity } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { escapeHtml } from "../core/html.js";
import { setViewPatchControls } from "../core/view-patch-controls.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { patchHouseLearningSettingsStatus, renderHouseLearningSettings, renderHouseLearningStatusMarkup } from "../settings/house-learning.js";

// No whitespace between the parts, so an empty message slot matches :empty and takes no space.
function renderControlMessages() {
  return `${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}${
    state.controlNotice ? `<p class="oq-helper-notice" role="status">${escapeHtml(state.controlNotice)}</p>` : ""}`;
}

export function patchLearningView() {
  const messages = state.root?.querySelector("[data-oq-learning-messages]");
  if (!messages) return false;
  const markup = renderControlMessages();
  if (messages.innerHTML !== markup) messages.innerHTML = markup;
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
      <div data-oq-learning-messages>${renderControlMessages()}</div>
      ${panel || statusError || `<p class="oq-settings-action-note" role="status">${escapeHtml(t(loading ? "common.loading" : "learning.unavailable"))}</p>`}
    </div>
  </section>`;
}

setViewPatchControls({ patchLearningDom: patchLearningView });
