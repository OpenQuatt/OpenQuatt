import { hasWeatherSettings, patchWeatherOutlook, patchWeatherSettings, renderWeatherOutlook, renderWeatherSettings } from "../settings/weather.js";
import { hasEntity } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { escapeHtml } from "../core/html.js";
import { getLearningPanelHref } from "../core/navigation.js";
import { setViewPatchControls } from "../core/view-patch-controls.js";
import { renderSettingsFeedback, syncSettingsFeedback } from "../features/settings-feedback.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { patchHouseLearningSettingsStatus, renderHouseLearningSettings, renderHouseLearningStatusMarkup } from "../settings/house-learning.js";
import { renderLearningExplanation } from "./learning-explanation.js";

// Messages appear as toasts, like in Settings; a patch picks up a message that arrives between renders.
export function patchLearningView() {
  if (!state.root?.querySelector("[data-oq-feedback-stack]")) return false;
  syncSettingsFeedback(state.root);
  const tabs = state.root?.querySelector("[data-oq-learning-panels]");
  const weather = hasWeatherSettings();
  if (Boolean(tabs) !== weather) return false;
  if (state.learningPanel === "explanation") return true;
  if (weather && state.learningPanel === "forecast") return patchWeatherOutlook();
  if (state.learningPanel === "forecast") return false;
  if (weather && state.learningPanel === "configuration" && !patchWeatherSettings()) return false;
  return hasEntity("houseLearningEnabled") ? patchHouseLearningSettingsStatus() : true;
}

function renderLearningPanelLink(id, label, icon, active) {
  return `<a class="oq-settings-group-button oq-learning-link${active ? " is-active" : ""}" href="${escapeHtml(getLearningPanelHref(id))}" data-oq-action="select-learning-panel" data-learning-panel="${id}"${active ? ' aria-current="page"' : ""}>${renderOqIcon(icon, "oq-settings-group-button-icon")}<span class="oq-settings-group-button-label">${escapeHtml(label)}</span></a>`;
}

function renderLearningDiagnostics() {
  const downloads = renderHouseLearningSettings("export");
  const data = downloads ? `<section class="oq-settings-section">
    <h4 class="oq-learning-diagnostic-title">${escapeHtml(t("learningWorkspace.downloads"))}</h4>
    ${downloads}
  </section>` : "";
  return renderHouseLearningSettings("diagnostics") + data;
}

// Same layout as Settings: the panels sit in the side navigation, the content beside it.
export function renderLearningView() {
  const weather = hasWeatherSettings();
  const selected = state.learningPanel;
  const explanation = selected === "explanation";
  const forecast = selected === "forecast";
  const configuration = selected === "configuration";
  const diagnostics = selected === "diagnostics";
  const title = explanation ? t("learningExplanation.title")
    : forecast ? t("learningWorkspace.forecastTab")
    : configuration ? t("navigation.settings")
    : diagnostics ? t("learningWorkspace.diagnosticsTab")
    : t("houseLearning.panel.title");
  const panel = explanation ? renderLearningExplanation()
    : forecast ? (weather ? renderWeatherOutlook() : "")
    : configuration ? renderHouseLearningSettings("configuration") + renderWeatherSettings()
    : diagnostics ? renderLearningDiagnostics()
    : renderHouseLearningSettings();
  const loading = state.loadingEntities || (hasEntity("houseLearningEnabled") && !state.houseLearningEndpointChecked);
  const statusError = forecast && !weather
    ? `<p class="oq-settings-action-note" role="status">${escapeHtml(t(state.loadingEntities ? "common.loading" : "weather.unavailable"))}</p>`
    : state.houseLearningStatusError ? renderHouseLearningStatusMarkup() : "";
  return `<section class="oq-settings-view oq-learning-view">
    <aside class="oq-settings-view-aside">
      <nav class="oq-settings-group-nav" aria-label="${escapeHtml(t("learningWorkspace.navLabel"))}"${weather ? " data-oq-learning-panels" : ""}>
        ${renderLearningPanelLink("explanation", t("learningExplanation.tab"), "info", explanation)}
        ${renderLearningPanelLink("measurements", t("navigation.learning"), "home-cog", selected === "measurements")}
        ${weather || forecast ? renderLearningPanelLink("forecast", t("learningWorkspace.forecastTab"), "sun", forecast) : ""}
        ${renderLearningPanelLink("configuration", t("navigation.settings"), "settings", configuration)}
        ${renderLearningPanelLink("diagnostics", t("learningWorkspace.diagnosticsTab"), "activity", diagnostics)}
      </nav>
      <p class="oq-settings-view-note">${escapeHtml(t("learning.copy"))}</p>
    </aside>
    <div class="oq-settings-view-main">
      <header class="oq-settings-section-head oq-learning-page-head">
        <h3>${escapeHtml(title)}</h3>
        <span class="oq-settings-section-badge oq-settings-section-badge--experimental">${escapeHtml(t("houseLearning.panel.experimental"))}</span>
      </header>
      ${panel || statusError || `<p class="oq-settings-action-note" role="status">${escapeHtml(t(loading ? "common.loading" : "learning.unavailable"))}</p>`}
    </div>
    ${renderSettingsFeedback()}
  </section>`;
}

setViewPatchControls({ patchLearningDom: patchLearningView });
