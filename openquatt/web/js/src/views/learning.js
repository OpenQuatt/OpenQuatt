import { hasEntity } from "../core/app-shared.js";
import { escapeHtml } from "../core/html.js";
import { setViewPatchControls } from "../core/view-patch-controls.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { patchHouseLearningSettingsStatus, renderHouseLearningSettings, renderHouseLearningStatusMarkup } from "../settings/house-learning.js";

function renderControlMessages() {
  return `${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
    ${state.controlNotice ? `<p class="oq-helper-notice" role="status">${escapeHtml(state.controlNotice)}</p>` : ""}`;
}

export function patchLearningView() {
  const messages = state.root?.querySelector("[data-oq-learning-messages]");
  if (!messages) return false;
  const markup = renderControlMessages();
  if (messages.innerHTML !== markup) messages.innerHTML = markup;
  return patchHouseLearningSettingsStatus();
}

export function renderLearningView() {
  const panel = renderHouseLearningSettings();
  const loading = state.loadingEntities || (hasEntity("houseLearningEnabled") && !state.houseLearningEndpointChecked);
  const statusError = state.houseLearningStatusError ? renderHouseLearningStatusMarkup() : "";
  return `<section class="oq-helper-panel">
    <h2 class="oq-helper-section-title">${escapeHtml(t("navigation.learning"))}</h2>
    <p class="oq-helper-section-copy">${escapeHtml(t("learning.copy"))}</p>
    <div data-oq-learning-messages>${renderControlMessages()}</div>
    ${panel || statusError || `<p class="oq-settings-action-note" role="status">${escapeHtml(t(loading ? "common.loading" : "learning.unavailable"))}</p>`}
  </section>`;
}

setViewPatchControls({ patchLearningDom: patchLearningView });
