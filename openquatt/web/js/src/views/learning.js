import { hasEntity } from "../core/app-shared.js";
import { escapeHtml } from "../core/html.js";
import { setViewPatchControls } from "../core/view-patch-controls.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { patchHouseLearningSettingsStatus, renderHouseLearningSettings, renderHouseLearningStatusMarkup } from "../settings/house-learning.js";

export function renderLearningView() {
  const panel = renderHouseLearningSettings();
  const loading = state.loadingEntities || (hasEntity("houseLearningEnabled") && !state.houseLearningEndpointChecked);
  const statusError = state.houseLearningStatusError ? renderHouseLearningStatusMarkup() : "";
  return `<section class="oq-helper-panel">
    <h2 class="oq-helper-section-title">${escapeHtml(t("navigation.learning"))}</h2>
    <p class="oq-helper-section-copy">${escapeHtml(t("learning.copy"))}</p>
    ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
    ${state.controlNotice ? `<p class="oq-helper-notice" role="status">${escapeHtml(state.controlNotice)}</p>` : ""}
    ${panel || statusError || `<p class="oq-settings-action-note" role="status">${escapeHtml(t(loading ? "common.loading" : "learning.unavailable"))}</p>`}
  </section>`;
}

setViewPatchControls({ patchLearningDom: patchHouseLearningSettingsStatus });
