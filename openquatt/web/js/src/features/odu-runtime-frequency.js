import { hasEntity } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { fetchWithTimeout } from "../core/browser-utils.js";
import { getEntityValue, parseLooseNumber } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { renderNumberInputControl } from "../core/number-controls.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { getInstallationTopology } from "./device-context.js";
import { renderOduEditorAction, renderOduEditorModal, renderOduEditorPanel } from "./odu-editor-ui.js";
import { formatNumber, t } from "../i18n/index.js";

export const ODU_RUNTIME_FREQUENCY_LEVELS = Array.from({ length: 21 }, (_item, index) => index);
export const ODU_RUNTIME_FREQUENCY_MODES = ["cooling", "heating"];

export function getOduRuntimeFrequencyEndpoint(hp, action) {
  const hpIndex = Number(hp) === 2 ? 2 : 1;
  return `${getBasePath()}/openquatt/odu-runtime/hp${hpIndex}/${action}`;
}

export function getOduRuntimeFrequencyHpIndexes() {
  return getInstallationTopology() === "duo" || hasEntity("hp2ExcludeMinHz") ? [1, 2] : [1];
}

export function normalizeOduRuntimeFrequencyStatus(payload = {}, hp = 1) {
  const levelCount = Number(payload.level_count) === 21 ? 21 : 11;
  const normalizeValues = (values) => Array.from({ length: levelCount }, (_item, index) => {
    const value = Number(values?.[index]);
    return Number.isInteger(value) && value >= 0 && value <= 120 ? value : null;
  });
  return {
    available: payload.available !== false,
    unsupported: payload.unsupported === true,
    hp: Number(payload.hp || hp) === 2 ? 2 : 1,
    busy: payload.busy === true,
    loaded: payload.loaded === true,
    armed: payload.armed === true,
    extendedLayout: payload.extended_layout === true,
    levelCount,
    status: String(payload.status || "Ready: load the current compressor frequency table from the ODU"),
    csrfToken: String(payload.csrf_token || ""),
    cooling: normalizeValues(payload.cooling),
    heating: normalizeValues(payload.heating),
  };
}

function getDraftKey(hp, mode, level) {
  return `${Number(hp) === 2 ? 2 : 1}:${mode === "cooling" ? "cooling" : "heating"}:${level}`;
}

export function getOduRuntimeFrequencyStatus(hp) {
  return state.oduRuntimeFrequencyStatuses?.[Number(hp) === 2 ? 2 : 1] || null;
}

function storeStatus(hp, status) {
  state.oduRuntimeFrequencyStatuses = { ...(state.oduRuntimeFrequencyStatuses || {}), [hp]: status };
}

export function getOduRuntimeFrequencyDraftValue(hp, mode, level) {
  const key = getDraftKey(hp, mode, level);
  if (Object.prototype.hasOwnProperty.call(state.oduRuntimeFrequencyDrafts || {}, key)) {
    return state.oduRuntimeFrequencyDrafts[key];
  }
  const value = getOduRuntimeFrequencyStatus(hp)?.[mode]?.[level];
  return Number.isFinite(value) ? String(value) : "";
}

export function updateOduRuntimeFrequencyDraft(input) {
  const hp = Number(input.dataset.oqOduRuntimeHp) === 2 ? 2 : 1;
  const mode = input.dataset.oqOduRuntimeMode === "cooling" ? "cooling" : "heating";
  const level = Number(input.dataset.oqOduRuntimeLevel);
  if (!Number.isInteger(level) || level < 0 || level > 20) return false;
  state.oduRuntimeFrequencyDrafts = {
    ...(state.oduRuntimeFrequencyDrafts || {}),
    [getDraftKey(hp, mode, level)]: String(input.value || ""),
  };
  return true;
}

function hydrateDrafts(status) {
  if (!status?.loaded) return;
  const next = { ...(state.oduRuntimeFrequencyDrafts || {}) };
  ODU_RUNTIME_FREQUENCY_MODES.forEach((mode) => {
    for (let level = 0; level < status.levelCount; level += 1) {
      next[getDraftKey(status.hp, mode, level)] = String(status[mode][level] ?? "");
    }
  });
  state.oduRuntimeFrequencyDrafts = next;
}

async function fetchStatus(hp) {
  const response = await fetchWithTimeout(
    getOduRuntimeFrequencyEndpoint(hp, "status"),
    { cache: "no-store", headers: { "Cache-Control": "no-store" } },
    8000,
    t("oduFrequency.statusTimeout", { hp }),
  );
  if (response.status === 404) {
    return normalizeOduRuntimeFrequencyStatus({ available: false, unsupported: true, hp }, hp);
  }
  if (!response.ok) throw new Error(`HP${hp} status HTTP ${response.status}`);
  return normalizeOduRuntimeFrequencyStatus(await response.json(), hp);
}

export function shouldRefreshOduRuntimeFrequencySurface() {
  return state.systemModal === "odu-frequency-settings";
}

export async function refreshOduRuntimeFrequencyStatuses(options = {}) {
  if (!shouldRefreshOduRuntimeFrequencySurface() && options.force !== true) return false;
  if (state.oduRuntimeFrequencyFetchPromise) return state.oduRuntimeFrequencyFetchPromise;
  const now = Date.now();
  if (!options.force && now - Number(state.oduRuntimeFrequencyLastFetchAt || 0) < 5000) {
    return false;
  }
  const previousSignature = JSON.stringify(state.oduRuntimeFrequencyStatuses || {});
  state.oduRuntimeFrequencyFetchPromise = (async () => {
    try {
      const entries = await Promise.all(getOduRuntimeFrequencyHpIndexes().map(async (hp) => [hp, await fetchStatus(hp)]));
      state.oduRuntimeFrequencyStatuses = Object.fromEntries(entries);
      state.oduRuntimeFrequencyLastFetchAt = Date.now();
      state.oduRuntimeFrequencyError = "";
      const changed = JSON.stringify(state.oduRuntimeFrequencyStatuses) !== previousSignature;
      if (changed && shouldRefreshOduRuntimeFrequencySurface()) render();
      return changed;
    } catch (error) {
      state.oduRuntimeFrequencyError = t("oduFrequency.statusFailed", { error: error.message || String(error) });
      if (!options.silent) render();
      return false;
    } finally {
      state.oduRuntimeFrequencyFetchPromise = null;
    }
  })();
  return state.oduRuntimeFrequencyFetchPromise;
}

function getRequestErrorMessage(error) {
  if (error === "busy") return t("oduFrequency.reqBusy");
  if (error === "load_required") return t("oduFrequency.reqLoad");
  if (error === "arm_required") return t("oduFrequency.reqArm");
  if (error === "invalid_table") return t("oduFrequency.reqInvalid");
  if (error === "forbidden") return t("oduFrequency.reqForbidden");
  return error || t("oduFrequency.reqDefault");
}

async function postAction(hp, action, values = {}) {
  let status = getOduRuntimeFrequencyStatus(hp);
  if (!status?.csrfToken) {
    status = await fetchStatus(hp);
    storeStatus(hp, status);
  }
  if (!status.csrfToken) throw new Error(t("oduFrequency.reqNoCsrf"));

  const body = new URLSearchParams();
  body.set("csrf_token", status.csrfToken);
  Object.entries(values).forEach(([key, value]) => body.set(key, String(value)));
  const response = await fetchWithTimeout(
    getOduRuntimeFrequencyEndpoint(hp, action),
    {
      method: "POST",
      cache: "no-store",
      headers: {
        "Cache-Control": "no-store",
        "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
      },
      body: body.toString(),
    },
    8000,
    t("oduFrequency.reqTimeout", { hp }),
  );
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(getRequestErrorMessage(payload.error || `HTTP ${response.status}`));
  const next = normalizeOduRuntimeFrequencyStatus(payload, hp);
  storeStatus(hp, next);
  return next;
}

async function waitForOperation(hp) {
  const deadline = Date.now() + 65000;
  while (Date.now() < deadline) {
    await new Promise((resolve) => window.setTimeout(resolve, 500));
    const status = await fetchStatus(hp);
    storeStatus(hp, status);
    render();
    if (!status.busy) return status;
  }
  throw new Error(t("oduFrequency.reqLongBusy"));
}

function collectDraftTable(hp) {
  const status = getOduRuntimeFrequencyStatus(hp);
  const count = status?.levelCount === 21 ? 21 : 11;
  const collect = (mode) => Array.from({ length: count }, (_item, level) => {
    const value = Number(String(getOduRuntimeFrequencyDraftValue(hp, mode, level)).replace(",", "."));
    return Number.isInteger(value) ? value : Number.NaN;
  });
  return { cooling: collect("cooling"), heating: collect("heating") };
}

async function runOperation(hp, action) {
  state.busyAction = `odu-runtime-hp${hp}-${action}`;
  state.controlNotice = "";
  state.controlError = "";
  render();
  try {
    let values = {};
    if (action === "apply") {
      const table = collectDraftTable(hp);
      values = { cooling: table.cooling.join(","), heating: table.heating.join(",") };
    }
    let status = await postAction(hp, action, values);
    if (status.busy) status = await waitForOperation(hp);
    if (action === "load" && status.loaded) hydrateDrafts(status);
    const upper = status.status.toUpperCase();
    if (upper.includes("FAILED") || upper.includes("BLOCKED")) {
      throw new Error(status.status);
    }
    state.controlNotice = action === "load"
      ? t("oduFrequency.opLoaded", { hp })
      : t("oduFrequency.opApplied", { hp });
  } catch (error) {
    state.controlError = t("oduFrequency.opFailed", { hp, error: error.message || String(error) });
  } finally {
    state.busyAction = "";
    state.oduRuntimeFrequencyLastFetchAt = 0;
    render();
  }
}

async function toggleArm(hp) {
  const enabled = getOduRuntimeFrequencyStatus(hp)?.armed !== true;
  state.busyAction = `odu-runtime-hp${hp}-arm`;
  state.controlNotice = "";
  state.controlError = "";
  render();
  try {
    await postAction(hp, "arm", { enabled });
    state.controlNotice = t("oduFrequency.armChanged", { hp, state: enabled ? t("oduFrequency.armOn") : t("oduFrequency.armOff") });
  } catch (error) {
    state.controlError = t("oduFrequency.armFailed", { hp, error: error.message || String(error) });
  } finally {
    state.busyAction = "";
    render();
  }
}

export function handleOduRuntimeFrequencyInputKeyDown(event) {
  if (event.key !== "Tab" || event.altKey || event.ctrlKey || event.metaKey) return;
  const input = event.target?.closest?.("input[data-oq-odu-runtime-tab-index]");
  const table = input?.closest(".oq-settings-odu-runtime-table");
  if (!input || !table) return;
  const inputs = Array.from(table.querySelectorAll("input[data-oq-odu-runtime-tab-index]:not(:disabled)"))
    .sort((left, right) => Number(left.dataset.oqOduRuntimeTabIndex || 0) - Number(right.dataset.oqOduRuntimeTabIndex || 0));
  const currentIndex = inputs.indexOf(input);
  const nextInput = inputs[currentIndex + (event.shiftKey ? -1 : 1)];
  if (currentIndex < 0 || !nextInput) return;
  event.preventDefault();
  nextInput.focus();
  nextInput.select?.();
}

const actionHandlers = {
  "odu-runtime-load": (button) => runOperation(Number(button.dataset.hp) === 2 ? 2 : 1, "load"),
  "odu-runtime-arm": (button) => toggleArm(Number(button.dataset.hp) === 2 ? 2 : 1),
  "odu-runtime-apply": (button) => runOperation(Number(button.dataset.hp) === 2 ? 2 : 1, "apply"),
};

export function handleOduRuntimeFrequencyAction(action, button) {
  return invokeActionMap(actionHandlers, action, button);
}

function getOperationState(hp) {
  const devWriteState = __OQ_PREVIEW__ && typeof window !== "undefined"
    ? String(window.__OQ_DEV_ODU_WRITE_STATE__ || "")
    : "";
  const mode = devWriteState === "standby"
    ? "Standby"
    : devWriteState === "running"
      ? "Heating"
      : String(getEntityValue(`hp${hp}Mode`) || "").trim();
  const frequency = devWriteState === "standby"
    ? 0
    : devWriteState === "running"
      ? 30
      : parseLooseNumber(getEntityValue(`hp${hp}Freq`));
  const modeKnown = mode && !/onbekend|unknown/i.test(mode);
  const frequencyKnown = Number.isFinite(frequency);
  const standby = modeKnown && /standby|stand-by/i.test(mode);
  const stopped = frequencyKnown && frequency <= 0.5;
  return {
    safe: standby && stopped,
    copy: !modeKnown
      ? t("oduFrequency.unknownState")
      : !standby
        ? t("oduFrequency.inMode", { mode })
        : !frequencyKnown
          ? t("oduFrequency.unknownFreq")
          : !stopped
            ? t("oduFrequency.runningFreq", { freq: formatNumber(frequency, { maximumFractionDigits: 0 }) })
            : t("oduFrequency.idleSafe"),
  };
}

function getTableValidation(hp) {
  const status = getOduRuntimeFrequencyStatus(hp);
  const levelCount = status?.levelCount === 21 ? 21 : 11;
  const invalid = [];
  ODU_RUNTIME_FREQUENCY_MODES.forEach((mode) => {
    let previous = -1;
    for (let level = 0; level < levelCount; level += 1) {
      const value = Number(String(getOduRuntimeFrequencyDraftValue(hp, mode, level)).replace(",", "."));
      if (!Number.isInteger(value) || value < previous || value > 120 || (level === 0 ? value !== 0 : value < 1)) {
        invalid.push(`${mode === "cooling" ? "C" : "H"}F${level}`);
      }
      if (Number.isFinite(value)) previous = value;
    }
  });
  return { valid: invalid.length === 0, invalid };
}

export function getStatusPresentation(status) {
  const code = String(status || "").toUpperCase();
  if (code.includes("VERIFIED SUCCESSFULLY")) return [t("oduFrequency.verified"), "success"];
  if (code.includes("LOADED")) return [t("oduFrequency.loaded"), "success"];
  if (code.includes("OPERATING MODE UNKNOWN") || code.includes("FREQUENCY UNKNOWN")
    || code.includes("SAFETY CHECK TIMED OUT")) {
    return [t("oduFrequency.unknownSafe"), "warning"];
  }
  if (code.includes("BLOCKED")) return [t("oduFrequency.blocked"), "warning"];
  if (code.includes("FAILED")) return [t("oduFrequency.failed"), "warning"];
  if (code.includes("WRITES ENABLED")) return [t("oduFrequency.writesOn"), ""];
  if (code.includes("WRITES DISABLED")) return [t("oduFrequency.writesOff"), ""];
  if (code.includes("WRITING") || code.includes("READING") || code.includes("CHECKING")
    || code.includes("VERIFYING")) return [t("oduFrequency.checking"), ""];
  return [t("oduFrequency.loadFirst"), ""];
}

function renderFrequencyInput(hp, mode, level, tabIndex) {
  const status = getOduRuntimeFrequencyStatus(hp);
  return renderNumberInputControl({
    value: getOduRuntimeFrequencyDraftValue(hp, mode, level),
    meta: { min: 0, max: 120, step: 1 },
    controlClass: "oq-helper-control oq-settings-odu-runtime-control",
    inputClass: "oq-helper-input oq-helper-input--compact-number oq-settings-odu-runtime-input",
    inputAttributes: `inputmode="numeric" data-oq-odu-runtime-hp="${hp}" data-oq-odu-runtime-mode="${mode}"
        data-oq-odu-runtime-level="${level}" data-oq-odu-runtime-tab-index="${tabIndex}"
        aria-label="${escapeHtml(t("oduFrequency.inputAria", { hp, mode: mode === "cooling" ? t("oduFrequency.inputCooling") : t("oduFrequency.inputHeating"), level }))}"`,
    disabled: !status?.loaded || status.busy,
  });
}

function renderFrequencyTable(hp) {
  const status = getOduRuntimeFrequencyStatus(hp);
  const levels = ODU_RUNTIME_FREQUENCY_LEVELS.slice(0, status?.levelCount === 21 ? 21 : 11);
  return `
    <div class="oq-settings-odu-runtime-table" role="table" aria-label="${escapeHtml(t("oduFrequency.tableAria", { hp }))}">
      <div class="oq-settings-odu-runtime-row oq-settings-odu-runtime-row--head" role="row">
        <span role="columnheader">${escapeHtml(t("oduFrequency.colLevel"))}</span><span role="columnheader">${t("oduFrequency.colCooling")}</span><span role="columnheader">${t("oduFrequency.colHeating")}</span>
      </div>
      ${levels.map((level) => `
        <div class="oq-settings-odu-runtime-row" role="row">
          <span class="oq-settings-odu-runtime-level" role="cell">F${level}</span>
          <div role="cell">${renderFrequencyInput(hp, "cooling", level, level)}</div>
          <div role="cell">${renderFrequencyInput(hp, "heating", level, levels.length + level)}</div>
        </div>`).join("")}
    </div>`;
}

function renderFrequencyPanel(hp) {
  const status = getOduRuntimeFrequencyStatus(hp);
  const operation = getOperationState(hp);
  const validation = getTableValidation(hp);
  const available = status && status.available !== false && !status.unsupported;
  const busy = status?.busy === true || String(state.busyAction || "").startsWith(`odu-runtime-hp${hp}-`);
  const armed = status?.armed === true;
  const [statusLabel, statusTone] = getStatusPresentation(status?.status);
  const applyDisabled = busy || !status?.loaded || !armed || !validation.valid || !operation.safe || !available;
  return renderOduEditorPanel({
    hp, title: t("oduFrequency.panelTitle"), copy: operation.copy,
    actions: renderOduEditorAction(hp, "odu-runtime-load", t("oduFrequency.loadAction"), busy || !available)
      + renderOduEditorAction(hp, "odu-runtime-arm", armed ? t("oduFrequency.armLock") : t("oduFrequency.armRelease"), busy || !status?.loaded || !available)
      + renderOduEditorAction(hp, "odu-runtime-apply", busy ? t("oduFrequency.applyBusy") : t("oduFrequency.applyAction"), applyDisabled, "warning"),
    statusLabel, tone: statusTone,
    body: `
      ${status?.loaded ? renderFrequencyTable(hp) : `<p class="oq-settings-odu-runtime-validation is-warning">${escapeHtml(t("oduFrequency.loadTableFirst"))}</p>`}
      ${!status?.loaded || validation.valid ? "" : `<p class="oq-settings-odu-runtime-validation is-warning">${escapeHtml(t("oduFrequency.checkInvalid", { items: validation.invalid.slice(0, 6).join(", ") }))}</p>`}
    `,
  });
}

export function renderOduRuntimeFrequencyModal() {
  const hpIndexes = getOduRuntimeFrequencyHpIndexes();
  return renderOduEditorModal({
    modalId: "odu-frequency-settings",
    titleId: "oq-odu-frequency-title",
    title: t("oduFrequency.modalTitle"),
    closeLabel: t("oduFrequency.modalClose"),
    warning: `
          <strong>${escapeHtml(t("oduFrequency.modalWarnTitle"))}</strong>
          <p>${escapeHtml(t("oduFrequency.modalWarn1"))}</p>
          <p>${escapeHtml(t("oduFrequency.modalWarn2"))}</p>
          <p>${escapeHtml(t("oduFrequency.modalWarn3"))}</p>`,
    error: state.oduRuntimeFrequencyError,
    panels: hpIndexes.map(renderFrequencyPanel).join(""),
    footer: `<details class="oq-settings-odu-technical"${state.oduRuntimeFrequencyTechnicalDetailsOpen ? " open" : ""}><summary data-oq-action="toggle-odu-frequency-technical-details">${escapeHtml(t("oduFrequency.techTitle"))}</summary><p>${escapeHtml(t("oduFrequency.techCopy"))}</p></details>`,
  });
}
