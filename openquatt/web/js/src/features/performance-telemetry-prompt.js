import { renderOqIcon, ENTITY_DEFS } from "../core/config.js";
import { fetchWithTimeout } from "../core/browser-utils.js";
import { buildEntityPath } from "../core/domain-helpers.js";
import { getEntityValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { parsePerformanceTelemetryActiveValue, savePerformanceTelemetryPromptChoice, shouldOfferPerformanceTelemetryPrompt } from "../core/performance-telemetry-domain.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { renderPerformanceTelemetryDisclosure } from "./performance-telemetry.js";

const MODAL = "performance-telemetry-prompt";
const DEFER_KEY = "oq-performance-telemetry-prompt-deferred";
const KEYS = ["performanceTelemetryEnabled", "performanceTelemetryChoiceConfigured", "performanceTelemetryPromptHandled"];

function isDeferred() {
  if (state.performanceTelemetryPromptDeferred) return true;
  try { return window.sessionStorage.getItem(DEFER_KEY) === "true"; } catch { return false; }
}

export function syncPerformanceTelemetryPrompt() {
  const enabled = parsePerformanceTelemetryActiveValue(getEntityValue("performanceTelemetryEnabled"));
  const handled = parsePerformanceTelemetryActiveValue(getEntityValue("performanceTelemetryPromptHandled"));
  const blocked = state.loadingEntities || state.nativeOpen || state.updateModalOpen
    || state.deviceReconnectMode || state.ota?.wait || state.restartRefresh?.wait
    || (state.quickStartModalOpen && (state.complete !== true || state.quickStartModalMode === "generation"));
  if (state.systemModal === MODAL) {
    if (blocked || (!state.performanceTelemetryPromptBusy
      && (enabled === true || (!state.performanceTelemetryPromptError && handled === true)))) {
      state.systemModal = "";
    }
    return;
  }
  if (shouldOfferPerformanceTelemetryPrompt({
    setupComplete: state.complete,
    enabled,
    handled,
    deferred: isDeferred(),
    blocked: blocked || state.systemModal || state.busyAction,
  })) {
    state.performanceTelemetryPromptError = "";
    state.systemModal = MODAL;
  }
}

export function deferPerformanceTelemetryPrompt() {
  if (state.systemModal !== MODAL || state.performanceTelemetryPromptBusy) return;
  state.performanceTelemetryPromptDeferred = true;
  try { window.sessionStorage.setItem(DEFER_KEY, "true"); } catch { /* Memory fallback for restricted browsers. */ }
  state.systemModal = "";
  state.performanceTelemetryPromptError = "";
  render();
}

async function saveChoice(enabled) {
  if (state.systemModal !== MODAL || state.busyAction || state.performanceTelemetryPromptBusy) return;
  state.performanceTelemetryPromptBusy = true;
  state.performanceTelemetryPromptWriteRevision += 1;
  state.busyAction = "performance-telemetry-prompt";
  state.performanceTelemetryPromptError = "";
  render();
  const entity = ENTITY_DEFS.performanceTelemetryEnabled;
  try {
    const confirmed = await savePerformanceTelemetryPromptChoice({
      expectedEnabled: enabled,
      write: async () => {
        const response = await fetchWithTimeout(buildEntityPath(entity.domain, entity.name, enabled ? "turn_on" : "turn_off"),
          { method: "POST" }, 3000);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
      },
      read: async () => {
        const payloads = await Promise.all(KEYS.map(async (key) => {
          const definition = ENTITY_DEFS[key];
          return fetchWithTimeout(buildEntityPath(definition.domain, definition.name), { cache: "no-store" }, 1500, "", async (response) => {
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            return response.json();
          });
        }));
        KEYS.forEach((key, index) => { state.entities[key] = { ...state.entities[key], ...payloads[index] }; });
        return { enabled: getEntityValue(KEYS[0]), choice: getEntityValue(KEYS[1]), handled: getEntityValue(KEYS[2]) };
      },
    });
    if (confirmed) {
      if (state.systemModal === MODAL) state.systemModal = "";
      state.controlNotice = t(enabled ? "performance.promptThanks" : "performance.promptDeclined");
    } else {
      state.performanceTelemetryPromptError = t("performance.promptUnconfirmed");
    }
  } finally {
    state.performanceTelemetryPromptBusy = false;
    state.performanceTelemetryPromptWriteRevision += 1;
    if (state.busyAction === "performance-telemetry-prompt") state.busyAction = "";
    render();
  }
}

export function handlePerformanceTelemetryPromptAction(action) {
  if (action === "defer-performance-telemetry-prompt") { deferPerformanceTelemetryPrompt(); return true; }
  if (action === "enable-performance-telemetry-prompt") { void saveChoice(true); return true; }
  if (action === "decline-performance-telemetry-prompt") { void saveChoice(false); return true; }
  return false;
}

export function handlePerformanceTelemetryPromptKeyDown(event) {
  if (state.systemModal !== MODAL) return false;
  if (event.key === "Escape") {
    event.preventDefault();
    deferPerformanceTelemetryPrompt();
    return true;
  }
  if (event.key === "Tab") {
    const dialog = state.root?.querySelector('[data-oq-modal="system"] [role="dialog"]');
    const controls = dialog ? [...dialog.querySelectorAll('button:not(:disabled), summary, a[href], input:not(:disabled), select:not(:disabled)')].filter((el) => el.getClientRects().length) : [];
    if (!controls.length) { event.preventDefault(); dialog?.focus(); return true; }
    const first = controls[0];
    const last = controls[controls.length - 1];
    if (event.shiftKey && (event.target === first || event.target === dialog)) { event.preventDefault(); last.focus(); }
    else if (!event.shiftKey && event.target === last) { event.preventDefault(); first.focus(); }
    return true;
  }
  return false;
}

export function renderPerformanceTelemetryPrompt() {
  const busy = state.performanceTelemetryPromptBusy;
  return renderModalShell({
    modalId: "system",
    titleId: "oq-performance-prompt-title",
    kicker: t("performance.promptKicker"),
    title: t("performance.promptTitle"),
    copy: t("performance.promptCopy"),
    closeAction: "defer-performance-telemetry-prompt",
    closeLabel: t("performance.promptLater"),
    className: "oq-helper-modal--scrollable oq-performance-prompt",
    sectionAttributes: `aria-busy="${busy}"`,
    bodyMarkup: `
      <div class="oq-performance-prompt-invite">
        <span class="oq-performance-prompt-icon" aria-hidden="true">${renderOqIcon("activity")}</span>
        <div><h3>${escapeHtml(t("performance.promptBenefit"))}</h3><p>${escapeHtml(t("performance.promptBenefitCopy"))}</p></div>
      </div>
      <p class="oq-helper-modal-copy">${escapeHtml(t("performance.promptChoiceCopy"))}</p>
      ${renderPerformanceTelemetryDisclosure({ collapsible: true, idPrefix: "oq-performance-prompt", open: state.performanceTelemetryDetailsOpen })}
      ${state.performanceTelemetryPromptError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.performanceTelemetryPromptError)}</p>` : ""}
      ${busy ? `<p class="oq-helper-notice" role="status">${escapeHtml(t("performance.promptSaving"))}</p>` : ""}
    `,
    actions: `
      <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="enable-performance-telemetry-prompt" ${busy ? "disabled" : ""}>${escapeHtml(t("performance.promptEnable"))}</button>
      <button class="oq-helper-button" type="button" data-oq-action="decline-performance-telemetry-prompt" ${busy ? "disabled" : ""}>${escapeHtml(t("performance.promptDecline"))}</button>
      <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="defer-performance-telemetry-prompt" ${busy ? "disabled" : ""}>${escapeHtml(t("performance.promptLater"))}</button>
    `,
  });
}
