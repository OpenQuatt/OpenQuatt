import { SETTINGS_GROUP_IDS, SETTINGS_GROUPS } from "./config.js";
import { createDiagnosticsState, createFirmwareState, createHistoryState, createMotionState, createSecurityState, createSettingsState } from "./state-slices.js";

function getStoredDebugRecordingAcknowledgedId() {
  try {
    return String(window.localStorage.getItem("oq-debug-recording-acknowledged-id") || "");
  } catch (_error) {
    return "";
  }
}

export const DEFAULT_TREND_WINDOW_HOURS = 24;
export const TREND_WINDOW_HOURS_OPTIONS = [3, 12, 24, 72, 168, 336, 720];
const QUICK_START_SETUP_INSTALL_STORAGE_KEY = "oq-quickstart-setup-install";
const QUICK_START_SETUP_INSTALL_STATUSES = new Set(["pending", "successful-phase", "complete"]);

export function getStoredQuickStartSetupInstall() {
  try {
    const value = JSON.parse(window.sessionStorage.getItem(QUICK_START_SETUP_INSTALL_STORAGE_KEY) || "null");
    const targetTopology = String(value?.targetTopology || "");
    const targetConnection = String(value?.targetConnection || "");
    const status = String(value?.status || "");
    if (!QUICK_START_SETUP_INSTALL_STATUSES.has(status)
      || !["single", "duo"].includes(targetTopology)
      || !["wifi", "eth"].includes(targetConnection)) {
      return null;
    }
    return {
      status,
      targetTopology,
      targetConnection,
      targetVersion: String(value?.targetVersion || ""),
      startedAt: Number.isFinite(Number(value?.startedAt)) ? Number(value.startedAt) : 0,
    };
  } catch (_error) {
    return null;
  }
}

export function storeQuickStartSetupInstall(record) {
  try {
    window.sessionStorage.setItem(QUICK_START_SETUP_INSTALL_STORAGE_KEY, JSON.stringify(record));
  } catch (_error) {
    // Keep the in-memory install state when storage is unavailable.
  }
}

export function clearQuickStartSetupInstall() {
  try {
    window.sessionStorage.removeItem(QUICK_START_SETUP_INSTALL_STORAGE_KEY);
  } catch (_error) {
    // Keep cleanup best-effort in embedded browsers.
  }
}

const initialQuickStartSetupInstall = getStoredQuickStartSetupInstall();
const initialQuickStartSetupComplete = initialQuickStartSetupInstall?.status === "complete";

export const state = {
  mounted: false,
  root: null,
  nativeApp: null,
  nativeFrontendLoaded: false,
  nativeFrontendLoading: false,
  pollTimer: null,
  supplementaryPrimeTimer: null,
  entitySyncInFlight: false,
  pendingEntitySyncOptions: null,
  lastEntitySyncAttemptAt: 0,
  lastFastEntitySyncAt: 0,
  lastBulkEntitySyncAt: 0,
  lastStaticEntitySyncAt: 0,
  lastAuthStatusRefreshAt: 0,
  loginAuthStatusPollTimer: null,
  lastApiSecurityStatusRefreshAt: 0,
  lastMqttStatusRefreshAt: 0,
  summary: "",
  stage: "Laden...",
  interfacePanelOpen: getStoredInterfacePanelOpen(),
  devPanelOpen: __OQ_PREVIEW__ && getStoredDevPanelOpen(),
  nativeOpen: getStoredSurface() === "native",
  currentStep: initialQuickStartSetupComplete ? "generation" : "setup",
  quickStartModalMode: "wizard",
  settingsGroup: getStoredSettingsGroup(),
  appView: "",
  overviewTheme: getStoredOverviewTheme(),
  hpVisualMode: getStoredHpVisualMode(),
  hpLayoutMode: getStoredHpLayoutMode(),
  ...createHistoryState(getStoredTrendWindowHours()),
  deviceReconnectMode: "",
  deviceReconnectStartedAt: 0,
  deviceReconnectRecoveryStartedAt: 0,
  deviceReconnectRecoveryTimer: null,
  deviceReconnectLastError: "",
  // OTA refresh lifecycle: ok 0=unconfirmed, 1=accepted, 2=accepted with a later outage.
  ota: { on: false, ok: 0, id: null, wait: false, base: null },
  // Restart refresh lifecycle: ok 0=unconfirmed, 1=accepted, 2=outage observed after the request.
  restartRefresh: { on: false, ok: 0, id: null, wait: false, base: null },
  firmwareOtaQuietUntil: 0,
  firmwareOtaQuietTimer: null,
  entitySyncFailureCount: 0,
  lastEntitySyncAt: 0,
  lastEntitySyncSuccessAt: 0,
  lastEntityResponseAt: 0,
  overviewMetadataHydrated: false,
  overviewMetadataHydrating: false,
  busyAction: "",
  controlError: "",
  controlNotice: "",
  ...createDiagnosticsState(getStoredDebugRecordingAcknowledgedId()),
  ...createSettingsState(),
  quickStartSetupUpdateComplete: initialQuickStartSetupComplete,
  updateModalOpen: false,
  systemModal: "",
  ...createSecurityState(),
  ...createFirmwareState(),
  ...createMotionState(getPrefersReducedMotion()),
};

export function restoreStoredQuickStartSetupInstall() {
  const record = getStoredQuickStartSetupInstall();
  if (!record || record.status === "complete") {
    return record;
  }
  state.updateInstallMode = "quickstart-setup";
  state.updateInstallTargetConnection = record.targetConnection;
  state.updateInstallTargetTopology = record.targetTopology;
  state.updateInstallTargetVersion = record.targetVersion;
  state.updateInstallSuccessfulPhaseObserved = record.status === "successful-phase";
  state.updateInstallResumedAfterReload = true;
  state.updateInstallPhaseHint = record.status === "successful-phase" ? "rebooting" : "starting";
  state.updateInstallProgressHint = record.status === "successful-phase" ? 100 : 0;
  state.quickStartSetupDraft = `${record.targetTopology}:${record.targetConnection}`;
  return record;
}

restoreStoredQuickStartSetupInstall();

export function getStoredOverviewTheme() {
  try {
    return window.localStorage.getItem("oq-overview-theme") === "dark" ? "dark" : "light";
  } catch (_error) {
    return "light";
  }
}

export function getStoredInterfacePanelOpen() {
  return false;
}


export function getStoredSurface() {
  try {
    return window.localStorage.getItem("oq-active-surface") === "native" ? "native" : "app";
  } catch (_error) {
    return "app";
  }
}


export function getStoredDevPanelOpen() {
  if (!__OQ_PREVIEW__) {
    return false;
  }
  try {
    return window.localStorage.getItem("oq-dev-panel-open") === "true";
  } catch (_error) {
    return false;
  }
}


export function getStoredSettingsGroup() {
  try {
    const stored = window.localStorage.getItem("oq-settings-group");
    return SETTINGS_GROUP_IDS.has(stored) ? stored : SETTINGS_GROUPS[0].id;
  } catch (_error) {
    return SETTINGS_GROUPS[0].id;
  }
}


export function getStoredHpVisualMode() {
  try {
    return window.localStorage.getItem("oq-hp-visual-mode") === "compact" ? "compact" : "schematic";
  } catch (_error) {
    return "schematic";
  }
}


export function getStoredHpLayoutMode() {
  try {
    const stored = window.localStorage.getItem("oq-hp-layout-mode");
    return stored === "focus-hp1" || stored === "focus-hp2" ? stored : "equal";
  } catch (_error) {
    return "equal";
  }
}


export function getStoredTrendWindowHours() {
  try {
    const stored = Number(window.localStorage.getItem("oq-trend-window-hours"));
    return TREND_WINDOW_HOURS_OPTIONS.includes(stored) ? stored : DEFAULT_TREND_WINDOW_HOURS;
  } catch (_error) {
    return DEFAULT_TREND_WINDOW_HOURS;
  }
}


export function getReducedMotionMedia() {
  if (typeof window === "undefined" || typeof window.matchMedia !== "function") {
    return null;
  }
  try {
    return window.matchMedia("(prefers-reduced-motion: reduce)");
  } catch (_error) {
    return null;
  }
}


export function getPrefersReducedMotion() {
  return Boolean(getReducedMotionMedia()?.matches);
}
