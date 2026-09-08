import { hasEntity } from "../core/app-shared.js";
import { downloadJsonFile, fetchWithTimeout } from "../core/browser-utils.js";
import { isCurveMode } from "../core/domain-helpers.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { normalizeHouseLearningExport } from "../settings/house-learning-chart.js";

export const HOUSE_LEARNING_STATUS_INTERVAL_MS = 10000;

const numberOrNull = (value) => value === null || value === undefined || value === "" || !Number.isFinite(Number(value))
  ? null
  : Number(value);
const stringList = (value) => Array.isArray(value)
  ? value.filter((item) => typeof item === "string" && item.trim()).map((item) => item.trim()).slice(0, 24)
  : [];
const collectionPhase = (collection, prefix) => {
  const keys = ["active", "elapsed_s", "target_s", "intervals"].map((suffix) => `${prefix}_${suffix}`);
  if (!keys.some((key) => Object.hasOwn(collection, key))) return null;
  return {
    active: collection[`${prefix}_active`] === true,
    elapsedSeconds: numberOrNull(collection[`${prefix}_elapsed_s`]),
    targetSeconds: numberOrNull(collection[`${prefix}_target_s`]),
    intervals: numberOrNull(collection[`${prefix}_intervals`]),
  };
};

export function normalizeHouseLearningStatus(payload = {}) {
  if (Number(payload.schema) !== 1 || payload.mode !== "passive") throw new Error("onbekend statusformaat");
  const source = (key) => {
    const item = payload.sources?.[key];
    return item && typeof item === "object"
      ? { route: String(item.route || "unknown"), valid: item.valid === true }
      : { route: "unknown", valid: false };
  };
  return {
    enabled: payload.enabled === true,
    controlMode: numberOrNull(payload.control_mode),
    storageReady: payload.storage_ready === true,
    status: String(payload.status || "unknown"),
    sourceStatus: String(payload.source_status || "unknown"),
    modelValidationStatus: String(payload.model_validation_status || "unknown"),
    journalStatus: String(payload.journal_status || "unknown"),
    invalidReasons: stringList(payload.invalid_reasons),
    records: Math.max(0, Math.trunc(numberOrNull(payload.records) || 0)),
    batchAdviceReady: payload.batch_advice_ready === true,
    adviceReady: payload.advice_ready === true,
    hBatch: numberOrNull(payload.h_batch),
    t0Batch: numberOrNull(payload.t0_batch),
    uRls: numberOrNull(payload.u_rls),
    cRlsWhPerK: numberOrNull(payload.c_rls_wh_per_k),
    rlsSamples: Math.max(0, Math.trunc(numberOrNull(payload.rls_samples) || 0)),
    rlsReady: payload.rls_ready === true,
    rlsReadinessReasons: stringList(payload.rls_readiness_reasons),
    tickEpoch: numberOrNull(payload.tick_epoch),
    blockedReasons: stringList(payload.blocked_reasons),
    sources: Object.fromEntries(["room", "setpoint", "outside", "flow"].map((key) => [key, source(key)])),
    collection: payload.collection && typeof payload.collection === "object"
      ? {
        batch: collectionPhase(payload.collection, "batch"),
        thermal: collectionPhase(payload.collection, "thermal"),
      }
      : null,
  };
}

export function shouldRefreshHouseLearningStatusSurface() {
  return !document.hidden && state.appView === "settings" && state.settingsGroup === "heating"
    && !isCurveMode() && hasEntity("houseLearningEnabled");
}

export const getHouseLearningStatusEndpoint = () => `${getBasePath()}/openquatt/learning/status`;
export const getHouseLearningExportEndpoint = () => `${getBasePath()}/openquatt/learning/export`;

export async function loadHouseLearningChart() {
  if (state.houseLearningChartLoading || state.busyAction || state.houseLearningReset === "pending") return false;
  const requestId = Number(state.houseLearningChartRequestId || 0) + 1;
  state.houseLearningChartRequestId = requestId;
  state.houseLearningChartLoading = true;
  state.houseLearningChartError = "";
  render();
  try {
    const response = await fetchWithTimeout(
      getHouseLearningExportEndpoint(),
      { cache: "no-store", headers: { "Cache-Control": "no-store" } },
      8000,
      "leerdata timeout",
    );
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const records = normalizeHouseLearningExport(await response.json());
    if (requestId !== state.houseLearningChartRequestId) return false;
    state.houseLearningChart = records;
    state.houseLearningChartFetchedAt = Date.now();
    return true;
  } catch (error) {
    if (requestId !== state.houseLearningChartRequestId) return false;
    state.houseLearningChart = null;
    state.houseLearningChartFetchedAt = 0;
    state.houseLearningChartError = `Meetgegevens konden niet worden geladen. ${error.message || String(error)}`;
    return false;
  } finally {
    if (requestId === state.houseLearningChartRequestId) {
      state.houseLearningChartLoading = false;
      render();
    }
  }
}

export async function downloadHouseLearningExport() {
  if (state.busyAction) return;
  state.busyAction = "houseLearningExport";
  state.houseLearningStatusError = "";
  render();
  try {
    const response = await fetchWithTimeout(
      getHouseLearningExportEndpoint(),
      { cache: "no-store", headers: { "Cache-Control": "no-store" } },
      8000,
      "Leerdata reageert niet",
    );
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    downloadJsonFile("openquatt-house-learning.json", await response.json());
  } catch (error) {
    state.houseLearningStatusError = error.message || String(error);
  } finally {
    state.busyAction = "";
    render();
  }
}

export function handleHouseLearningAction(action, button, pressNamedButton) {
  if (action === "download-house-learning") {
    void downloadHouseLearningExport();
    return true;
  }
  if (action === "load-house-learning-chart") {
    void loadHouseLearningChart();
    return true;
  }
  if (action === "press-named-button" && button?.dataset.oqButtonKey === "houseLearningReset") {
    void resetHouseLearningData(pressNamedButton);
    return true;
  }
  return false;
}

export async function refreshHouseLearningStatus(options = {}) {
  const resetReconcile = options.resetReconcile === true;
  if (!resetReconcile && !shouldRefreshHouseLearningStatusSurface()) return false;
  if (state.houseLearningReset === "pending" && !resetReconcile) return false;
  if (state.houseLearningFetchPromise) return state.houseLearningFetchPromise;
  if (!options.force && Date.now() - Number(state.houseLearningLastFetchAt || 0) < HOUSE_LEARNING_STATUS_INTERVAL_MS) {
    return false;
  }
  const initialCapabilityCheck = !state.houseLearningEndpointAvailable && !state.houseLearningEndpointChecked;
  const requestGeneration = Number(state.houseLearningRequestId || 0);
  const before = JSON.stringify([state.houseLearningEndpointAvailable, state.houseLearningEndpointChecked, state.houseLearningStatus, state.houseLearningStatusError]);
  state.houseLearningEndpointChecking = true;
  state.houseLearningFetchPromise = (async () => {
    let probeCompleted = false;
    try {
      const response = await fetchWithTimeout(
        getHouseLearningStatusEndpoint(),
        { cache: "no-store", headers: { "Cache-Control": "no-store" } },
        8000,
        "leerstatus timeout",
      );
      if ((!resetReconcile && !shouldRefreshHouseLearningStatusSurface()) || requestGeneration !== state.houseLearningRequestId) return false;
      if (response.status === 404) {
        state.houseLearningEndpointAvailable = false;
        state.houseLearningStatus = null;
        state.houseLearningStatusError = "";
      } else {
        state.houseLearningEndpointAvailable = true;
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const status = normalizeHouseLearningStatus(await response.json());
        if ((!resetReconcile && !shouldRefreshHouseLearningStatusSurface()) || requestGeneration !== state.houseLearningRequestId) return false;
        state.houseLearningStatus = status;
        if (status.records === 0 && status.journalStatus === "cleared") {
          state.houseLearningChartRequestId = Number(state.houseLearningChartRequestId || 0) + 1;
          state.houseLearningChart = null;
          state.houseLearningChartLoading = false;
          state.houseLearningChartError = "";
          state.houseLearningChartFetchedAt = 0;
        }
        state.houseLearningStatusError = "";
      }
      probeCompleted = true;
    } catch (error) {
      if ((!resetReconcile && !shouldRefreshHouseLearningStatusSurface()) || requestGeneration !== state.houseLearningRequestId) return false;
      state.houseLearningStatus = null;
      state.houseLearningStatusError = `Leerstatus kon niet worden opgehaald. ${error.message || String(error)}`;
      probeCompleted = true;
    } finally {
      if (requestGeneration === state.houseLearningRequestId) {
        if (probeCompleted) state.houseLearningEndpointChecked = true;
        state.houseLearningEndpointChecking = false;
        state.houseLearningLastFetchAt = Date.now();
      }
    }
    const changed = before !== JSON.stringify([state.houseLearningEndpointAvailable, state.houseLearningEndpointChecked, state.houseLearningStatus, state.houseLearningStatusError]);
    // The initial probe must replace its loading panel immediately. Later status
    // polls retain the settings partial-patch path while the panel is present.
    if (changed && !state.focusedField && (initialCapabilityCheck || !state.root?.querySelector("[data-oq-house-learning]"))) render();
    return changed;
  })();
  if (initialCapabilityCheck) render();
  try {
    return await state.houseLearningFetchPromise;
  } finally {
    state.houseLearningFetchPromise = null;
  }
}

export function isHouseLearningResetConfirmed(status = state.houseLearningStatus) {
  return Boolean(status) && status.enabled === false && status.status === "paused"
    && status.records === 0 && status.journalStatus === "cleared";
}

export async function resetHouseLearningData(pressNamedButton, options = {}) {
  if (state.houseLearningReset === "pending" || state.busyAction) return false;
  const confirmReset = options.confirmReset || (() => window.confirm(
    "Leerdata wissen?\n\nAlle leerhistorie wordt verwijderd en passief leren wordt gepauzeerd.",
  ));
  if (!confirmReset()) return false;

  const staleRequest = state.houseLearningFetchPromise;
  state.houseLearningRequestId = Number(state.houseLearningRequestId || 0) + 1;
  state.houseLearningChartRequestId = Number(state.houseLearningChartRequestId || 0) + 1;
  state.houseLearningChart = null;
  state.houseLearningChartError = "";
  state.houseLearningChartFetchedAt = 0;
  state.houseLearningChartLoading = false;
  state.houseLearningReset = "pending";
  state.houseLearningResetError = "";
  state.houseLearningStatusError = "";
  state.houseLearningStatus = null;
  render();

  await pressNamedButton("houseLearningReset");
  if (state.controlError) {
    state.houseLearningReset = "error";
    state.houseLearningResetError = state.controlError;
    render();
    return false;
  }

  if (staleRequest) await staleRequest.catch(() => false);
  const timerHost = options.timerHost || window;
  const pollDelays = options.pollDelays || [0, 1000, 3000, 7000];
  for (const delayMs of pollDelays) {
    if (delayMs) await new Promise((resolve) => timerHost.setTimeout(resolve, delayMs));
    await refreshHouseLearningStatus({ force: true, resetReconcile: true });
    if (isHouseLearningResetConfirmed()) {
      state.houseLearningReset = "";
      state.houseLearningResetError = "";
      state.controlNotice = "Leerdata gewist; passief leren is gepauzeerd.";
      render();
      return true;
    }
  }

  state.houseLearningReset = "uncertain";
  state.houseLearningResetError = "Reset geaccepteerd, maar wissen is nog niet bevestigd. De opdracht is niet herhaald.";
  state.controlNotice = "";
  render();
  return false;
}
