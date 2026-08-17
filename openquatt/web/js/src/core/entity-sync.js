import { getSetupCompleteState, isTrendHistoryEnabled, renderAppSummary } from "./app-shared.js";
import { AUX_RELAY_SETTING_KEYS, AUX_RELAY_STATE_KEYS, BOILER_DIAGNOSTIC_KEYS, BOILER_SETTING_KEYS, BOILER_SUPPORT_SWITCHING_KEYS, BULK_POLL_INTERVAL_MS, CIC_COMPATIBILITY_KEYS, CIC_POLLING_DIAGNOSTIC_KEYS, CIC_POLLING_SETTING_KEYS, COMMISSIONING_STATE_KEYS, COMPRESSOR_SETTING_KEYS, CONNECTIVITY_PROBE_SUCCESS_TTL_MS, CONNECTIVITY_PROBE_TIMEOUT_MS, CONTROL_REPLAY_STATE_KEYS, COOLING_SETTING_KEYS, CURVE_POINTS, CURVE_SETTING_KEYS, ENTITY_DEFS, ENTITY_REFRESH_CONCURRENCY, FAST_OVERVIEW_KEYS, FAST_VIEW_ENTITY_REFRESH_CONCURRENCY, FIRMWARE_ENTITY_KEYS, FIRMWARE_MODAL_KEYS, FLOW_SETTING_KEYS, FLOW_TUNING_KEYS, HEADER_ENTITY_KEYS, HIDDEN_POLL_INTERVAL_MS, INSTALLATION_MONITORING_STATE_KEYS, LIMIT_KEYS, ODU_RUNTIME_FREQUENCY_KEYS, OPENTHERM_DIAGNOSTIC_KEYS, OPENTHERM_SETTING_KEYS, OTB_DIAGNOSTIC_KEYS, OVERVIEW_ENERGY_COLUMN_CONFIGS, OVERVIEW_KEYS, OVERVIEW_METADATA_KEYS, POWER_HOUSE_KEYS, QUICK_START_FLOW_SOURCE_KEYS, QUICK_START_THERMOSTAT_SOURCE_KEYS, SENSOR_CALIBRATION_KEYS, SENSOR_CALIBRATION_STATE_KEYS, SENSOR_SELECTION_KEYS, SENSOR_SELECTION_STATE_KEYS, SERVICE_CONTROL_KEYS, SERVICE_STATUS_ENTITY_KEYS, SETTINGS_GROUP_IDS, SETTINGS_GROUPS, SETTINGS_KEYS, SILENT_SETTING_KEYS, STATIC_POLL_INTERVAL_MS } from "./config.js";
import { buildEntityPath, isCurveMode } from "./domain-helpers.js";
import { getEntityValue, parseLooseNumber } from "./entity-store.js";
import { state } from "./state.js";
import { updateWebServerLogState } from "./feature-state.js";
import { getDefaultAppView, getUrlAppView, setAppView } from "./navigation.js";
import { isFirmwareOtaQuietActive } from "./firmware-quiet.js";
import { getInstallationMonitoringModel, syncInstallationMonitoringDetailsState } from "./installation-monitoring.js";
import { getIncidentMonitoringFailureUpdate, getIncidentMonitoringSuccessUpdate, getIncidentMonitoringUnsupportedUpdate } from "./incident-monitoring.js";
import { beginDeviceReconnect, clearDeviceReconnect, markDeviceReconnectRecovered, reconcileOtaEvidence } from "./device-reconnect.js";
import { getSettingsRenderSignature } from "./render-signatures.js";
import { isSystemSettingsGroupActive } from "./surface-state.js";
import { getHeaderRenderSignature, patchHeaderDom } from "./header-render-controls.js";
import { patchSettingsDom } from "./settings-render-controls.js";
import { ENERGY_HISTORY_VIEW_KEYS, getSettingsStorageRefreshKeys, SETTINGS_STORAGE_KEYS, TREND_HISTORY_VIEW_KEYS } from "./storage-history-keys.js";
import { refreshEnergyHistoryData, refreshSettingsStorageState, refreshTrendHistoryData, refreshTrendHistoryMetadata, shouldRefreshSettingsStorageForCurrentSurface } from "./storage-history-controls.js";
import { patchControlReplayDom, patchDiagnosisDom, patchEnergyDom, patchOverviewDom, patchResultsDom } from "./view-patch-controls.js";
import { clearWebServerLogOutput, closeWebServerLogStream, resetWebServerLogRecoveryState } from "./webserver-log-controls.js";
import { getMqttSensorsModalRenderSignature, refreshMqttStatus, shouldRefreshMqttStatusForCurrentSurface } from "../features/mqtt-actions.js";
import { getApiSecurityStatusSignature, refreshApiSecurityStatus, refreshAuthStatus, shouldRefreshApiSecurityStatusForCurrentSurface, shouldRefreshAuthStatusForCurrentSurface } from "../features/security-actions.js";
import { refreshOduEepromDumpStatuses, shouldRefreshOduEepromDumpSurface } from "../features/odu-eeprom-dump.js";
import { render } from "./render-scheduler.js";
import { fetchWithTimeout } from "./browser-utils.js";

  export function scheduleOverviewPrefetch() {
    if (state.nativeOpen || state.appView !== "settings") {
      return;
    }

    const run = () => {
      if (state.nativeOpen || state.appView !== "settings") {
        return;
      }
      if (state.loadingEntities || state.focusedField || state.draggingCurveKey || state.busyAction || state.settingsInteractionLock) {
        window.setTimeout(scheduleOverviewPrefetch, 250);
        return;
      }
      void syncEntities({ prefetchView: "overview", forceFast: true });
    };

    if (typeof window.requestIdleCallback === "function") {
      window.requestIdleCallback(run, { timeout: 2000 });
      return;
    }

    window.setTimeout(run, 0);
  }

  export async function hydrateOverviewMetadata() {
    if (state.nativeOpen || !isInitialOverviewView() || state.overviewMetadataHydrated || state.overviewMetadataHydrating) {
      return false;
    }

    const keys = getOverviewMetadataHydrationKeys();
    if (!keys.length) {
      state.overviewMetadataHydrated = true;
      return false;
    }

    state.overviewMetadataHydrating = true;
    try {
      await refreshEntities(keys, "all", { concurrency: ENTITY_REFRESH_CONCURRENCY });
      state.overviewMetadataHydrated = true;
      return true;
    } catch (_error) {
      return false;
    } finally {
      state.overviewMetadataHydrating = false;
      if (state.mounted && !state.nativeOpen) {
        render();
      }
    }
  }

  export async function primeDeferredEntities(keys, detail = "state") {
    if (!keys.length || state.nativeOpen) {
      return;
    }

    state.entitySyncInFlight = true;
    try {
      await refreshEntities(keys, detail, {
        concurrency: detail === "all" ? ENTITY_REFRESH_CONCURRENCY : FAST_VIEW_ENTITY_REFRESH_CONCURRENCY,
      });
    } finally {
      state.entitySyncInFlight = false;
      const pendingOptions = state.pendingEntitySyncOptions;
      state.pendingEntitySyncOptions = null;
      if (pendingOptions && !state.nativeOpen) {
        window.setTimeout(() => {
          void syncEntities(pendingOptions);
        }, 0);
      }
    }

    if (state.mounted && !state.nativeOpen) {
      render();
    }
  }

  export async function primeSupplementaryData() {
    if (state.nativeOpen) {
      return;
    }

    try {
      if (isInitialOverviewView()) {
        await hydrateOverviewMetadata();
      }
      if (state.appView === "overview" || state.appView === "control" || state.appView === "diagnosis") {
        await refreshTrendHistoryData({ force: true });
      }
      if (state.appView === "results") {
        await refreshEnergyHistoryData({ force: true });
      }
      await refreshAuthStatus({ force: true });
      if (isSystemSettingsGroupActive()) {
        await Promise.all([
          refreshApiSecurityStatus({ force: true }),
          refreshSettingsStorageState({ forceTrendHistory: true, forceEnergyHistory: true }),
        ]);
      }
    } finally {
      if (state.mounted && !state.nativeOpen) {
        render();
      }
      scheduleOverviewPrefetch();
    }
  }

  export async function primeEntities() {
    if (state.nativeOpen) {
      return;
    }
    state.loadingEntities = true;
    render();
    const loadDelayMs = getDevInitialLoadDelayMs();
    if (loadDelayMs > 0) {
      await new Promise((resolve) => window.setTimeout(resolve, loadDelayMs));
    }
    const initialKeys = getInitialPrimeKeys();
    const deferredKeys = getDeferredPrimeKeys(initialKeys);
    const initialDetail = state.appView === "settings" ? "all" : "state";
    try {
      await Promise.all([
        refreshEntities(initialKeys, initialDetail, {
          concurrency: initialDetail === "all" ? ENTITY_REFRESH_CONCURRENCY : FAST_VIEW_ENTITY_REFRESH_CONCURRENCY,
        }),
        shouldRefreshIncidentMonitoringSurface()
          ? refreshIncidentMonitoringData({ force: true })
          : false,
      ]);
      if (state.appView === "settings") {
        await waitForInitialSettingsReady();
      } else {
        await waitForInitialOverviewReady();
      }
    } finally {
      state.loadingEntities = false;
      render();
    }
    const deferredDetail = state.appView === "settings" ? "all" : "state";
    window.setTimeout(() => {
      void primeDeferredEntities(deferredKeys, deferredDetail);
    }, DEFERRED_ENTITY_PRIME_DELAY_MS);
    schedulePrimeSupplementaryData(getSupplementaryPrimeDelayMs());
  }

  export const INITIAL_SETTINGS_READY_KEY_MAP = {
    installation: [
      "hpGeneration",
      "boilerCvAssistEnabled",
      "boilerRatedHeatPower",
      ...BOILER_SETTING_KEYS,
      "otbLinkAvailable",
      "otbConnectionAutoSelected",
      "otbConnectionMismatch",
      "auxRelayFunction",
      "flowControlMode",
      "flowSetpoint",
      "manualIpwm",
      "silentStartTime",
      "silentEndTime",
      "maxWater",
      "minRuntime",
    ],
    service: [
      "compressorStarts2hWarningLimit",
      "compressorStarts72hWarningLimit",
      "compressorCyclingWarning2h",
      "compressorCyclingWarning72h",
      "alternatingCompressorStartsWarning",
      "boilerFaultFallbackEnabled",
      "commissioningStatus",
      "cm100Active",
    ],
    heating: ["strategy"],
    cooling: ["manualCoolingEnable", "coolingWithoutDewPointMode"],
    integrations: ["otEnabled", "cicPollingEnabled", "flowSource", "boilerConnection", "boilerCommandValid", "otbLinkAvailable"],
    system: ["setupComplete", "projectVersionText", "releaseChannelText", "firmwareUpdateChannel", "statusLedsEnabled", "usageTelemetryEnabled", "usageTelemetryChoiceConfigured", "usageTelemetryInstallationId"],
  };

  export const SETTINGS_GROUP_KEY_MAP = {
    installation: [
      "setupComplete",
      "installationTopology",
      "hpGeneration",
      "boilerCvAssistEnabled",
      "boilerRatedHeatPower",
      ...BOILER_SETTING_KEYS,
      "otbLinkAvailable",
      "otbConnectionAutoSelected",
      "otbConnectionMismatch",
      ...BOILER_SUPPORT_SWITCHING_KEYS,
      ...AUX_RELAY_SETTING_KEYS,
      ...AUX_RELAY_STATE_KEYS,
      ...FLOW_SETTING_KEYS,
      ...FLOW_TUNING_KEYS,
      ...SILENT_SETTING_KEYS,
      ...COMPRESSOR_SETTING_KEYS,
      ...SENSOR_CALIBRATION_KEYS,
      ...SENSOR_CALIBRATION_STATE_KEYS,
      "maxWater",
      ...ODU_RUNTIME_FREQUENCY_KEYS,
    ],
    service: [
      "hpGeneration",
      ...INSTALLATION_MONITORING_STATE_KEYS,
      ...COMMISSIONING_STATE_KEYS,
      ...SENSOR_CALIBRATION_KEYS,
      ...SENSOR_CALIBRATION_STATE_KEYS,
      "boilerCvAssistEnabled",
      "boilerRatedHeatPower",
      "flowSelected",
      "flowKp",
      "flowKi",
      ...SERVICE_CONTROL_KEYS,
    ],
    heating: [
      "strategy",
      ...POWER_HOUSE_KEYS,
      ...CURVE_SETTING_KEYS,
      "dayMax",
      "silentMax",
    ],
    cooling: [
      "manualCoolingEnable",
      "coolingWithoutDewPointMode",
      "coolingDewPointSelected",
      "coolingMinimumSafeSupplyTemp",
      "coolingSupplyTarget",
      "coolingSupplyError",
      "silentModeOverride",
      "silentActive",
      "silentMax",
      ...COOLING_SETTING_KEYS,
    ],
    integrations: [
      ...OPENTHERM_SETTING_KEYS,
      ...OPENTHERM_DIAGNOSTIC_KEYS,
      ...BOILER_SETTING_KEYS,
      ...BOILER_DIAGNOSTIC_KEYS,
      ...OTB_DIAGNOSTIC_KEYS,
      ...CIC_POLLING_SETTING_KEYS,
      ...CIC_POLLING_DIAGNOSTIC_KEYS,
      ...SENSOR_SELECTION_KEYS,
      ...SENSOR_SELECTION_STATE_KEYS,
      "waterSupplyCalibrationOffset",
      "waterSupplyCalibrationRequired",
      "waterSupplyCalibrationStatus",
      ...CIC_COMPATIBILITY_KEYS,
    ],
    system: [
      "setupComplete",
      ...FIRMWARE_ENTITY_KEYS,
      "firmwareUpdateChannel",
      "projectVersionText",
      "releaseChannelText",
      "statusLedsEnabled",
      "usageTelemetryEnabled",
      "usageTelemetryChoiceConfigured",
      "usageTelemetryInstallationId",
      ...SETTINGS_STORAGE_KEYS,
      "webServerLogHistoryEnabled",
      "debugLevel",
    ],
  };

  export function getSettingsRefreshKeys() {
    return [...new Set(["setupComplete", ...SETTINGS_KEYS])];
  }

  export function getDevInitialLoadDelayMs() {
    const raw = __OQ_PREVIEW__ && typeof window !== "undefined" ? Number(window.__OQ_DEV_LOAD_DELAY_MS || 0) : 0;
    return Number.isFinite(raw) && raw > 0 ? raw : 0;
  }

  export const INITIAL_OVERVIEW_READY_KEYS = [
    "strategy",
    "controlModeLabel",
    "openquattEnabled",
    "installationTopology",
    "hpGeneration",
    "totalPower",
    "flowSelected",
    "totalCop",
    "manualCoolingEnable",
    "coolingEnableSource",
    "coolingEnableSelected",
    "coolingEnableEffectiveSource",
    "silentModeOverride",
    "totalHeat",
    "totalCoolingPower",
  ];

  export const INITIAL_OVERVIEW_TEXT_KEYS = ["strategy", "controlModeLabel", "hpGeneration", "coolingEnableEffectiveSource"];

  export const INITIAL_OVERVIEW_NUMERIC_KEYS = ["totalPower", "flowSelected"];

  export const INITIAL_OVERVIEW_THERMAL_KEYS = ["totalHeat", "totalCoolingPower"];

  export const INITIAL_OVERVIEW_READY_TIMEOUT_MS = 2000;

  export const INITIAL_OVERVIEW_READY_POLL_MS = 250;

  export const INITIAL_SETTINGS_READY_TIMEOUT_MS = 5000;

  export const INITIAL_SETTINGS_READY_POLL_MS = 250;

  export function getSettingsGroupHydrationKeys(groupId = state.settingsGroup) {
    const normalized = SETTINGS_GROUP_IDS.has(groupId) ? groupId : SETTINGS_GROUPS[0].id;
    return [...new Set([
      "setupComplete",
      "strategy",
      ...HEADER_ENTITY_KEYS,
      ...(SETTINGS_GROUP_KEY_MAP[normalized] || []),
    ])];
  }

  export function getInitialSettingsReadyKeys() {
    const normalized = SETTINGS_GROUP_IDS.has(state.settingsGroup) ? state.settingsGroup : SETTINGS_GROUPS[0].id;
    return [...new Set(INITIAL_SETTINGS_READY_KEY_MAP[normalized] || INITIAL_SETTINGS_READY_KEY_MAP.installation)];
  }

  export const DEFERRED_ENTITY_PRIME_DELAY_MS = 1200;

  export const SUPPLEMENTARY_PRIME_DELAY_MS = 1800;

  export const HISTORY_VIEW_SUPPLEMENTARY_PRIME_DELAY_MS = 0;

  export const OVERVIEW_BULK_FOLLOWUP_DELAY_MS = 3500;

  export function getPrimeBaseKeys() {
    return ["setupComplete", "strategy", "usageTelemetryEnabled", "usageTelemetryChoiceConfigured", ...HEADER_ENTITY_KEYS];
  }

  export function getEnergyViewEntityKeys() {
    const keys = new Set();
    OVERVIEW_ENERGY_COLUMN_CONFIGS.forEach((column) => {
      if (column.counterResetKey) {
        keys.add(column.counterResetKey);
      }
      (column.categories || []).forEach((category) => {
        (category.groups || []).forEach((group) => {
          (group.rows || []).forEach((row) => {
            const key = Array.isArray(row) ? row[1] : "";
            if (key) {
              keys.add(key);
            }
          });
        });
      });
    });
    return [...keys];
  }

  export function getOverviewLikeHydrationKeys(view, options = {}) {
    const forceFast = options.forceFast === true;
    const includeBulk = options.includeBulk === true;
    if (view === "energy" || view === "results") {
      return [...new Set([
        ...getPrimeBaseKeys(),
        ...(view === "energy" ? INITIAL_OVERVIEW_READY_KEYS : []),
        ...getEnergyViewEntityKeys(),
        ...(view === "results" ? ENERGY_HISTORY_VIEW_KEYS : []),
      ])];
    }
    return [...new Set([
      ...getPrimeBaseKeys(),
      ...(forceFast || !includeBulk ? FAST_OVERVIEW_KEYS : OVERVIEW_KEYS),
      ...(view === "control" ? CONTROL_REPLAY_STATE_KEYS : []),
      ...(view === "diagnosis" ? TREND_HISTORY_VIEW_KEYS : []),
    ])];
  }

  export function getSupplementaryPrimeDelayMs(view = state.appView) {
    return view === "diagnosis" || view === "results" || (view === "settings" && isSystemSettingsGroupActive())
      ? HISTORY_VIEW_SUPPLEMENTARY_PRIME_DELAY_MS
      : SUPPLEMENTARY_PRIME_DELAY_MS;
  }

  export function schedulePrimeSupplementaryData(delayMs = getSupplementaryPrimeDelayMs()) {
    if (state.nativeOpen) {
      return;
    }
    if (state.supplementaryPrimeTimer) {
      window.clearTimeout(state.supplementaryPrimeTimer);
    }
    state.supplementaryPrimeTimer = window.setTimeout(() => {
      state.supplementaryPrimeTimer = null;
      void primeSupplementaryData();
    }, delayMs);
  }

  export function queuePendingEntitySyncOptions(options = {}) {
    const current = state.pendingEntitySyncOptions || {};
    const merged = {
      ...current,
      ...options,
    };
    if (current.forceBulk || options.forceBulk) {
      merged.forceBulk = true;
      merged.forceFast = false;
    } else if (current.forceFast || options.forceFast) {
      merged.forceFast = true;
    }
    state.pendingEntitySyncOptions = merged;
  }

  export function hasUsableEntityValue(key) {
    const value = String(getEntityValue(key) ?? "").trim().toLowerCase();
    return value !== "" && value !== "unknown" && value !== "unavailable" && value !== "nan";
  }

  export function hasUsableNumericEntityValue(key) {
    return hasUsableEntityValue(key) && Number.isFinite(parseLooseNumber(getEntityValue(key)));
  }

  export function isInitialOverviewView() {
    return state.appView === "overview" || state.appView === "diagnosis" || state.appView === "energy";
  }

  export function getOverviewMetadataHydrationKeys() {
    return OVERVIEW_METADATA_KEYS.filter((key) => {
      const entity = state.entities[key];
      if (!entity) {
        return !ENTITY_DEFS[key]?.optional;
      }
      const unit = String(entity.uom ?? entity.unit_of_measurement ?? "").trim();
      return !unit;
    });
  }

  export function isInitialOverviewReady() {
    if (!isInitialOverviewView()) {
      return true;
    }

    return INITIAL_OVERVIEW_TEXT_KEYS.every(hasUsableEntityValue)
      && INITIAL_OVERVIEW_NUMERIC_KEYS.every(hasUsableNumericEntityValue)
      && INITIAL_OVERVIEW_THERMAL_KEYS.some(hasUsableNumericEntityValue);
  }

  export function isInitialSettingsView() {
    return state.appView === "settings";
  }

  export function isInitialSettingsReady() {
    if (!isInitialSettingsView()) {
      return true;
    }

    return getInitialSettingsReadyKeys().every((key) => {
      if (ENTITY_DEFS[key]?.optional && !state.entities[key]) {
        return true;
      }
      return hasUsableEntityValue(key);
    });
  }

  export async function waitForInitialOverviewReady() {
    if (isInitialOverviewReady()) {
      return;
    }

    const deadline = Date.now() + INITIAL_OVERVIEW_READY_TIMEOUT_MS;
    while (!state.nativeOpen && !isInitialOverviewReady() && Date.now() < deadline) {
      await new Promise((resolve) => window.setTimeout(resolve, INITIAL_OVERVIEW_READY_POLL_MS));
      try {
        await refreshEntities(INITIAL_OVERVIEW_READY_KEYS, "state");
      } catch (_error) {
        return;
      }
    }
  }

  export async function waitForInitialSettingsReady() {
    if (isInitialSettingsReady()) {
      return;
    }

    const deadline = Date.now() + INITIAL_SETTINGS_READY_TIMEOUT_MS;
    while (!state.nativeOpen && !isInitialSettingsReady() && Date.now() < deadline) {
      await new Promise((resolve) => window.setTimeout(resolve, INITIAL_SETTINGS_READY_POLL_MS));
      try {
        await refreshEntities(getInitialSettingsReadyKeys(), "all");
      } catch (_error) {
        return;
      }
    }
  }

  export const ENTITY_REQUEST_TIMEOUT_MS = 8000;

  export const RECONNECT_ENTITY_REQUEST_TIMEOUT_MS = 3000;

  export const BULK_ENTITY_ENDPOINT = "/openquatt/entities";

  export const BULK_ENTITY_REQUEST_BODY_MAX_CHARS = 900;

  export const SERVICE_STATUS_ENDPOINT = "/openquatt/service/status";
  export const DECISION_LOG_ENDPOINT = "/openquatt/decision-log";
  export const INCIDENT_MONITORING_ENDPOINT = "/openquatt/incidents";
  export const DECISION_LOG_REFRESH_INTERVAL_MS = 15000;
  export const INCIDENT_MONITORING_REFRESH_INTERVAL_MS = 10000;

  export function getEntityRequestTimeoutMs() {
    return state.deviceReconnectMode || state.busyAction === "restartAction" || state.updateInstallBusy || state.updateInstallPhaseHint
      ? RECONNECT_ENTITY_REQUEST_TIMEOUT_MS
      : ENTITY_REQUEST_TIMEOUT_MS;
  }

  export function isLikelyDeviceConnectionError(message) {
    const normalized = String(message || "").toLowerCase();
    return normalized.includes("failed to fetch")
      || normalized.includes("load failed")
      || normalized.includes("networkerror")
      || normalized.includes("network request failed")
      || normalized.includes("connection refused")
      || normalized.includes("connection reset")
      || normalized.includes("err_connection")
      || normalized.includes("timeout");
  }

  export const OPTIONAL_MISSING_RETRY_INTERVAL_MS = 5 * 60 * 1000;

  export function clearOptionalMissingEntities() {
    if (!state.optionalMissingEntities) {
      return;
    }
    for (const key of Object.keys(state.optionalMissingEntities)) {
      delete state.optionalMissingEntities[key];
    }
  }

  export function noteEntityRefreshSuccess() {
    const now = Date.now();
    state.lastEntitySyncAt = now;
    state.lastEntitySyncSuccessAt = now;
    state.entitySyncFailureCount = 0;
    reconcileOtaEvidence();
    const wasReconnectActive = Boolean(state.deviceReconnectMode);
    const reconnectRecovered = wasReconnectActive && typeof markDeviceReconnectRecovered === "function"
      ? markDeviceReconnectRecovered()
      : false;
    if (reconnectRecovered) {
      state.lastFastEntitySyncAt = 0;
      state.lastBulkEntitySyncAt = 0;
      state.lastStaticEntitySyncAt = 0;
      state.trendHistoryRaw = "";
      state.trendHistoryError = "";
      state.trendHistorySignature = "";
      state.trendHistoryNowMs = Number.NaN;
      state.trendHistoryLastFetchAt = 0;
      state.decisionLog = null;
      state.decisionLogError = "";
      state.decisionLogSignature = "";
      state.decisionLogLastFetchAt = 0;
      state.incidentMonitoringSnapshot = null;
      state.incidentMonitoringError = "";
      state.incidentMonitoringUnsupported = false;
      state.incidentMonitoringFailureCount = 0;
      state.incidentMonitoringSignature = "";
      state.incidentMonitoringRenderPending = false;
      state.incidentMonitoringLastFetchAt = 0;
      state.incidentAction = {
        hp: 0,
        kind: "",
        requestId: 0,
        pending: false,
        ok: null,
        result: "",
      };
      if (typeof resetWebServerLogRecoveryState === "function") {
        resetWebServerLogRecoveryState();
      } else {
        closeWebServerLogStream();
        clearWebServerLogOutput();
        updateWebServerLogState({ webServerLogEnabled: null, webServerLogConnected: false });
      }
      clearOptionalMissingEntities();
    }
  }

  export function noteEntityRefreshFailure(message) {
    if (!isLikelyDeviceConnectionError(message)) {
      state.entitySyncFailureCount = 0;
      clearDeviceReconnect();
      return;
    }
    if (state.ota.ok === 1) {
      state.ota.ok = 2;
    }
    state.entitySyncFailureCount = Number(state.entitySyncFailureCount || 0) + 1;
    state.deviceReconnectLastError = String(message || "");
    if (
      state.deviceReconnectMode
      || state.busyAction === "restartAction"
      || state.updateInstallBusy
      || state.updateInstallPhaseHint
      || state.ota.on
      || state.entitySyncFailureCount >= 2
    ) {
      beginDeviceReconnect(
        state.updateInstallBusy || state.updateInstallPhaseHint || state.ota.on
          ? "ota"
          : state.busyAction === "restartAction" ? "restart" : "reconnect",
        message,
      );
    }
  }

  export function shouldRefreshConnectivityProbe(now = Date.now(), options = {}) {
    if (options.forceProbe === true) {
      return true;
    }
    if (
      state.deviceReconnectMode
      || state.busyAction === "restartAction"
      || state.updateInstallBusy
      || state.updateInstallPhaseHint
      || Number(state.entitySyncFailureCount || 0) > 0
    ) {
      return true;
    }

    const lastSuccessAt = Number(state.lastEntitySyncSuccessAt || state.lastEntitySyncAt || state.lastEntityResponseAt || 0);
    if (!lastSuccessAt) {
      return true;
    }

    const ttlMs = document.hidden ? HIDDEN_POLL_INTERVAL_MS : CONNECTIVITY_PROBE_SUCCESS_TTL_MS;
    return (now - lastSuccessAt) >= ttlMs;
  }

  export async function refreshConnectivityProbe() {
    const entity = ENTITY_DEFS.status || ENTITY_DEFS.setupComplete;
    if (!entity) {
      return { ok: true, message: "" };
    }
    const timeoutMs = state.deviceReconnectMode ? RECONNECT_ENTITY_REQUEST_TIMEOUT_MS : CONNECTIVITY_PROBE_TIMEOUT_MS;
    const url = buildEntityPath(entity.domain, entity.name);

    try {
      const response = await fetchWithTimeout(
        url,
        { cache: "no-store" },
        timeoutMs,
        `${entity.name} request timed out after ${timeoutMs}ms`,
      );
      state.lastEntityResponseAt = Date.now();
      return {
        ok: response.ok || response.status === 404,
        message: response.ok || response.status === 404 ? "" : `${entity.name} HTTP ${response.status}`,
      };
    } catch (error) {
      return { ok: false, message: error.message || String(error) };
    }
  }

  export function isKnownOptionalMissingEntity(key, now = Date.now()) {
    const missingAt = state.optionalMissingEntities?.[key];
    if (!ENTITY_DEFS[key]?.optional || !missingAt) {
      return false;
    }
    return (now - Number(missingAt)) < OPTIONAL_MISSING_RETRY_INTERVAL_MS;
  }

  export function markOptionalMissingEntity(key, now = Date.now()) {
    if (!state.optionalMissingEntities) {
      return;
    }
    state.optionalMissingEntities[key] = now;
  }

  export function getBulkEntityLine(key) {
    const entity = ENTITY_DEFS[key];
    return entity ? `${key}\t${entity.domain}\t${entity.name}` : "";
  }

  export function buildBulkEntityRequestBody(lines, detail) {
    const params = new URLSearchParams();
    params.set("detail", detail === "all" ? "all" : "state");
    params.set("entities", lines.join("\n"));
    return params.toString();
  }

  export function buildBulkEntityChunks(keys, detail) {
    const chunks = [];
    let currentKeys = [];
    let currentLines = [];
    let currentBody = "";

    keys.forEach((key) => {
      const line = getBulkEntityLine(key);
      if (!line) {
        return;
      }

      const nextLines = [...currentLines, line];
      const nextBody = buildBulkEntityRequestBody(nextLines, detail);
      if (currentLines.length && nextBody.length > BULK_ENTITY_REQUEST_BODY_MAX_CHARS) {
        chunks.push({ keys: currentKeys, body: currentBody });
        currentKeys = [key];
        currentLines = [line];
        currentBody = buildBulkEntityRequestBody(currentLines, detail);
        return;
      }

      currentKeys = [...currentKeys, key];
      currentLines = nextLines;
      currentBody = nextBody;
    });

    if (currentLines.length) {
      chunks.push({ keys: currentKeys, body: currentBody });
    }

    return chunks;
  }

  export async function fetchBulkEntityChunk(chunk) {
    const timeoutMs = getEntityRequestTimeoutMs();
    const fetchOptions = {
      method: "POST",
      cache: "no-store",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: chunk.body,
    };

    const response = await fetchWithTimeout(
      BULK_ENTITY_ENDPOINT,
      fetchOptions,
      timeoutMs,
      `bulk entities request timed out after ${timeoutMs}ms`,
    );
    if (!response.ok) {
      throw new Error(`bulk entities HTTP ${response.status}`);
    }
    return response.json();
  }

  export async function fetchServiceStatusPayload() {
    const timeoutMs = getEntityRequestTimeoutMs();
    const fetchOptions = { cache: "no-store", headers: { "Cache-Control": "no-store" } };

    const response = await fetchWithTimeout(
      SERVICE_STATUS_ENDPOINT,
      fetchOptions,
      timeoutMs,
      `service status request timed out after ${timeoutMs}ms`,
    );
    if (!response.ok) {
      throw new Error(`service status HTTP ${response.status}`);
    }
    return response.json();
  }

  export async function fetchDecisionLogPayload() {
    const timeoutMs = getEntityRequestTimeoutMs();
    const fetchOptions = { cache: "no-store", headers: { "Cache-Control": "no-store" } };

    const response = await fetchWithTimeout(
      DECISION_LOG_ENDPOINT,
      fetchOptions,
      timeoutMs,
      `decision log request timed out after ${timeoutMs}ms`,
    );
    if (!response.ok) {
      throw new Error(`decision log HTTP ${response.status}`);
    }
    return response.json();
  }

  async function fetchIncidentMonitoringPayload() {
    const timeoutMs = getEntityRequestTimeoutMs();
    const response = await fetchWithTimeout(
      INCIDENT_MONITORING_ENDPOINT,
      { cache: "no-store", headers: { "Cache-Control": "no-store" } },
      timeoutMs,
      `incident monitoring request timed out after ${timeoutMs}ms`,
    );
    if (response.status === 404) {
      return { unsupported: true, payload: null };
    }
    if (!response.ok) {
      throw new Error(`incident monitoring HTTP ${response.status}`);
    }
    return { unsupported: false, payload: await response.json() };
  }

  function shouldRefreshIncidentMonitoringSurface(appView = state.appView, settingsGroup = state.settingsGroup) {
    return appView === "overview" || (appView === "settings" && settingsGroup === "service");
  }

  export async function refreshIncidentMonitoringData(options = {}) {
    if (!shouldRefreshIncidentMonitoringSurface() && options.prefetchOverview !== true) {
      return false;
    }
    const force = options.force === true;
    const now = Date.now();
    if (state.incidentMonitoringFetchPromise) {
      return state.incidentMonitoringFetchPromise;
    }
    if (!force && state.incidentMonitoringUnsupported) {
      return false;
    }
    if (!force && (now - Number(state.incidentMonitoringLastFetchAt || 0)) < INCIDENT_MONITORING_REFRESH_INTERVAL_MS) {
      return false;
    }

    const applyUpdate = (update) => {
      Object.assign(state, update);
      if (update.changed) syncInstallationMonitoringDetailsState(getInstallationMonitoringModel());
      return update.changed;
    };
    const request = (async () => {
      try {
        const result = await fetchIncidentMonitoringPayload();
        return applyUpdate(result.unsupported
          ? getIncidentMonitoringUnsupportedUpdate(state)
          : getIncidentMonitoringSuccessUpdate(state, result.payload));
      } catch (error) {
        return applyUpdate(getIncidentMonitoringFailureUpdate(state, error));
      }
    })();
    state.incidentMonitoringFetchPromise = request;

    try {
      return await request;
    } finally {
      if (state.incidentMonitoringFetchPromise === request) {
        state.incidentMonitoringFetchPromise = null;
      }
    }
  }

  export function getDecisionLogSignature(payload = {}) {
    const events = Array.isArray(payload.events) ? payload.events : [];
    const buckets = Array.isArray(payload.buckets) ? payload.buckets : [];
    const firstEvent = events[0] || {};
    const lastEvent = events[events.length - 1] || {};
    const lastBucket = buckets[buckets.length - 1] || {};
    return [
      payload?.meta?.event_count ?? events.length,
      payload?.meta?.dropped_count ?? 0,
      events.length,
      firstEvent.seq ?? "",
      lastEvent.seq ?? "",
      lastEvent.uptime_s ?? "",
      payload?.meta?.boot_epoch_s ?? "",
      firstEvent.epoch_s ?? "",
      lastEvent.epoch_s ?? "",
      buckets.length,
      lastBucket.hour_start_uptime_s ?? "",
      lastBucket.hour_start_epoch_s ?? "",
      lastBucket.source ?? "",
      lastBucket.attention_count ?? "",
      payload?.meta?.flash_write_count ?? "",
    ].join("|");
  }

  export async function refreshDecisionLogData(options = {}) {
    if (state.appView !== "control") {
      return false;
    }
    const force = options.force === true;
    const now = Date.now();
    if (!force && state.decisionLogFetchPromise) {
      return state.decisionLogFetchPromise;
    }
    if (!force && (state.decisionLog || state.decisionLogError) &&
        (now - Number(state.decisionLogLastFetchAt || 0)) < DECISION_LOG_REFRESH_INTERVAL_MS) {
      return false;
    }

    state.decisionLogFetchPromise = (async () => {
      const payload = await fetchDecisionLogPayload();
      const events = Array.isArray(payload?.events) ? payload.events : [];
      if (!payload?.ok || !Array.isArray(payload?.events)) {
        throw new Error("decision log response mist events");
      }
      const signature = getDecisionLogSignature(payload);
      const changed = state.decisionLogError !== "" || state.decisionLogSignature !== signature || !state.decisionLog;
      state.decisionLog = {
        ...payload,
        events,
        buckets: Array.isArray(payload.buckets) ? payload.buckets : [],
      };
      state.decisionLogError = "";
      state.decisionLogSignature = signature;
      state.decisionLogLastFetchAt = Date.now();
      return changed;
    })();

    try {
      return await state.decisionLogFetchPromise;
    } catch (error) {
      const nextError = error.message || String(error);
      const changed = Boolean(state.decisionLog) || state.decisionLogError !== nextError;
      state.decisionLog = null;
      state.decisionLogError = nextError;
      state.decisionLogSignature = "";
      state.decisionLogLastFetchAt = Date.now();
      return changed;
    } finally {
      state.decisionLogFetchPromise = null;
    }
  }

  export function mergeServiceStatusPayload(payload = {}) {
    const entities = payload?.entities && typeof payload.entities === "object" ? payload.entities : {};
    Object.entries(entities).forEach(([key, entity]) => {
      if (!SERVICE_STATUS_ENTITY_KEYS.has(key)) {
        return;
      }
      if (state.optionalMissingEntities) {
        delete state.optionalMissingEntities[key];
      }
      state.entities[key] = mergeEntityPayload(key, state.entities[key], entity);
    });
  }

  export async function refreshServiceStatusEntities(keys) {
    if (!keys.length) {
      return { ok: true, message: "" };
    }
    try {
      const payload = await fetchServiceStatusPayload();
      mergeServiceStatusPayload(payload);
      state.lastEntityResponseAt = Date.now();
      return { ok: true, message: "" };
    } catch (error) {
      return { ok: false, message: error.message || String(error) };
    }
  }

  export async function refreshEntities(keys, detail = "state", options = {}) {
    const now = Date.now();
    const forceMissing = options.forceMissing === true;
    const refreshKeys = keys.filter((key) =>
      forceMissing || SERVICE_STATUS_ENTITY_KEYS.has(key) || !isKnownOptionalMissingEntity(key, now)
    );
    if (!refreshKeys.length) {
      return;
    }
    const serviceStatusKeys = refreshKeys.filter((key) => SERVICE_STATUS_ENTITY_KEYS.has(key));
    const regularKeys = refreshKeys.filter((key) => !SERVICE_STATUS_ENTITY_KEYS.has(key));
    const serviceStatusResult = await refreshServiceStatusEntities(serviceStatusKeys);
    const entityRefreshKeys = serviceStatusResult.ok
      ? regularKeys
      : [...regularKeys, ...serviceStatusKeys];
    if (!entityRefreshKeys.length) {
      applyDerivedState();
      syncInstallationMonitoringDetailsState(getInstallationMonitoringModel());
      if (!state.busyAction) {
        noteEntityRefreshSuccess();
        state.controlError = "";
      }
      return;
    }

    const requestedConcurrency = Number(options.concurrency);
    const concurrency = Number.isFinite(requestedConcurrency) && requestedConcurrency > 0
      ? Math.floor(requestedConcurrency)
      : ENTITY_REFRESH_CONCURRENCY;
    const chunks = buildBulkEntityChunks(entityRefreshKeys, detail);
    const chunkConcurrency = Math.max(1, Math.min(concurrency, ENTITY_REFRESH_CONCURRENCY));
    const results = [];
    for (let index = 0; index < chunks.length; index += chunkConcurrency) {
      const batch = chunks.slice(index, index + chunkConcurrency);
      const batchResults = await Promise.allSettled(
        batch.map(async (chunk) => ({ chunk, payload: await fetchBulkEntityChunk(chunk) }))
      );
      results.push(...batchResults);
    }

    if (results.some((result) => result.status === "fulfilled")) {
      state.lastEntityResponseAt = Date.now();
    }

    let firstError = "";
    results.forEach((result) => {
      if (result.status !== "fulfilled") {
        const message = result.reason.message || String(result.reason);
        if (!firstError) {
          firstError = message;
        }
        return;
      }

      const { chunk, payload } = result.value;
      const entities = payload?.entities && typeof payload.entities === "object" ? payload.entities : {};
      const missing = new Set(Array.isArray(payload?.missing) ? payload.missing : []);

      chunk.keys.forEach((key) => {
        if (Object.prototype.hasOwnProperty.call(entities, key)) {
          if (state.optionalMissingEntities) {
            delete state.optionalMissingEntities[key];
          }
          state.entities[key] = mergeEntityPayload(key, state.entities[key], entities[key]);
          return;
        }

        const entity = ENTITY_DEFS[key];
        if (entity?.optional) {
          if (missing.has(key)) {
            markOptionalMissingEntity(key, now);
          }
        } else if (!firstError) {
          firstError = `${entity?.name || key} ontbreekt in bulk response`;
        }
      });
    });

    applyDerivedState();
    syncInstallationMonitoringDetailsState(getInstallationMonitoringModel());
    if (firstError) {
      noteEntityRefreshFailure(firstError);
      if (state.deviceReconnectMode) {
        state.controlError = "";
      } else {
        state.controlError = `Niet alle helpervelden konden worden ververst. ${firstError}`;
      }
    } else if (!state.busyAction) {
      noteEntityRefreshSuccess();
      state.controlError = "";
    }
  }

  export function mergeEntityPayload(key, previous = {}, payload = {}) {
    const next = {
      ...(previous || {}),
      ...(payload || {}),
    };
    const isSelect = ENTITY_DEFS[key]?.domain === "select";
    if (!isSelect) {
      return next;
    }

    if (!String(payload.state ?? "").trim() && String(previous?.state ?? "").trim()) {
      next.state = previous.state;
    }
    if (!String(payload.value ?? "").trim() && String(previous?.value ?? "").trim()) {
      next.value = previous.value;
    }
    if (!Array.isArray(payload.option) && Array.isArray(previous?.option)) {
      next.option = previous.option;
    }
    if (!Array.isArray(payload.options) && Array.isArray(previous?.options)) {
      next.options = previous.options;
    }
    return next;
  }

  export function applyDerivedState() {
    state.complete = getSetupCompleteState();
    state.stage = state.complete === true ? "Gereed" : state.complete === false ? "Quick Start" : "Laden...";
    state.summary = renderAppSummary();
    if (state.appView === "diagnosis" && !isTrendHistoryEnabled()) {
      setAppView(getDefaultAppView(), { syncMode: "replace", forceSync: true });
    }
    if (!state.appView) {
      setAppView(getUrlAppView() || getDefaultAppView(), { syncMode: "replace", forceSync: true });
    }
  }

  export function getInitialPrimeKeys() {
    const base = getPrimeBaseKeys();
    if (state.appView === "settings") {
      return [...new Set([...base, ...getSettingsGroupHydrationKeys()])];
    }
    if (state.appView === "energy" || state.appView === "results") {
      return getOverviewLikeHydrationKeys(state.appView, { forceFast: true });
    }
    if (state.appView === "overview" || state.appView === "diagnosis") {
      return getOverviewLikeHydrationKeys(state.appView, { forceFast: true });
    }
    return [...new Set(base)];
  }

  export function getDeferredPrimeKeys(initialKeys = []) {
    const initial = new Set(initialKeys);
    const fullKeys = state.appView === "settings"
      ? getSettingsGroupHydrationKeys()
      : state.appView === "overview" || state.appView === "control" || state.appView === "diagnosis"
        ? [...new Set([...getOverviewLikeHydrationKeys(state.appView, { includeBulk: true }), ...FIRMWARE_ENTITY_KEYS])]
        : state.appView === "energy" || state.appView === "results"
        ? [...new Set([...getOverviewLikeHydrationKeys(state.appView, { forceFast: true }), ...FIRMWARE_ENTITY_KEYS])]
        : [...new Set(["setupComplete", "strategy", ...HEADER_ENTITY_KEYS])];
    return fullKeys.filter((key) => !initial.has(key));
  }

  export function isBulkEntitySyncDue(now = Date.now(), options = {}) {
    if (options.forceBulk === true) {
      return true;
    }
    return (now - Number(state.lastBulkEntitySyncAt || 0)) >= BULK_POLL_INTERVAL_MS;
  }

  export async function syncEntities(options = {}) {
    if (state.nativeOpen || state.loadingEntities || state.draggingCurveKey || state.busyAction || state.settingsInteractionLock) {
      return;
    }
    if (state.updateInstallBusy || isFirmwareOtaQuietActive()) {
      state.pendingEntitySyncOptions = null;
      return;
    }
    if (state.focusedField && state.appView !== "settings") {
      return;
    }
    if (state.entitySyncInFlight) {
      queuePendingEntitySyncOptions(options);
      return;
    }

    const now = Date.now();
    if (document.hidden && (now - Number(state.lastEntitySyncAttemptAt || 0)) < HIDDEN_POLL_INTERVAL_MS) {
      return;
    }

    const appView = state.appView;
    const isPrefetchOverview = options.prefetchView === "overview" && !options.forceBulk && appView === "settings";
    const syncView = isPrefetchOverview ? "overview" : appView;
    const isOverviewLike = syncView === "overview" || syncView === "control" || syncView === "diagnosis" ||
      syncView === "energy" || syncView === "results";
    const forceFast = options.forceFast === true && !options.forceBulk;
    const isBulkDue = !forceFast && !isPrefetchOverview && isBulkEntitySyncDue(now, options);
    const isStaticDue = (now - Number(state.lastStaticEntitySyncAt || 0)) >= STATIC_POLL_INTERVAL_MS;
    const staticKeys = isStaticDue || state.updateInstallBusy || state.updateInstallPhaseHint
      ? FIRMWARE_ENTITY_KEYS
      : [];
    const quickStartFlowSourceKeys = state.quickStartModalOpen && state.currentStep === "flow-source"
      ? QUICK_START_FLOW_SOURCE_KEYS
      : [];
    const quickStartSetupKeys = state.quickStartModalOpen && state.currentStep === "setup"
      ? FIRMWARE_MODAL_KEYS
      : [];
    const quickStartThermostatSourceKeys = state.quickStartModalOpen && state.currentStep === "thermostat-source"
      ? QUICK_START_THERMOSTAT_SOURCE_KEYS
      : [];
    const settingsStorageKeys = shouldRefreshSettingsStorageForCurrentSurface()
      ? getSettingsStorageRefreshKeys()
      : [];
    const keys = isPrefetchOverview
      ? [
          ...FAST_OVERVIEW_KEYS,
          ...HEADER_ENTITY_KEYS,
          "setupComplete",
          ...staticKeys,
        ]
      : isOverviewLike
      ? [
          ...getOverviewLikeHydrationKeys(syncView, { forceFast, includeBulk: isBulkDue }),
          ...staticKeys,
        ]
      : appView === "settings"
        ? [...new Set([...getSettingsGroupHydrationKeys(), ...settingsStorageKeys, ...staticKeys])]
        : isBulkDue
          ? [
              "setupComplete",
              ...staticKeys,
              ...HEADER_ENTITY_KEYS,
              "strategy",
              ...LIMIT_KEYS,
              ...FLOW_SETTING_KEYS,
              ...(isCurveMode() ? CURVE_POINTS.map((point) => point.key) : POWER_HOUSE_KEYS),
            ]
          : ["setupComplete", ...HEADER_ENTITY_KEYS, "strategy", ...staticKeys];

    state.entitySyncInFlight = true;
    state.lastEntitySyncAttemptAt = now;
    try {
      const reconnectModeBefore = state.deviceReconnectMode;
      const probe = shouldRefreshConnectivityProbe(now, options)
        ? await refreshConnectivityProbe()
        : { ok: true, message: "" };
      if (!probe.ok) {
        noteEntityRefreshFailure(probe.message);
        if (!isPrefetchOverview) {
          render();
        }
        return;
      }
      await refreshEntities([...new Set([...keys, ...(state.ota.wait ? ["uptime", "projectVersionText"] : []), ...quickStartSetupKeys, ...quickStartFlowSourceKeys, ...quickStartThermostatSourceKeys])], isPrefetchOverview ? "state" : appView === "settings" || quickStartSetupKeys.length ? "all" : "state", {
        concurrency: forceFast && isOverviewLike ? FAST_VIEW_ENTITY_REFRESH_CONCURRENCY : ENTITY_REFRESH_CONCURRENCY,
      });
      state.lastFastEntitySyncAt = Date.now();
      if (isBulkDue && (syncView === "overview" || syncView === "control" || syncView === "diagnosis") && !isPrefetchOverview) {
        state.lastBulkEntitySyncAt = state.lastFastEntitySyncAt;
      }
      if (staticKeys.length) {
        state.lastStaticEntitySyncAt = state.lastFastEntitySyncAt;
      }
      if (isPrefetchOverview) {
        await refreshIncidentMonitoringData({ prefetchOverview: true });
        return;
      }
      if (isOverviewLike && !state.overviewMetadataHydrated && !state.overviewMetadataHydrating) {
        void hydrateOverviewMetadata();
      }
      const reconnectChanged = reconnectModeBefore !== state.deviceReconnectMode;
      const incidentMonitoringChanged = shouldRefreshIncidentMonitoringSurface(syncView, state.settingsGroup)
        ? await refreshIncidentMonitoringData({ force: options.forceIncidentMonitoring === true })
        : false;
      const shouldDeferSupplementary = forceFast && isOverviewLike;
      const trendChanged = shouldDeferSupplementary
        ? false
        : syncView === "overview" || syncView === "control" || syncView === "diagnosis"
          ? await refreshTrendHistoryData()
          : false;
      const energyHistoryChanged = shouldDeferSupplementary
        ? false
        : state.appView === "results"
          ? await refreshEnergyHistoryData()
          : false;
      const decisionLogChanged = syncView === "control"
        ? await refreshDecisionLogData({ force: options.forceDecisionLog === true })
        : false;
      const settingsStorageChanged = shouldDeferSupplementary
        ? false
        : shouldRefreshSettingsStorageForCurrentSurface()
          ? (await Promise.all([
              refreshTrendHistoryMetadata(),
              refreshEnergyHistoryData({ metaOnly: true }),
            ])).some(Boolean)
          : false;
      const authChanged = shouldDeferSupplementary || !shouldRefreshAuthStatusForCurrentSurface() ? false : await refreshAuthStatus();
      const apiSecurityChanged = shouldDeferSupplementary || !shouldRefreshApiSecurityStatusForCurrentSurface() ? false : await refreshApiSecurityStatus();
      const mqttChanged = shouldDeferSupplementary || !shouldRefreshMqttStatusForCurrentSurface()
        ? false
        : await refreshMqttStatus({ force: state.systemModal === "mqtt-sensors" });
      if (!shouldDeferSupplementary && shouldRefreshOduEepromDumpSurface()) {
        await refreshOduEepromDumpStatuses();
      }
      const nextHeaderSignature = getHeaderRenderSignature();
      if (shouldDeferSupplementary && !state.nativeOpen) {
        schedulePrimeSupplementaryData(getSupplementaryPrimeDelayMs(syncView));
      }
      if (reconnectChanged) {
        render();
        return;
      }
      if (incidentMonitoringChanged && state.appView === "settings" && state.settingsGroup === "service") {
        if (state.focusedField) {
          state.incidentMonitoringRenderPending = true;
        } else {
          state.incidentMonitoringRenderPending = false;
          render();
        }
        return;
      }
      if (trendChanged && state.appView === "diagnosis" && !state.root?.querySelector(".oq-overview-trends")) {
        render();
        return;
      }
      if (energyHistoryChanged && state.appView === "results" && !state.root?.querySelector(".oq-energy-history")) {
        render();
        return;
      }
      if (decisionLogChanged && state.appView === "control") {
        if (!patchControlReplayDom()) {
          render();
        }
        return;
      }
      if (settingsStorageChanged && state.appView === "settings") {
        render();
        return;
      }
      if (authChanged && (state.systemModal === "login" || isSystemSettingsGroupActive())) {
        render();
        return;
      }
      if (apiSecurityChanged && state.appView === "settings") {
        render();
        return;
      }
      if (state.systemModal === "mqtt-sensors") {
        const nextMqttSensorsSignature = getMqttSensorsModalRenderSignature();
        if (nextMqttSensorsSignature !== state.mqttSensorsModalRenderSignature) {
          render();
        }
        return;
      }
      if (mqttChanged && state.appView === "settings") {
        render();
        return;
      }
      const hasOpenOverlay = Boolean(state.updateModalOpen || state.systemModal || state.interfacePanelOpen);
      if (nextHeaderSignature !== state.headerRenderSignature) {
        if (hasOpenOverlay && patchHeaderDom()) {
          state.headerRenderSignature = nextHeaderSignature;
        } else {
          render();
          return;
        }
      } else {
        patchHeaderDom();
      }
      if (state.appView === "settings" && (String(state.systemModal || "").startsWith("service-task-") || state.systemModal === "water-sensor-corrections")) {
        const nextSettingsSignature = getSettingsRenderSignature();
        if (nextSettingsSignature !== state.settingsRenderSignature) {
          render();
        }
        return;
      }
      if (hasOpenOverlay) {
        return;
      }
      if (state.appView === "settings") {
        const nextSettingsSignature = getSettingsRenderSignature();
        if (nextSettingsSignature !== state.settingsRenderSignature) {
          if (!state.focusedField) {
            render();
            return;
          }
        }
        if (!patchSettingsDom()) {
          if (!state.focusedField) {
            render();
          }
          return;
        }
        if (state.focusedField) {
          state.settingsRenderSignature = nextSettingsSignature;
        }
        return;
      }
      if (state.appView === "diagnosis") {
        if (!patchDiagnosisDom()) {
          render();
        }
        return;
      }
      if (state.appView === "control") {
        if (!patchControlReplayDom()) {
          render();
        }
        return;
      }
      if (state.appView === "energy") {
        if (!patchEnergyDom()) {
          render();
        }
        return;
      }
      if (state.appView === "results") {
        if (!patchResultsDom()) {
          render();
        }
        return;
      }
      if (!patchOverviewDom()) {
        render();
      }
    } catch (error) {
      if (!isPrefetchOverview) {
        state.controlError = `Helperstatus kon niet worden geladen. ${error.message}`;
        render();
      }
    } finally {
      state.entitySyncInFlight = false;
      const pendingOptions = state.pendingEntitySyncOptions;
      state.pendingEntitySyncOptions = null;
      if (pendingOptions && !state.nativeOpen) {
        window.setTimeout(() => {
          void syncEntities(pendingOptions);
        }, 0);
      }
      if (forceFast && (syncView === "overview" || syncView === "control" || syncView === "diagnosis") && !isPrefetchOverview && !state.nativeOpen && !pendingOptions && isBulkEntitySyncDue(Date.now())) {
        window.setTimeout(() => {
          void syncEntities({ forceBulk: true });
        }, OVERVIEW_BULK_FOLLOWUP_DELAY_MS);
      }
    }
  }
