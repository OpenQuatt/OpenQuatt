import { hasEntity } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { fetchWithTimeout } from "../core/browser-utils.js";
import { escapeHtml } from "../core/html.js";
import { renderNumberInputControl } from "../core/number-controls.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { getInstallationTopology } from "./device-context.js";
import { renderOduEditorAction, renderOduEditorModal, renderOduEditorPanel } from "./odu-editor-ui.js";
import { formatNumber, t } from "../i18n/index.js";

const MODE_OPTIONS = [
  [1, "oduSettings.mode1"],
  [2, "oduSettings.mode2"],
  [3, "oduSettings.mode3"],
];

// Match the backend capability check; unknown variants are never writable.
export function isOduSettingsModeSupported(mode, variant) {
  if (!Number.isInteger(mode) || mode < 0 || mode > 3) return false;
  if (variant === 1) return mode <= 2;
  return variant === 2 || variant === 3 || variant === 4;
}

function modeDescription(mode) {
  if (Number(mode) === 1) {
    return t("oduSettings.modeDesc1");
  }
  if (Number(mode) === 2) {
    return t("oduSettings.modeDesc2");
  }
  if (Number(mode) === 3) {
    return t("oduSettings.modeDesc3");
  }
  return t("oduSettings.modeUnknown");
}

function settingsSummary(settings, variant) {
  const mode = Number(settings?.mode);
  if (!isOduSettingsModeSupported(mode, variant)) {
    return t("oduSettings.summaryOther", { mode: Number.isInteger(mode) ? formatNumber(mode, { maximumFractionDigits: 0 }) : "—" });
  }
  if (mode === 1) {
    return t("oduSettings.summaryMode1", { start: settings?.startTemperatureC ?? "—", stop: settings?.stopDeltaC ?? "—" });
  }
  if (mode === 3) return t("oduSettings.summaryMode3", { start: settings?.startTemperatureC ?? "—" });
  return t("oduSettings.summaryOther", { mode: Number.isInteger(mode) ? formatNumber(mode, { maximumFractionDigits: 0 }) : "—" });
}

export function getOduSettingsEndpoint(hp, action) {
  return `${getBasePath()}/openquatt/odu-settings/hp${Number(hp) === 2 ? 2 : 1}/${action}`;
}

export function getOduSettingsHpIndexes() {
  return getInstallationTopology() === "duo" || hasEntity("hp2ExcludeMinHz") ? [1, 2] : [1];
}

function normalizeSettings(values = {}) {
  const mode = Number(values.mode);
  const startTemperatureC = Number(values.start_temperature_c);
  const stopDeltaC = Number(values.stop_delta_c);
  return {
    mode: Number.isInteger(mode) ? mode : null,
    startTemperatureC: Number.isInteger(startTemperatureC) ? startTemperatureC : null,
    stopDeltaC: Number.isInteger(stopDeltaC) ? stopDeltaC : null,
  };
}

export function normalizeOduSettingsStatus(payload = {}, hp = 1) {
  return {
    available: payload.available !== false,
    unsupported: payload.unsupported === true,
    hp: Number(payload.hp || hp) === 2 ? 2 : 1,
    busy: payload.busy === true,
    loaded: payload.loaded === true,
    profileAvailable: payload.profile_available === true,
    autoReapply: payload.auto_reapply === true,
    identityReady: payload.identity_ready === true,
    identityMatches: payload.identity_matches === true,
    writeUncertain: payload.write_uncertain === true,
    variant: Number(payload.variant) || 0,
    controlBoardItem: Number(payload.control_board_item) || 0,
    status: String(payload.status || "READY"),
    csrfToken: String(payload.csrf_token || ""),
    actual: normalizeSettings(payload.actual),
    desired: normalizeSettings(payload.desired),
    defaults: normalizeSettings(payload.defaults),
  };
}

export function getOduSettingsStatus(hp) {
  return state.oduSettingsStatuses?.[Number(hp) === 2 ? 2 : 1] || null;
}

function createDraft(status) {
  const source = status?.profileAvailable && status.identityMatches
    ? status.desired
    : status?.loaded
      ? status.actual
      : status?.defaults;
  return {
    mode: String(source?.mode ?? ""),
    startTemperatureC: String(source?.startTemperatureC ?? ""),
    stopDeltaC: String(source?.stopDeltaC ?? ""),
    autoReapply: status?.autoReapply === true,
    dirty: false,
  };
}

function getDraft(hp) {
  const hpIndex = Number(hp) === 2 ? 2 : 1;
  return state.oduSettingsDrafts?.[hpIndex] || createDraft(getOduSettingsStatus(hpIndex));
}

function hydrateDraft(status, force = false) {
  const current = state.oduSettingsDrafts?.[status.hp];
  if (current?.dirty && !force) return;
  state.oduSettingsDrafts = {
    ...(state.oduSettingsDrafts || {}),
    [status.hp]: createDraft(status),
  };
}

async function fetchStatus(hp) {
  const response = await fetchWithTimeout(
    getOduSettingsEndpoint(hp, "status"),
    { cache: "no-store", headers: { "Cache-Control": "no-store" } },
    8000,
    t("oduSettings.fetchTimeout", { hp }),
  );
  if (response.status === 404) {
    return normalizeOduSettingsStatus({ available: false, unsupported: true, hp }, hp);
  }
  if (!response.ok) throw new Error(t("oduSettings.fetchHttp", { hp, status: response.status }));
  return normalizeOduSettingsStatus(await response.json(), hp);
}

function storeStatus(status, forceDraft = false) {
  state.oduSettingsStatuses = { ...(state.oduSettingsStatuses || {}), [status.hp]: status };
  hydrateDraft(status, forceDraft);
}

function statusFetchErrorMessage(error) {
  const detail = String(error?.message || error || "").trim();
  if (!detail || /failed to fetch|networkerror|load failed/i.test(detail)) {
    return t("oduSettings.fetchError");
  }
  return t("oduSettings.fetchErrorDetail", { detail });
}

export function shouldRefreshOduSettingsSurface() {
  return state.systemModal === "odu-bottom-plate-settings";
}

export async function refreshOduSettingsStatuses(options = {}) {
  if (!shouldRefreshOduSettingsSurface() && options.force !== true) return false;
  if (state.oduSettingsFetchPromise) return state.oduSettingsFetchPromise;
  const now = Date.now();
  if (!options.force && now - Number(state.oduSettingsLastFetchAt || 0) < 5000) return false;
  const previous = JSON.stringify(state.oduSettingsStatuses || {});
  const hadError = Boolean(state.oduSettingsError);
  state.oduSettingsFetchPromise = (async () => {
    try {
      const statuses = await Promise.all(getOduSettingsHpIndexes().map(fetchStatus));
      statuses.forEach((status) => storeStatus(status));
      state.oduSettingsLastFetchAt = Date.now();
      state.oduSettingsError = "";
      const changed = JSON.stringify(state.oduSettingsStatuses || {}) !== previous;
      if ((changed || hadError) && shouldRefreshOduSettingsSurface()) render();
      return changed || hadError;
    } catch (error) {
      state.oduSettingsError = statusFetchErrorMessage(error);
      if (!options.silent) render();
      return false;
    } finally {
      state.oduSettingsFetchPromise = null;
    }
  })();
  return state.oduSettingsFetchPromise;
}

function errorMessage(error) {
  if (error === "busy") return t("oduSettings.errBusy");
  if (error === "unavailable") return t("oduSettings.errUnavailable");
  if (error === "identity_required") return t("oduSettings.errIdentity");
  if (error === "invalid_settings") return t("oduSettings.errInvalid");
  if (error === "forbidden") return t("oduSettings.errForbidden");
  return error || t("oduSettings.errDefault");
}

async function postAction(hp, action, values = {}) {
  let status = getOduSettingsStatus(hp);
  if (!status?.csrfToken) {
    status = await fetchStatus(hp);
    storeStatus(status);
  }
  const body = new URLSearchParams({ csrf_token: status.csrfToken });
  Object.entries(values).forEach(([key, value]) => body.set(key, String(value)));
  const response = await fetchWithTimeout(
    getOduSettingsEndpoint(hp, action),
    {
      method: "POST",
      cache: "no-store",
      headers: { "Cache-Control": "no-store", "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
      body: body.toString(),
    },
    8000,
    t("oduSettings.actionTimeout", { hp }),
  );
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(errorMessage(payload.error || `HTTP ${response.status}`));
  const next = normalizeOduSettingsStatus(payload, hp);
  storeStatus(next);
  return next;
}

async function waitForOperation(hp) {
  const deadline = Date.now() + 35000;
  while (Date.now() < deadline) {
    await new Promise((resolve) => window.setTimeout(resolve, 500));
    const status = await fetchStatus(hp);
    storeStatus(status);
    render();
    if (!status.busy) return status;
  }
  throw new Error(t("oduSettings.waitLong"));
}

function parseDraftInteger(value) {
  const raw = String(value ?? "").trim();
  if (!raw) return null;
  const parsed = Number(raw);
  return Number.isInteger(parsed) ? parsed : null;
}

function validDraft(draft, variant) {
  const mode = parseDraftInteger(draft.mode);
  const startTemperatureC = parseDraftInteger(draft.startTemperatureC);
  const stopDeltaC = parseDraftInteger(draft.stopDeltaC);
  return mode !== null && mode >= 1 && isOduSettingsModeSupported(mode, variant)
    && startTemperatureC !== null && startTemperatureC >= -30 && startTemperatureC <= 30
    && stopDeltaC !== null && stopDeltaC >= 0 && stopDeltaC <= 30;
}

export function getOduSettingsEditorModel(hp) {
  const status = getOduSettingsStatus(hp);
  const draft = getDraft(hp);
  const busy = Boolean(status?.busy || String(state.busyAction || "").startsWith(`odu-settings-hp${hp}-`));
  const enabled = Boolean(status?.available && status.identityReady && !status.unsupported && !busy);
  const mode = parseDraftInteger(draft.mode);
  const modeSupported = isOduSettingsModeSupported(mode, status?.variant);
  return {
    status, draft, busy, enabled,
    modeOptions: MODE_OPTIONS.filter(([value]) => isOduSettingsModeSupported(value, status?.variant)),
    saveDisabled: !enabled || !validDraft(draft, status?.variant),
    temperatureSettingsVisible: modeSupported && (mode === 1 || mode === 3),
    stopDeltaVisible: modeSupported && mode === 1,
    modeCopy: modeSupported ? modeDescription(draft.mode) : t("oduSettings.modeUnknown"),
  };
}

function assertOperationCompleted(status, action) {
  // Older firmware can persist a profile while still waiting for standby.
  const allowed = action === "load" ? ["LOADED"] : ["IN_SYNC", "PENDING_SAFE"];
  const confirmed = status.available && !status.unsupported && !status.busy && status.loaded
    && allowed.includes(status.status)
    && (action === "load" || (status.profileAvailable && status.identityMatches && !status.writeUncertain));
  if (!confirmed) throw new Error(t("oduSettings.confirmResult", { status: status.status }));
}

async function runOperation(hp, action) {
  const draft = getDraft(hp);
  if (action === "save" && !validDraft(draft, getOduSettingsStatus(hp)?.variant)) {
    state.oduSettingsError = t("oduSettings.invalidDraft", { hp });
    render();
    return;
  }
  state.busyAction = `odu-settings-hp${hp}-${action}`;
  state.oduSettingsError = "";
  state.controlNotice = "";
  render();
  try {
    let status = await postAction(hp, action, action === "save" ? {
      mode: Number(draft.mode),
      start_temperature_c: Number(draft.startTemperatureC),
      stop_delta_c: Number(draft.stopDeltaC),
      auto_reapply: draft.autoReapply,
    } : {});
    if (status.busy) status = await waitForOperation(hp);
    storeStatus(status, true);
    assertOperationCompleted(status, action);
    state.controlNotice = status.status === "PENDING_SAFE"
      ? t("oduSettings.savedPending", { hp })
      : action === "load"
        ? t("oduSettings.loadedValues", { hp })
        : t("oduSettings.savedChecked", { hp });
  } catch (error) {
    state.oduSettingsError = t("oduSettings.actionFailed", { hp, error: error.message || String(error) });
  } finally {
    state.busyAction = "";
    state.oduSettingsLastFetchAt = 0;
    render();
  }
}

export function updateOduSettingsDraft(input) {
  const hp = Number(input.dataset.oqOduSettingsHp) === 2 ? 2 : 1;
  const field = String(input.dataset.oqOduSettingsField || "");
  if (!["mode", "startTemperatureC", "stopDeltaC", "autoReapply"].includes(field)) return false;
  const draft = getDraft(hp);
  state.oduSettingsDrafts = {
    ...(state.oduSettingsDrafts || {}),
    [hp]: { ...draft, [field]: field === "autoReapply" ? Boolean(input.checked) : String(input.value), dirty: true },
  };
  const panel = input.closest(".oq-settings-odu-runtime-panel");
  const model = getOduSettingsEditorModel(hp);
  const temperatureSettings = panel?.querySelector("[data-oq-odu-temperature-settings]");
  const modeOutput = panel?.querySelector("[data-oq-odu-mode-description]");
  const stopDeltaSetting = panel?.querySelector("[data-oq-odu-stop-delta-setting]");
  const saveButton = panel?.querySelector('[data-oq-action="odu-settings-save"]');
  if (temperatureSettings) temperatureSettings.hidden = !model.temperatureSettingsVisible;
  if (modeOutput) modeOutput.textContent = model.modeCopy;
  if (stopDeltaSetting) stopDeltaSetting.hidden = !model.stopDeltaVisible;
  if (saveButton) saveButton.disabled = model.saveDisabled;
  return true;
}

function statusPresentation(status) {
  if (!status) return state.oduSettingsError
    ? [t("oduSettings.statusUnavailable"), "warning"]
    : [t("oduSettings.statusLoading"), ""];
  const code = String(status?.status || "").toUpperCase();
  if (status?.writeUncertain || code === "VERIFY_FAILED") return [t("oduSettings.statusUncertain"), "warning"];
  if (code === "INVALID_SETTINGS" || (status.identityReady && status.loaded
      && !isOduSettingsModeSupported(status.actual?.mode, status.variant))) {
    return [t("oduSettings.errInvalid"), "warning"];
  }
  if (code === "IN_SYNC") return [t("oduSettings.statusInSync"), "success"];
  if (code === "PENDING_SAFE") return [t("oduSettings.statusPending"), "warning"];
  if (code === "APPLYING" || status?.busy) return [t("oduSettings.statusApplying"), ""];
  if (code === "IDENTITY_MISMATCH") return [t("oduSettings.statusMismatch"), "warning"];
  if (code === "PERSIST_FAILED") return [t("oduSettings.statusPersistFail"), "warning"];
  if (code === "LOADED") return [status.autoReapply ? t("oduSettings.statusLoadedOn") : t("oduSettings.statusLoadedOff"), ""];
  if (status?.unsupported) return [t("oduSettings.statusUnsupported"), "warning"];
  if (!status?.available) return [t("oduSettings.statusUnreachable"), "warning"];
  return [t("oduSettings.statusNotLoaded"), ""];
}

function variantLabel(variant) {
  if (variant === 1) return t("oduSettings.variantV1");
  if (variant === 2) return t("oduSettings.variantV15");
  if (variant === 3) return t("oduSettings.variantV2Old");
  if (variant === 4) return t("oduSettings.variantV2New");
  return t("oduSettings.variantUnknown");
}

function renderPanel(hp) {
  const model = getOduSettingsEditorModel(hp);
  const { status, draft, busy, enabled } = model;
  const [statusLabel, tone] = statusPresentation(status);
  return renderOduEditorPanel({
    hp, title: t("oduSettings.panelTitle"), copy: variantLabel(status?.variant),
    actions: renderOduEditorAction(hp, "odu-settings-load", t("oduSettings.loadAction"), !enabled),
    statusLabel, tone,
    body: status?.loaded || status?.profileAvailable ? `
        <div class="oq-settings-odu-fields">
          <label><span>${escapeHtml(t("oduSettings.controlLabel"))}</span><select class="oq-helper-select" data-oq-odu-settings-hp="${hp}" data-oq-odu-settings-field="mode" ${!enabled ? "disabled" : ""}>
            ${model.modeOptions.some(([value]) => value === Number(draft.mode)) ? "" : `<option value="" selected disabled>${escapeHtml(t("oduSettings.chooseControl"))}</option>`}
            ${model.modeOptions.map(([value, labelKey]) => `<option value="${value}"${Number(draft.mode) === value ? " selected" : ""}>${value} · ${escapeHtml(t(labelKey))}</option>`).join("")}
          </select></label>
          <p class="oq-settings-odu-mode-description" data-oq-odu-mode-description aria-live="polite">${escapeHtml(model.modeCopy)}</p>
          <div class="oq-settings-odu-temperature-settings" data-oq-odu-temperature-settings${model.temperatureSettingsVisible ? "" : " hidden"}>
            <label><span>${escapeHtml(Number(draft.mode) === 1 ? t("oduSettings.limitOn") : t("oduSettings.limitDefrost"))}</span>${renderNumberInputControl({
              value: draft.startTemperatureC, meta: { min: -30, max: 30, step: 1 }, disabled: !enabled,
              controlTag: "span", controlClass: "oq-helper-control oq-helper-control--suffix",
              inputAttributes: `data-oq-odu-settings-hp="${hp}" data-oq-odu-settings-field="startTemperatureC"`,
              unitMarkup: '<span class="oq-helper-unit-chip">°C</span>',
            })}</label>
            <label data-oq-odu-stop-delta-setting${model.stopDeltaVisible ? "" : " hidden"}><span>${escapeHtml(t("oduSettings.hysteresis"))}</span>${renderNumberInputControl({
              value: draft.stopDeltaC, meta: { min: 0, max: 30, step: 1 }, disabled: !enabled,
              controlTag: "span", controlClass: "oq-helper-control oq-helper-control--suffix",
              inputAttributes: `data-oq-odu-settings-hp="${hp}" data-oq-odu-settings-field="stopDeltaC"`,
              unitMarkup: '<span class="oq-helper-unit-chip">K</span>',
            })}</label>
          </div>
        </div>
        <label class="oq-settings-odu-auto"><input type="checkbox" data-oq-odu-settings-hp="${hp}" data-oq-odu-settings-field="autoReapply" ${draft.autoReapply ? "checked" : ""} ${!enabled ? "disabled" : ""}><span><strong>${escapeHtml(t("oduSettings.reapplyTitle"))}</strong><small>${escapeHtml(t("oduSettings.reapplyCopy"))}</small></span></label>
        ${settingsSummary(status?.defaults, status?.variant) === settingsSummary(status?.actual, status?.variant) ? "" : `<p class="oq-settings-odu-runtime-validation">${escapeHtml(t("oduSettings.validationDefault", { defaults: settingsSummary(status?.defaults, status?.variant), actual: settingsSummary(status?.actual, status?.variant) }))}</p>`}
        <div class="oq-helper-modal-actions">${renderOduEditorAction(hp, "odu-settings-save", busy ? t("common.busy") : t("oduSettings.saveApply"), model.saveDisabled, "primary")}</div>
      ` : `<p class="oq-settings-odu-runtime-validation">${escapeHtml(t("oduSettings.loadFirst"))}</p>`,
  });
}

export function renderOduSettingsModal() {
  return renderOduEditorModal({
    modalId: "odu-bottom-plate-settings",
    titleId: "oq-odu-settings-title",
    title: t("oduSettings.modalTitle"),
    closeLabel: t("oduSettings.modalClose"),
    warning: t("oduSettings.modalWarning"),
    error: state.oduSettingsError,
    notice: String(state.controlNotice || "").startsWith("HP") ? state.controlNotice : "",
    panels: getOduSettingsHpIndexes().map(renderPanel).join(""),
  });
}

const actionHandlers = {
  "odu-settings-load": (button) => runOperation(Number(button.dataset.hp) === 2 ? 2 : 1, "load"),
  "odu-settings-save": (button) => runOperation(Number(button.dataset.hp) === 2 ? 2 : 1, "save"),
};

export function handleOduSettingsAction(action, button) {
  return invokeActionMap(actionHandlers, action, button);
}

export async function getOduSettingsBackupProfiles() {
  const profiles = {};
  const statuses = await Promise.all(getOduSettingsHpIndexes().map(fetchStatus));
  statuses.forEach((status) => {
    if (!status.profileAvailable || !status.identityMatches
        || !isOduSettingsModeSupported(status.desired.mode, status.variant)) return;
    profiles[`hp${status.hp}`] = {
      variant: status.variant,
      control_board_item: status.controlBoardItem,
      mode: status.desired.mode,
      start_temperature_c: status.desired.startTemperatureC,
      stop_delta_c: status.desired.stopDeltaC,
      auto_reapply: status.autoReapply,
    };
  });
  return profiles;
}

export async function restoreOduSettingsBackupProfiles(profiles = {}) {
  const results = [];
  const availableHp = new Set(getOduSettingsHpIndexes());
  for (const [key, profile] of Object.entries(profiles)) {
    const hp = key === "hp2" ? 2 : key === "hp1" ? 1 : 0;
    if (!hp || !availableHp.has(hp)) {
      results.push({ key, applied: false, reason: t("oduSettings.backupMissing") });
      continue;
    }
    try {
      const current = await fetchStatus(hp);
      storeStatus(current);
      if (!current.identityReady || current.variant !== profile.variant
          || current.controlBoardItem !== profile.control_board_item) {
        results.push({ key, applied: false, reason: t("oduSettings.backupMismatch") });
        continue;
      }
      if (!isOduSettingsModeSupported(profile.mode, current.variant)) {
        results.push({ key, applied: false, reason: t("oduSettings.errInvalid") });
        continue;
      }
      let status = await postAction(hp, "save", {
        mode: profile.mode,
        start_temperature_c: profile.start_temperature_c,
        stop_delta_c: profile.stop_delta_c,
        auto_reapply: profile.auto_reapply,
      });
      if (status.busy) status = await waitForOperation(hp);
      assertOperationCompleted(status, "save");
      results.push({ key, applied: true, pending: status.status === "PENDING_SAFE" });
    } catch (error) {
      results.push({ key, applied: false, reason: t("oduSettings.backupUnconfirmed", { error: error.message || String(error) }) });
    }
  }
  return results;
}
