import { state } from "./state.js";
import { render } from "./render-scheduler.js";
import { updateFirmwareState } from "./feature-state.js";
import { refreshWebAppCache } from "./app-cache.js";

export const DEVICE_RECONNECT_RECOVERY_CLEAR_DELAY_MS = 1500;
export const OTA_REFRESH_DELAY_MS = 1500;
export const RESTART_REFRESH_DELAY_MS = 0;

function getRebootEvidence() {
  const uptime = state.entities.uptime;
  const version = state.entities.projectVersionText;
  return [
    // ESPHome's numeric value is unrounded; the display state is quantized and includes the unit.
    +(uptime?.value ?? uptime?.state) * (String(uptime?.state || uptime?.uom).endsWith("s") ? 1000 : 3600000),
    version?.state || version?.value || "",
  ];
}

function clearBrowserRefresh(refresh) {
  if (refresh.id) {
    window.clearTimeout(refresh.id);
    refresh.id = null;
  }
  refresh.on = false;
  refresh.ok = 0;
  refresh.wait = false;
  refresh.base = null;
}

function armBrowserRefresh(refresh, clearRefresh) {
  clearRefresh();
  refresh.on = true;
  refresh.base = [...getRebootEvidence(), performance.now()];
}

function awaitRebootEvidence(refresh, clearRefresh, timeoutMs) {
  if (!refresh.on) {
    return;
  }
  if (refresh.id) {
    window.clearTimeout(refresh.id);
  }
  refresh.wait = true;
  refresh.id = window.setTimeout(() => {
    refresh.id = null;
    if (refresh.wait) {
      clearRefresh();
    }
  }, timeoutMs);
}

function hasRebootEvidence(refresh, acceptVersionChange = false) {
  if (!refresh.on || !refresh.wait) {
    return false;
  }

  const evidence = getRebootEvidence();
  return evidence[0] < refresh.base[0]
    // A low post-boot uptime proves its boot happened after the initiating request.
    || (isNaN(refresh.base[0]) && evidence[0] + 1000 <= performance.now() - refresh.base[2])
    || refresh.ok === 2
    || (acceptVersionChange && refresh.base[1] && evidence[1] && evidence[1] !== refresh.base[1]);
}

function scheduleBrowserRefresh(refresh, clearRefresh, delayMs, refreshAppCache = false) {
  if (!refresh.on || (refresh.id && !refresh.wait)) {
    return;
  }

  if (refresh.id) {
    window.clearTimeout(refresh.id);
  }
  refresh.wait = false;
  refresh.id = window.setTimeout(async () => {
    if (!refresh.on) {
      return;
    }
    if (refreshAppCache) {
      await refreshWebAppCache();
      if (!refresh.on) {
        return;
      }
    }
    clearRefresh();
    window.location.reload();
  }, delayMs);
}

export function armOtaRefresh() {
  armBrowserRefresh(state.ota, clearOtaRefresh);
}

export function clearOtaRefresh() {
  clearBrowserRefresh(state.ota);
}

export function awaitOtaEvidence(timeoutMs = 300000) {
  awaitRebootEvidence(state.ota, clearOtaRefresh, timeoutMs);
}

export function reconcileOtaEvidence() {
  if (hasRebootEvidence(state.ota, true)) {
    scheduleOtaRefresh();
  }
}

export function scheduleOtaRefresh(delayMs = OTA_REFRESH_DELAY_MS) {
  scheduleBrowserRefresh(state.ota, clearOtaRefresh, delayMs, true);
}

export function armRestartRefresh() {
  armBrowserRefresh(state.restartRefresh, clearRestartRefresh);
}

export function clearRestartRefresh() {
  clearBrowserRefresh(state.restartRefresh);
}

export function awaitRestartEvidence(timeoutMs = 300000) {
  awaitRebootEvidence(state.restartRefresh, clearRestartRefresh, timeoutMs);
}

export function reconcileRestartEvidence() {
  if (hasRebootEvidence(state.restartRefresh)) {
    scheduleRestartRefresh();
  }
}

export function scheduleRestartRefresh(delayMs = RESTART_REFRESH_DELAY_MS) {
  scheduleBrowserRefresh(state.restartRefresh, clearRestartRefresh, delayMs);
}

export function isRestartRefreshActive() {
  return state.restartRefresh.on;
}

export function clearDeviceReconnectRecoveryTimer() {
  if (!state.deviceReconnectRecoveryTimer) {
    return;
  }
  window.clearTimeout(state.deviceReconnectRecoveryTimer);
  state.deviceReconnectRecoveryTimer = null;
}

export function isDeviceReconnectRecovering() {
  return Number(state.deviceReconnectRecoveryStartedAt || 0) > 0;
}

export function getDeviceReconnectPhaseStartedAt() {
  return isDeviceReconnectRecovering()
    ? Number(state.deviceReconnectRecoveryStartedAt || 0)
    : Number(state.deviceReconnectStartedAt || 0);
}

export function getDeviceReconnectStatusLabel() {
  return isDeviceReconnectRecovering() ? "Gegevens verversen" : "Wachten op gegevens";
}

export function getDeviceReconnectStatusCopy() {
  const startedAt = getDeviceReconnectPhaseStartedAt();
  const elapsedSeconds = startedAt > 0 ? Math.max(0, Math.round((Date.now() - startedAt) / 1000)) : 0;
  if (isDeviceReconnectRecovering()) {
    return elapsedSeconds > 0 ? `${elapsedSeconds}s aan het verversen` : "Net weer online";
  }
  return elapsedSeconds > 0 ? `${elapsedSeconds}s bezig` : "Net gestart";
}

export function markDeviceReconnectRecovered() {
  if (!state.deviceReconnectMode || isDeviceReconnectRecovering()) {
    return false;
  }

  clearDeviceReconnectRecoveryTimer();
  state.deviceReconnectRecoveryStartedAt = Date.now();
  state.deviceReconnectLastError = "";
  state.entitySyncFailureCount = 0;
  const recoveryStartedAt = state.deviceReconnectRecoveryStartedAt;
  state.deviceReconnectRecoveryTimer = window.setTimeout(() => {
    if (state.deviceReconnectMode && Number(state.deviceReconnectRecoveryStartedAt || 0) === recoveryStartedAt) {
      clearDeviceReconnect();
      render();
    }
  }, DEVICE_RECONNECT_RECOVERY_CLEAR_DELAY_MS);

  render();
  return true;
}

export function beginDeviceReconnect(mode = "reconnect", error = "") {
  if (!state.deviceReconnectMode) {
    state.deviceReconnectStartedAt = Date.now();
  }
  clearDeviceReconnectRecoveryTimer();
  state.deviceReconnectMode = mode;
  state.deviceReconnectRecoveryStartedAt = 0;
  state.deviceReconnectLastError = error ? String(error) : state.deviceReconnectLastError;
  state.systemModal = "";
  updateFirmwareState({ updateModalOpen: false });
  state.controlError = "";
}

export function clearDeviceReconnect() {
  clearDeviceReconnectRecoveryTimer();
  if (!state.deviceReconnectMode && !state.entitySyncFailureCount) {
    return;
  }
  state.deviceReconnectMode = "";
  state.deviceReconnectStartedAt = 0;
  state.deviceReconnectRecoveryStartedAt = 0;
  state.deviceReconnectLastError = "";
  state.entitySyncFailureCount = 0;
}

export function getDeviceReconnectTitle() {
  if (isDeviceReconnectRecovering()) {
    return "OpenQuatt is weer online";
  }
  if (state.deviceReconnectMode === "ota") {
    return "OpenQuatt wordt bijgewerkt";
  }
  if (state.deviceReconnectMode === "restart") {
    return "OpenQuatt herstart";
  }
  return "Verbinding herstellen";
}

export function getDeviceReconnectCopy() {
  if (isDeviceReconnectRecovering()) {
    if (state.deviceReconnectMode === "ota") {
      return "De update is bijna klaar. We verversen nu de gegevens en het logboek.";
    }
    return "De controller reageert weer. We verversen nu de gegevens en het logboek.";
  }
  if (state.deviceReconnectMode === "ota") {
    return "De controller installeert de update en start daarna opnieuw op. Deze melding verdwijnt zodra de web-app weer gegevens ontvangt.";
  }
  if (state.deviceReconnectMode === "restart") {
    return "De controller start opnieuw op. De web-app probeert automatisch opnieuw verbinding te maken.";
  }
  return "De web-app krijgt tijdelijk geen gegevens van de controller. We proberen automatisch opnieuw te verbinden.";
}
