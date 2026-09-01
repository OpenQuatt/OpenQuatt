import { hasEntity } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { fetchWithTimeout } from "../core/browser-utils.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { getInstallationTopology } from "./device-context.js";

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
    status: String(payload.status || "READY: load ODU runtime table"),
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
    `HP${hp} status reageert niet`,
  );
  if (response.status === 404) {
    return normalizeOduRuntimeFrequencyStatus({ available: false, unsupported: true, hp }, hp);
  }
  if (!response.ok) throw new Error(`HP${hp} status HTTP ${response.status}`);
  return normalizeOduRuntimeFrequencyStatus(await response.json(), hp);
}

export function shouldRefreshOduRuntimeFrequencySurface() {
  return state.appView === "settings"
    && state.settingsGroup === "installation"
    && state.oduRuntimeFrequencyDetailsOpen;
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
      state.oduRuntimeFrequencyError = `Status mislukt. ${error.message || String(error)}`;
      if (!options.silent) render();
      return false;
    } finally {
      state.oduRuntimeFrequencyFetchPromise = null;
    }
  })();
  return state.oduRuntimeFrequencyFetchPromise;
}

function getRequestErrorMessage(error) {
  if (error === "busy") return "ODU of Modbus-bus is bezig";
  if (error === "load_required") return "laad de actuele ODU-tabel eerst";
  if (error === "arm_required") return "geef runtime writes eerst vrij";
  if (error === "invalid_table") return "de frequentietabel is ongeldig";
  if (error === "forbidden") return "de beveiligingscontrole is verlopen; laad de pagina opnieuw";
  return error || "actie geweigerd";
}

async function postAction(hp, action, values = {}) {
  let status = getOduRuntimeFrequencyStatus(hp);
  if (!status?.csrfToken) {
    status = await fetchStatus(hp);
    storeStatus(hp, status);
  }
  if (!status.csrfToken) throw new Error("CSRF ontbreekt");

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
    `HP${hp} actie reageert niet`,
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
  throw new Error("firmware bleef langer dan 65 seconden bezig");
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
      ? `HP${hp}: runtime tabel geladen.`
      : `HP${hp}: runtime tabel via readback bevestigd.`;
  } catch (error) {
    state.controlError = `HP${hp} runtime-actie mislukt. ${error.message || String(error)}`;
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
    state.controlNotice = `HP${hp} runtime writes ${enabled ? "vrijgegeven" : "vergrendeld"}.`;
  } catch (error) {
    state.controlError = `HP${hp} write-lock aanpassen mislukt. ${error.message || String(error)}`;
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
