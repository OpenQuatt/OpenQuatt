import { hasEntity } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { fetchWithTimeout } from "../core/browser-utils.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { formatNumber, t } from "../i18n/index.js";
import { getInstallationTopology } from "./device-context.js";
import { renderOduEditorAction, renderOduEditorModal, renderOduEditorPanel } from "./odu-editor-ui.js";

const STATE_KEYS = {
  IDLE: "oduDefrost.stateIdle", QUEUED: "oduDefrost.stateQueued", LOADING: "oduDefrost.stateLoading", LOADED: "oduDefrost.stateLoaded",
  CHECKING: "oduDefrost.stateChecking", WAITING: "oduDefrost.stateWaiting", ACTIVE: "oduDefrost.stateActive",
  ACCEPTED: "oduDefrost.stateAccepted", RESYNC: "oduDefrost.stateResync",
  COMPLETE: "oduDefrost.stateComplete", TIMEOUT: "oduDefrost.stateTimeout", SAFETY_STOP: "oduDefrost.stateSafetyStop",
  READ_FAILED: "oduDefrost.stateReadFailed", WRITE_FAILED: "oduDefrost.stateWriteFailed", WRITE_UNCERTAIN: "oduDefrost.stateWriteUncertain",
  SAVED: "oduDefrost.stateSaved", STALE: "oduDefrost.stateStale",
};
const GUARD_KEYS = {
  READY: "oduDefrost.guardReady", OFFLINE: "oduDefrost.guardOffline", IDENTITY_REQUIRED: "oduDefrost.guardIdentityRequired",
  AUTO_CONTROL_UNAVAILABLE: "oduDefrost.guardAutoControlUnavailable", ALREADY_ACTIVE: "oduDefrost.guardAlreadyActive",
  BUSY: "oduDefrost.guardBusy", INCIDENT_BLOCK: "oduDefrost.guardIncidentBlock",
  PEER_DEFROST_ACTIVE: "oduDefrost.guardPeerDefrostActive", NOT_HEATING: "oduDefrost.guardNotHeating",
  COMPRESSOR_NOT_RUNNING: "oduDefrost.guardCompressorNotRunning", COMPRESSOR_RUNNING: "oduDefrost.guardCompressorRunning",
};

const MODE_INFO_KEYS = {
  0: "oduDefrost.mode0Info", 1: "oduDefrost.mode1Info",
  3: "oduDefrost.mode3Info", 4: "oduDefrost.mode4Info",
};

function stateLabel(state) {
  return STATE_KEYS[state] ? t(STATE_KEYS[state]) : state;
}

function guardLabel(guard) {
  return GUARD_KEYS[guard] ? t(GUARD_KEYS[guard]) : guard;
}

function operationLabel(mode) {
  return t(["oduDefrost.operationStandby", "oduDefrost.operationCooling", "oduDefrost.operationHeating",
    "oduDefrost.operationHotWater", "oduDefrost.operationDefrost"][mode] || "common.unknown");
}

export function getOduDefrostEndpoint(hp, action) {
  return `${getBasePath()}/openquatt/odu-defrost/hp${Number(hp) === 2 ? 2 : 1}/${action}`;
}

export function getOduDefrostHpIndexes() {
  return getInstallationTopology() === "duo" || hasEntity("hp2ExcludeMinHz") ? [1, 2] : [1];
}

function integer(value) {
  if (value === null || value === undefined || value === "") return -1;
  const number = Number(value);
  return Number.isInteger(number) && number >= 0 ? number : -1;
}

function decimal(value) {
  if (value === null || value === undefined || value === "") return null;
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

export function isDefrostModeSupported(mode, variant) {
  if (!Number.isInteger(mode)) return false;
  if (variant === 1) return mode === 0 || mode === 1 || mode === 3;
  if (variant === 2 || variant === 3 || variant === 4) return mode === 0 || mode === 1 || mode === 3 || mode === 4;
  return false;
}

export function normalizeOduDefrostStatus(payload = {}, hp = 1) {
  return {
    hp: Number(payload.hp || hp) === 2 ? 2 : 1,
    variant: integer(payload.variant),
    online: payload.online === true, fresh: payload.fresh === true,
    identityReady: payload.identity_ready === true, loaded: payload.loaded === true,
    autoDefrostControlOk: payload.auto_defrost_control_ok === true, busy: payload.busy === true,
    active: payload.active === true, manual: payload.manual === true, canTrigger: payload.can_trigger === true,
    defrostMode: integer(payload.defrost_mode), operationMode: integer(payload.operation_mode),
    autoReapply: payload.auto_reapply === true,
    profileState: String(payload.profile_state || "NONE"),
    state: String(payload.state || "IDLE").toUpperCase(), guard: String(payload.guard || "OFFLINE").toUpperCase(),
    elapsedS: integer(payload.elapsed_s), sinceLastS: integer(payload.since_last_s), lastDurationS: integer(payload.last_duration_s),
    ambientC: decimal(payload.ambient_c), coilC: decimal(payload.coil_c), evaporationC: decimal(payload.evaporation_c),
    compressorHz: decimal(payload.compressor_hz), csrfToken: String(payload.csrf_token || ""),
  };
}

export function getOduDefrostStatus(hp) {
  return state.oduDefrostStatuses?.[Number(hp) === 2 ? 2 : 1] || null;
}

async function fetchStatus(hp) {
  const response = await fetchWithTimeout(getOduDefrostEndpoint(hp, "status"), {
    cache: "no-store", headers: { "Cache-Control": "no-store" },
  }, 8000, t("oduDefrost.statusTimeout", { hp: formatNumber(hp, { maximumFractionDigits: 0 }) }));
  if (!response.ok) throw new Error(t("oduDefrost.statusHttp", { hp: formatNumber(hp, { maximumFractionDigits: 0 }), status: formatNumber(response.status, { maximumFractionDigits: 0 }) }));
  return normalizeOduDefrostStatus(await response.json(), hp);
}

function storeStatus(status) {
  state.oduDefrostStatuses = { ...(state.oduDefrostStatuses || {}), [status.hp]: status };
  const draft = state.oduDefrostDrafts?.[status.hp];
  if (status.state === "SAVED" && draft?.mode === status.defrostMode && draft.autoReapply === status.autoReapply) {
    state.oduDefrostDrafts = { ...state.oduDefrostDrafts, [status.hp]: null };
  }
}

export function shouldRefreshOduDefrostSurface() {
  return state.systemModal === "odu-defrost" || /confirm-[12]$/.test(state.systemModal || "");
}

export async function refreshOduDefrostStatuses(options = {}) {
  if (!shouldRefreshOduDefrostSurface() && options.force !== true) return false;
  if (String(state.busyAction || "").startsWith("odu-defrost-")) return false;
  if (state.oduDefrostFetchPromise) return state.oduDefrostFetchPromise;
  const now = Date.now();
  if (!options.force && now - Number(state.oduDefrostLastFetchAt || 0) < 5000) return false;
  const epoch = Number(state.oduDefrostEpoch || 0);
  const previous = JSON.stringify(state.oduDefrostStatuses || {});
  const hadError = Boolean(state.oduDefrostError);
  state.oduDefrostFetchPromise = (async () => {
    try {
      const statuses = await Promise.all(getOduDefrostHpIndexes().map(fetchStatus));
      if (epoch !== Number(state.oduDefrostEpoch || 0)) return false;
      statuses.forEach(storeStatus);
      state.oduDefrostLastFetchAt = Date.now();
      state.oduDefrostError = "";
      state.oduDefrostStatusFailed = false;
      const changed = JSON.stringify(state.oduDefrostStatuses || {}) !== previous;
      const selectingMethod = typeof document !== "undefined" && document.activeElement?.matches?.("select[data-oq-odu-defrost-hp]");
      if ((changed || hadError || options.force || state.oduDefrostRenderPending) && shouldRefreshOduDefrostSurface()) {
        if (selectingMethod) state.oduDefrostRenderPending = true;
        else {
          state.oduDefrostRenderPending = false;
          render();
        }
      }
      return changed;
    } catch (error) {
      if (epoch !== Number(state.oduDefrostEpoch || 0)) return false;
      state.oduDefrostStatusFailed = true;
      state.oduDefrostError = t("oduDefrost.statusFetchFailed", { error: error.message || String(error) });
      if (shouldRefreshOduDefrostSurface()) render();
      return false;
    } finally {
      state.oduDefrostFetchPromise = null;
    }
  })();
  return state.oduDefrostFetchPromise;
}

function actionError(error) {
  const code = String(error || "");
  if (code === "forbidden") return t("oduDefrost.errorForbidden");
  if (code === "busy") return t("oduDefrost.errorBusy");
  if (code === "stale") return t("oduDefrost.stale");
  if (code === "invalid_mode") return t("oduDefrost.errorRejected");
  return code || t("oduDefrost.errorRejected");
}

async function postAction(hp, action, extra = {}) {
  const operationEpoch = (Number(state.oduDefrostEpoch || 0) + 1);
  state.oduDefrostEpoch = operationEpoch;
  let status = getOduDefrostStatus(hp);
  if (!status?.csrfToken) status = await fetchStatus(hp);
  if (!status.csrfToken) throw new Error(t("oduDefrost.errorCsrfMissing"));
  const body = new URLSearchParams({ csrf_token: status.csrfToken, ...extra });
  const response = await fetchWithTimeout(getOduDefrostEndpoint(hp, action), {
    method: "POST", cache: "no-store",
    headers: { "Cache-Control": "no-store", "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
    body: body.toString(),
  }, 8000, t("oduDefrost.actionTimeout", { hp: formatNumber(hp, { maximumFractionDigits: 0 }) }));
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(actionError(payload.error || `HTTP ${response.status}`));
  const next = normalizeOduDefrostStatus(payload, hp);
  if (operationEpoch === Number(state.oduDefrostEpoch || 0)) storeStatus(next);
  return next;
}

async function runOperation(hp, action, extra) {
  const current = getOduDefrostStatus(hp);
  if (state.busyAction || current?.busy || state.oduDefrostStatusFailed
      || (action === "trigger" && !current?.canTrigger)) return;
  if (action === "load") state.oduDefrostDrafts = { ...state.oduDefrostDrafts, [hp]: null };
  state.busyAction = `odu-defrost-hp${hp}-${action}`;
  state.oduDefrostError = "";
  render();
  try {
    const status = await postAction(hp, action, extra);
    const failure = ["READ_FAILED", "WRITE_FAILED", "WRITE_UNCERTAIN", "SAFETY_STOP", "STALE"].includes(status.state);
    if (failure) throw new Error(stateLabel(status.state));
  } catch (error) {
    state.oduDefrostStatusFailed = true;
    state.oduDefrostError = t("oduDefrost.actionUncertain", {
      hp: formatNumber(hp, { maximumFractionDigits: 0 }), error: error.message || String(error),
    });
  } finally {
    state.busyAction = "";
    state.oduDefrostLastFetchAt = 0;
    render();
  }
}

function formatValue(value, unit = "") {
  return value === null ? t("common.notAvailable") : `${formatNumber(value, {
    minimumFractionDigits: Number.isInteger(value) ? 0 : 1,
    maximumFractionDigits: Number.isInteger(value) ? 0 : 1,
  })}${unit}`;
}

function formatDuration(value) {
  if (value === -1) return t("common.notAvailable");
  const minutes = Math.floor(value / 60);
  const seconds = value % 60;
  const formattedMinutes = formatNumber(minutes, { maximumFractionDigits: 0 });
  const formattedSeconds = formatNumber(seconds, { maximumFractionDigits: 0 });
  return minutes
    ? t("oduDefrost.durationMinutesSeconds", { minutes: formattedMinutes, seconds: formattedSeconds })
    : t("oduDefrost.durationSeconds", { seconds: formattedSeconds });
}

export function getOduDefrostPresentation(status, statusFailed = false) {
  if (statusFailed) return [t("oduDefrost.statusUnavailable"), "warning"];
  if (!status) return [t("oduDefrost.statusLoading"), ""];
  if (status.active) return [t("oduDefrost.stateActive"), "warning"];
  if (status.state === "WAITING") return [stateLabel(status.state), "warning"];
  if (["READ_FAILED", "WRITE_FAILED", "WRITE_UNCERTAIN", "SAFETY_STOP", "TIMEOUT", "STALE"].includes(status.state)) return [stateLabel(status.state), "warning"];
  if (["QUEUED", "LOADING", "CHECKING"].includes(status.state)) return [stateLabel(status.state), ""];
  if (status.online && status.fresh && status.identityReady && !status.loaded
      && status.guard === "AUTO_CONTROL_UNAVAILABLE") return [t("oduDefrost.settingsNotLoaded"), ""];
  if (status.state === "SAVED" && status.online && status.fresh && status.identityReady) return [stateLabel(status.state), "success"];
  if (status.guard === "NOT_HEATING") return [operationLabel(status.operationMode), ""];
  if (status.guard !== "READY") return [guardLabel(status.guard), "warning"];
  if (status.loaded && status.autoDefrostControlOk && status.defrostMode >= 0
      && !isDefrostModeSupported(status.defrostMode, status.variant)) {
    return [t("oduDefrost.modeUnsupported"), "warning"];
  }
  return [stateLabel(status.state), ["COMPLETE", "SAVED"].includes(status.state) ? "success" : ""];
}

export function getDefrostModeName(mode, variant = null) {
  if (variant !== null && !isDefrostModeSupported(mode, variant)) return t("oduDefrost.modeUnsupported");
  return [
    t("oduDefrost.modeAdaptiveCoilInterval"), t("oduDefrost.modeFixedIntervalCoil"), t("oduDefrost.modeReserved"),
    t("oduDefrost.modeAdaptiveTrend"), t("oduDefrost.modeTaTevap"),
  ][mode] || t("common.unknown");
}

export function getDefrostModeCopy(mode, variant = null) {
  if (variant !== null && !isDefrostModeSupported(mode, variant)) return t("oduDefrost.modeUnsupportedCopy");
  return [t("oduDefrost.mode0Copy"), t("oduDefrost.mode1Copy"), t("oduDefrost.mode2Copy"), t("oduDefrost.mode3Copy"), t("oduDefrost.mode4Copy")][mode] || "";
}

export function canSaveDefrostMode(status, failed = false) {
  if (!status || failed || !status.online || !status.fresh || !status.identityReady) return false;
  if (!isDefrostModeSupported(0, status.variant)) return false;
  if (status.busy || !status.loaded || !status.autoDefrostControlOk || status.active) return false;
  return status.compressorHz === 0;
}

export function getOduDefrostEditorModel(hp) {
  const status = getOduDefrostStatus(hp);
  const draft = state.oduDefrostDrafts?.[hp];
  const selected = draft ? draft.mode : status?.defrostMode;
  const expected = draft ? draft.expected : status?.defrostMode;
  const autoReapply = draft ? draft.autoReapply : Boolean(status?.autoReapply);
  const options = [0, 1, 3, 4].filter((mode) => isDefrostModeSupported(mode, status?.variant));
  const busy = Boolean(state.busyAction || status?.busy);
  const selectionValid = options.includes(selected);
  const stale = Boolean(draft && expected !== status?.defrostMode);
  return {
    selected, expected, options, stale, autoReapply,
    disabled: busy || state.oduDefrostStatusFailed || !status?.online || !status.fresh || !status.identityReady || !status.loaded,
    applyDisabled: busy || !canSaveDefrostMode(status, state.oduDefrostStatusFailed) || !selectionValid || stale || (selected === status.defrostMode && autoReapply === status.autoReapply),
    copy: getDefrostModeCopy(selected, status?.variant),
  };
}

export function updateOduDefrostDraft(input) {
  const hp = Number(input.dataset.oqOduDefrostHp) === 2 ? 2 : 1;
  const current = getOduDefrostEditorModel(hp);
  const mode = input.type === "checkbox" ? current.selected : input.value === "" ? -1 : Number(input.value);
  const status = getOduDefrostStatus(hp);
  if (!isDefrostModeSupported(mode, status?.variant) || getOduDefrostEditorModel(hp).disabled) return false;
  state.oduDefrostDrafts = {
    ...state.oduDefrostDrafts,
    [hp]: { mode, expected: state.oduDefrostDrafts?.[hp]?.expected ?? status.defrostMode,
      autoReapply: input.type === "checkbox" ? input.checked : current.autoReapply },
  };
  const model = getOduDefrostEditorModel(hp);
  const panel = input.closest?.(".oq-defrost-panel");
  const copy = panel?.querySelector("[data-oq-defrost-mode-copy]");
  const apply = panel?.querySelector('[data-oq-action="odu-defrost-apply"]');
  if (copy) copy.textContent = model.copy;
  if (apply) apply.disabled = model.applyDisabled;
  return true;
}

function renderValues(items) {
  return `<dl class="oq-defrost-values">${items.map(([label, value]) => `<dt>${escapeHtml(label)}</dt><dd>${escapeHtml(value)}</dd>`).join("")}</dl>`;
}

function renderPanel(hp) {
  const status = getOduDefrostStatus(hp);
  const busy = Boolean(status?.busy || String(state.busyAction || "").startsWith(`odu-defrost-hp${hp}-`));
  const failed = state.oduDefrostStatusFailed === true;
  const [statusLabel, tone] = getOduDefrostPresentation(status, failed);
  const loadReady = Boolean(status && !failed && status.online && status.fresh && status.identityReady && !busy);
  const triggerDisabled = !loadReady || !status.loaded || !status.autoDefrostControlOk || !status.canTrigger || status.active;
  const model = getOduDefrostEditorModel(hp);
  const mode = getDefrostModeName(status?.defrostMode, status?.variant);
  const operation = operationLabel(status?.operationMode);
  return renderOduEditorPanel({
    hp, title: t("oduDefrost.modeLabel"), copy: t(["oduSettings.variantUnknown", "oduSettings.variantV1", "oduSettings.variantV15", "oduSettings.variantV2Old", "oduSettings.variantV2New"][status?.variant] || "oduSettings.variantUnknown"), panelClass: "oq-defrost-panel",
    actions: renderOduEditorAction(hp, "odu-defrost-load", busy ? t("common.busy") : t("oduDefrost.loadAction"), !loadReady),
    statusLabel, tone,
    body: !status ? "" : `
      <div class="oq-defrost-settings-head"><small>${escapeHtml(status.guard === "NOT_HEATING" ? t("oduDefrost.guardNotHeating") : operation)}</small>${renderOduEditorAction(hp, "odu-defrost-trigger", t("oduDefrost.triggerAction"), triggerDisabled, "warning")}</div>
      ${renderValues([
        ...(status.active ? [[t("oduDefrost.elapsed"), formatDuration(status.elapsedS)]] : []),
        [t("oduDefrost.sinceLast"), status.sinceLastS >= 0 ? formatDuration(status.sinceLastS) : t("oduDefrost.noHistory")],
        ...(status.lastDurationS >= 0 ? [[t("oduDefrost.lastDuration"), formatDuration(status.lastDurationS)]] : []),
      ])}
      <section class="oq-defrost-settings" aria-label="${escapeHtml(t("oduDefrost.settingsTitle"))}">
        ${status.loaded ? `
          <div class="oq-defrost-settings-head oq-defrost-method-head"><strong>${escapeHtml(t("oduDefrost.modeLabel"))}</strong><button class="oq-settings-info-button" type="button" data-oq-action="odu-defrost-info" data-hp="${hp}" aria-label="${escapeHtml(t("settingsControls.infoAria", { title: t("oduDefrost.modeLabel") }))}" title="${escapeHtml(t("oduDefrost.modeLabel"))}">i</button></div>
          <label class="oq-defrost-mode-field">
            <select class="oq-helper-select" aria-label="${escapeHtml(t("oduDefrost.modeLabel"))}" data-oq-odu-defrost-hp="${hp}" ${model.disabled ? "disabled" : ""}>
              ${model.options.includes(model.selected) ? "" : `<option value="" selected disabled>${escapeHtml(t("oduDefrost.chooseMethod"))}</option>`}
              ${model.options.map((m) => `<option value="${m}"${model.selected === m ? " selected" : ""}>${m} · ${escapeHtml(getDefrostModeName(m))}</option>`).join("")}
            </select></label>
          <p class="oq-defrost-current">${escapeHtml(t("oduDefrost.currentMethod", { mode }))}</p>
          <p data-oq-defrost-mode-copy>${escapeHtml(model.copy)}</p>
          ${model.stale ? `<p class="oq-helper-error">${escapeHtml(t("oduDefrost.stale"))}</p>` : ""}
          <label class="oq-settings-odu-auto"><input type="checkbox" data-oq-odu-defrost-hp="${hp}" ${model.autoReapply ? "checked" : ""} ${model.disabled ? "disabled" : ""}><span><strong>${escapeHtml(t("oduSettings.reapplyTitle"))}</strong><small>${escapeHtml(t("oduSettings.reapplyCopy"))}</small></span></label>
          ${status.profileState === "PERSIST_FAILED" ? `<p role="status">${escapeHtml(t("oduSettings.statusPersistFail"))}</p>` : status.profileState === "IDENTITY_MISMATCH" ? `<p role="status">${escapeHtml(t("oduSettings.statusMismatch"))}</p>` : status.autoReapply && status.profileState === "PENDING" ? `<p role="status">${escapeHtml(t("oduSettings.statusPending"))}</p>` : ""}
          <div class="oq-defrost-apply-row"><p>${escapeHtml(t("oduDefrost.settingsCopy"))}</p>
            ${renderOduEditorAction(hp, "odu-defrost-apply", t("oduDefrost.applyAction"), model.applyDisabled, "warning")}</div>` : ""}
      </section>
      <details class="oq-settings-odu-technical"${state.oduDefrostDetailsOpen?.[hp] ? " open" : ""}><summary data-oq-action="toggle-odu-defrost-details" data-hp="${hp}">${escapeHtml(t("oduDefrost.detailsSummary"))}</summary>
        <p>${escapeHtml(t("oduDefrost.detailsCopy"))}</p>
        ${renderValues([
          [t("oduDefrost.ambient"), formatValue(status.ambientC, " °C")],
          [t("oduDefrost.coil"), formatValue(status.coilC, " °C")],
          [t("oduDefrost.evaporation"), formatValue(status.evaporationC, " °C")],
          [t("oduDefrost.compressor"), formatValue(status.compressorHz, " Hz")],
        ])}
      </details>`,
  });
}

export function renderOduDefrostModal() {
  const info = /^odu-defrost-info-([12])$/.exec(state.systemModal || "");
  if (info) {
    const hp = Number(info[1]);
    const status = getOduDefrostStatus(hp);
    return renderModalShell({
      modalId: "odu-defrost-info", titleId: "oq-odu-defrost-info-title", kicker: `HP${hp}`,
      modalClass: "oq-helper-modal--scrollable oq-defrost-info-modal",
      title: t("oduDefrost.modeLabel"), closeAction: "odu-defrost-cancel", closeLabel: t("common.close"),
      bodyMarkup: `<p class="oq-helper-modal-copy">${escapeHtml(t("oduDefrost.methodInfo"))}</p>
        ${[0, 1, 3, 4].filter((mode) => isDefrostModeSupported(mode, status?.variant)).map((mode) => `<section><h3>${mode} · ${escapeHtml(getDefrostModeName(mode))}</h3><p class="oq-helper-modal-copy">${escapeHtml(getDefrostModeCopy(mode, status.variant))} ${escapeHtml(t(MODE_INFO_KEYS[mode]))}</p></section>`).join("")}
        <div class="oq-helper-modal-actions">${renderOduEditorAction(hp, "odu-defrost-cancel", t("common.close"), false)}</div>`,
    });
  }
  const confirmation = /^odu-defrost-confirm-([12])$/.exec(state.systemModal || "");
  if (confirmation) {
    const hp = Number(confirmation[1]);
    const status = getOduDefrostStatus(hp);
    const disabled = Boolean(state.busyAction || state.oduDefrostStatusFailed || !status?.canTrigger || status.busy);
    return renderModalShell({
      modalId: "odu-defrost-confirm", titleId: "oq-odu-defrost-confirm-title",
      kicker: `HP${hp}`, title: t("oduDefrost.triggerAction"),
      closeAction: "odu-defrost-cancel", closeLabel: t("common.cancel"),
      bodyMarkup: `<p class="oq-helper-modal-copy">${escapeHtml(t("oduDefrost.triggerConfirm"))}</p>
        <div class="oq-helper-modal-actions">
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="odu-defrost-cancel">${escapeHtml(t("common.cancel"))}</button>
          ${renderOduEditorAction(hp, "odu-defrost-confirm", t("oduDefrost.triggerAction"), disabled, "warning")}
        </div>`,
    });
  }
  const saveConfirmation = /^odu-defrost-save-confirm-([12])$/.exec(state.systemModal || "");
  if (saveConfirmation) {
    const hp = Number(saveConfirmation[1]);
    const pending = state.oduDefrostSave;
    const status = getOduDefrostStatus(hp);
    const desired = pending?.hp === hp ? pending.desired : -1;
    const expected = pending?.hp === hp ? pending.expected : -1;
    const disabled = Boolean(state.busyAction || state.oduDefrostStatusFailed || status?.busy || !canSaveDefrostMode(status, state.oduDefrostStatusFailed) || !isDefrostModeSupported(desired, status?.variant) || expected !== status?.defrostMode);
    return renderModalShell({
      modalId: "odu-defrost-save-confirm", titleId: "oq-odu-defrost-save-confirm-title",
      kicker: `HP${hp}`, title: t("oduDefrost.saveAction"),
      closeAction: "odu-defrost-cancel", closeLabel: t("common.cancel"),
      bodyMarkup: `<p class="oq-helper-modal-copy">${escapeHtml(t("oduDefrost.saveConfirm", { old: getDefrostModeName(expected, status?.variant), new: getDefrostModeName(desired, status?.variant), hp: formatNumber(hp, { maximumFractionDigits: 0 }), reapply: t(pending?.autoReapply ? "common.yes" : "common.no") }))}</p>
        <div class="oq-helper-modal-actions">
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="odu-defrost-cancel">${escapeHtml(t("common.cancel"))}</button>
          ${renderOduEditorAction(hp, "odu-defrost-save-confirm", t("oduDefrost.saveAction"), disabled, "warning")}
        </div>`,
    });
  }
  return renderOduEditorModal({
    modalId: "odu-defrost", titleId: "oq-odu-defrost-title", title: t("oduDefrost.title"), closeLabel: t("oduDefrost.closeLabel"),
    warning: `<strong>${escapeHtml(t("oduDefrost.warningTitle"))}</strong><p>${escapeHtml(t("oduDefrost.warningCopy"))}</p>`,
    error: state.oduDefrostError, panels: getOduDefrostHpIndexes().map(renderPanel).join(""),
  });
}

const actionHandlers = {
  "odu-defrost-info": (button) => {
    state.systemModal = `odu-defrost-info-${Number(button.dataset.hp) === 2 ? 2 : 1}`;
    render();
  },
  "odu-defrost-load": (button) => runOperation(Number(button.dataset.hp) === 2 ? 2 : 1, "load"),
  "odu-defrost-trigger": (button) => {
    const hp = Number(button.dataset.hp) === 2 ? 2 : 1;
    if (state.busyAction || state.oduDefrostStatusFailed || !getOduDefrostStatus(hp)?.canTrigger) return;
    state.systemModal = `odu-defrost-confirm-${hp}`;
    render();
  },
  "odu-defrost-cancel": () => {
    state.oduDefrostSave = null;
    state.systemModal = "odu-defrost";
    render();
  },
  "odu-defrost-confirm": () => {
    const confirmation = /^odu-defrost-confirm-([12])$/.exec(state.systemModal || "");
    if (!confirmation) return;
    state.systemModal = "odu-defrost";
    render();
    return runOperation(Number(confirmation[1]), "trigger");
  },
  "odu-defrost-apply": (button) => openSaveConfirm(button),
  "odu-defrost-save-confirm": () => {
    const saveConfirmation = /^odu-defrost-save-confirm-([12])$/.exec(state.systemModal || "");
    const pending = state.oduDefrostSave;
    if (!saveConfirmation || !pending || Number(saveConfirmation[1]) !== pending.hp) return;
    const hp = pending.hp, desired = pending.desired, expected = pending.expected;
    const status = getOduDefrostStatus(hp);
    if (!canSaveDefrostMode(status, state.oduDefrostStatusFailed) || !isDefrostModeSupported(desired, status?.variant) || expected !== status.defrostMode) return;
    state.oduDefrostSave = null;
    state.systemModal = "odu-defrost";
    render();
    return runOperation(hp, "save", { mode: String(desired), expected_mode: String(expected), auto_reapply: String(pending.autoReapply) });
  },
};

function openSaveConfirm(button) {
  const hp = Number(button.dataset.hp) === 2 ? 2 : 1;
  const model = getOduDefrostEditorModel(hp);
  if (model.applyDisabled) return;
  state.oduDefrostSave = { hp, desired: model.selected, expected: model.expected, autoReapply: model.autoReapply };
  state.systemModal = `odu-defrost-save-confirm-${hp}`;
  render();
}

export function handleOduDefrostAction(action, button) {
  return invokeActionMap(actionHandlers, action, button);
}
