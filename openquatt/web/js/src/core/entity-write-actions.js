import { hasEntity } from "./app-shared.js";
import { CURVE_POINTS, ENTITY_DEFS, FIRMWARE_ENTITY_KEYS, FLOW_SETTING_KEYS, getOduRuntimeFrequencyButtonHp, getOduRuntimeFrequencyHpKeys, HEADER_ENTITY_KEYS, LIMIT_KEYS, ODU_RUNTIME_FREQUENCY_BUTTON_KEYS, OPENQUATT_RESUME_CLEAR_VALUE, OVERVIEW_KEYS, POWER_HOUSE_KEYS, QUICK_STEPS } from "./config.js";
import { armRestartRefresh, awaitRestartEvidence, beginDeviceReconnect, clearRestartRefresh } from "./device-reconnect.js";
import { buildEntityPath, isCurveMode } from "./domain-helpers.js";
import { formatOpenQuattResumeDateTime, getEntityValue, normalizeDateTimeValue, normalizeNumber, normalizeTimeValue, parseLooseNumber, toDateTimeInputValue } from "./entity-store.js";
import { getSettingsRefreshKeys, isLikelyDeviceConnectionError, refreshEntities, refreshIncidentMonitoringData, syncEntities } from "./entity-sync.js";
import {
  createIncidentActionRequestId,
  postIncidentActionRequest,
} from "./incident-monitoring.js";
import { setAppView } from "./navigation.js";
import { render } from "./render-scheduler.js";
import { clearQuickStartSetupInstall, state } from "./state.js";
import { pollFirmwareUpdateState, primeFirmwareUpdateState } from "../features/firmware-update.js";
import { updateFirmwareState } from "./feature-state.js";
import { stopLoginAuthStatusPolling } from "../features/security-actions.js";
import { refreshSettingsStorageStateSoon, SETTINGS_STORAGE_KEYS } from "../features/storage-history.js";
import { clearWebServerLogOutput, refreshWebServerLogHistory } from "../features/webserver-logs.js";
import { waitForUsageTelemetryChoiceConfirmation } from "./usage-telemetry-domain.js";

async function commitUsageTelemetrySwitch(entity, enabled) {
  const key = "usageTelemetryEnabled";
  const confirmChoice = (expectedEnabled) => waitForUsageTelemetryChoiceConfirmation({
    refresh: async () => {
      await refreshEntities([key, "usageTelemetryChoiceConfigured", "usageTelemetryInstallationId"], "all");
      return [getEntityValue(key), getEntityValue("usageTelemetryChoiceConfigured")];
    },
    expectedEnabled,
  });
  const previousEntity = state.entities[key] ? { ...state.entities[key] } : null;
  state.busyAction = `switch-${key}`;
  state.controlNotice = "";
  state.controlError = "";
  render();

  try {
    const action = enabled ? "turn_on" : "turn_off";
    const response = await fetch(buildEntityPath(entity.domain, entity.name, action), { method: "POST" });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    if (!await confirmChoice(enabled)) {
      throw new Error("de controller heeft de opgeslagen keuze niet bevestigd");
    }
    state.controlError = "";
    state.controlNotice = `${entity.name} ${enabled ? "ingeschakeld" : "uitgeschakeld"}.`;
  } catch (error) {
    let disabledConfirmed = false;
    try {
      const response = await fetch(buildEntityPath(entity.domain, entity.name, "turn_off"), { method: "POST" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      disabledConfirmed = await confirmChoice(false);
    } catch (_disableError) {
      // Report an unknown state instead of presenting an unverified privacy choice.
    }
    if (disabledConfirmed) {
      state.controlError = "";
      state.controlNotice = enabled
        ? "Inschakelen kon niet worden bevestigd. Delen is veilig uitgeschakeld."
        : "Delen is uitgeschakeld.";
    } else {
      if (previousEntity) {
        state.entities[key] = previousEntity;
      } else {
        delete state.entities[key];
      }
      state.controlError = `De keuze kon niet veilig worden bevestigd. Controleer de verbinding en probeer opnieuw (${error.message}).`;
    }
  } finally {
    state.busyAction = "";
    render();
  }
}

export async function commitSelect(key, option) {
  const entity = ENTITY_DEFS[key];
  const previousEntity = state.entities[key] ? { ...state.entities[key] } : null;
  const verifyControlModeOverride = key === "controlModeOverride";
  state.busyAction = `save-${key}`;
  state.controlNotice = "";
  state.controlError = "";
  if (!verifyControlModeOverride) {
    state.entities[key] = {
      ...(state.entities[key] || {}),
      state: option,
      value: option,
    };
  }
  render();

  try {
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?option=${encodeURIComponent(option)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    if (verifyControlModeOverride) {
      let confirmationPayload = null;
      try {
        const confirmationResponse = await fetch(buildEntityPath(entity.domain, entity.name), {
          cache: "no-store",
        });
        if (!confirmationResponse.ok) {
          throw new Error(`HTTP ${confirmationResponse.status}`);
        }
        confirmationPayload = await confirmationResponse.json();
      } catch (error) {
        const uncertainValue = option === "Auto"
          ? String(previousEntity?.value ?? previousEntity?.state ?? "Force CM0")
          : option;
        state.entities[key] = {
          ...(previousEntity || {}),
          state: uncertainValue,
          value: uncertainValue,
        };
        throw new Error(`de controllerstatus kon niet worden bevestigd (${error.message})`);
      }
      const confirmedValue = String(confirmationPayload?.value ?? confirmationPayload?.state ?? "");
      state.entities[key] = {
        ...(previousEntity || {}),
        ...(confirmationPayload || {}),
      };
      if (confirmedValue !== option) {
        throw new Error(`de controller meldt nog "${confirmedValue || "onbekend"}"`);
      }
    }
    delete state.drafts[key];
    delete state.inputDrafts[key];
    state.controlNotice = verifyControlModeOverride
      ? option === "Auto"
        ? "De normale moduskeuze is weer actief."
        : `${option} is tijdelijk actief en verloopt automatisch na maximaal 30 minuten.`
      : `${entity.name} bijgewerkt.`;
    if (key === "firmwareUpdateChannel") {
      updateFirmwareState({ updateInstallCompleted: false, updateInstallCompletedVersion: "" });
      state.entities.firmwareUpdateChannel = {
        ...(state.entities.firmwareUpdateChannel || {}),
        state: option,
        value: option,
      };
      primeFirmwareUpdateState(option);
      render();
      await pollFirmwareUpdateState();
      state.controlNotice = "Releasekanaal bijgewerkt.";
    } else if (key === "debugLevel") {
      state.controlNotice = "Logger level bijgewerkt.";
      if (state.systemModal === "webserver-logs") {
        void refreshWebServerLogHistory();
      }
    } else if (key === "webServerLogHistoryEnabled") {
      const selectedEnabled = ["1", "on", "true"].includes(String(option).toLowerCase());
      if (selectedEnabled) {
        state.webServerLogHistoryLoaded = false;
        void refreshWebServerLogHistory();
      } else {
        clearWebServerLogOutput();
      }
      if (state.systemModal === "webserver-logs") {
        render();
      }
    } else if (state.appView === "settings") {
      await refreshEntities(getSettingsRefreshKeys(), "all");
    } else {
      await refreshEntities(["setupComplete", "strategy", "openquattEnabled", "manualCoolingEnable", "silentModeOverride", ...FLOW_SETTING_KEYS, ...LIMIT_KEYS], "state");
    }
    if (key === "strategy" && state.appView !== "settings") {
      await refreshEntities(isCurveMode(option) ? CURVE_POINTS.map((point) => point.key) : POWER_HOUSE_KEYS, "state");
    }
    return true;
  } catch (error) {
    if (!verifyControlModeOverride && previousEntity) {
      state.entities[key] = previousEntity;
    }
    state.controlError = `${entity.name} kon niet worden bijgewerkt. ${error.message}`;
    return false;
  } finally {
    state.busyAction = "";
    render();
  }
}

export function getNumberSettingValidationError(key, value, entities = state.entities) {
  const normalized = parseLooseNumber(value);
  if (!Number.isFinite(normalized)) {
    return "";
  }
  if (key === "boilerSupportStartThreshold") {
    const stopThreshold = parseLooseNumber(entities.boilerSupportStopThreshold?.value ?? entities.boilerSupportStopThreshold?.state);
    if (Number.isFinite(stopThreshold) && normalized <= stopThreshold) {
      return `De startgrens moet hoger zijn dan de stopgrens (${stopThreshold} W).`;
    }
  }
  if (key === "boilerSupportStopThreshold") {
    const startThreshold = parseLooseNumber(entities.boilerSupportStartThreshold?.value ?? entities.boilerSupportStartThreshold?.state);
    if (Number.isFinite(startThreshold) && normalized >= startThreshold) {
      return `De stopgrens moet lager zijn dan de startgrens (${startThreshold} W).`;
    }
  }
  const exclusionBoundary = key.match(/^hp[12]Exclude(Min|Max)Hz$/);
  if (exclusionBoundary && normalized > 0) {
    const isMinimum = exclusionBoundary[1] === "Min";
    const pairedKey = key.replace(isMinimum ? "MinHz" : "MaxHz", isMinimum ? "MaxHz" : "MinHz");
    const pairedValue = parseLooseNumber(entities[pairedKey]?.value ?? entities[pairedKey]?.state);
    if (Number.isFinite(pairedValue) && pairedValue > 0) {
      if (isMinimum && normalized > pairedValue) {
        return `De ondergrens mag niet hoger zijn dan de bovengrens (${pairedValue} Hz).`;
      }
      if (!isMinimum && normalized < pairedValue) {
        return `De bovengrens mag niet lager zijn dan de ondergrens (${pairedValue} Hz).`;
      }
    }
  }
  return "";
}

export async function commitSwitch(key, enabled) {
  const entity = ENTITY_DEFS[key];
  if (!entity) {
    return;
  }
  if (key === "usageTelemetryEnabled") {
    await commitUsageTelemetrySwitch(entity, enabled);
    return;
  }

  state.busyAction = `switch-${key}`;
  state.controlNotice = "";
  state.controlError = "";
  render();

  try {
    const action = enabled ? "turn_on" : "turn_off";
    const response = await fetch(buildEntityPath(entity.domain, entity.name, action), { method: "POST" });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    state.entities[key] = {
      ...(state.entities[key] || {}),
      value: enabled,
      state: enabled,
    };
    state.controlNotice = `${entity.name} ${enabled ? "ingeschakeld" : "uitgeschakeld"}.`;
    state.busyAction = "";
    if (state.appView === "overview") {
      await refreshEntities([...OVERVIEW_KEYS, ...HEADER_ENTITY_KEYS, "setupComplete", ...FIRMWARE_ENTITY_KEYS], "state");
    } else if (state.appView === "settings") {
      await refreshEntities(getSettingsRefreshKeys(), "all");
      if (SETTINGS_STORAGE_KEYS.includes(key)) {
        refreshSettingsStorageStateSoon();
      }
    } else {
      await refreshEntities(["setupComplete", "strategy", "openquattEnabled", "manualCoolingEnable", "silentModeOverride", ...FLOW_SETTING_KEYS, ...LIMIT_KEYS], "state");
    }
    if (key === "webServerLogHistoryEnabled") {
      if (enabled) {
        state.webServerLogHistoryLoaded = false;
        void refreshWebServerLogHistory();
      } else {
        clearWebServerLogOutput();
      }
      if (state.systemModal === "webserver-logs") {
        render();
      }
    }
    render();
  } catch (error) {
    state.controlError = `${entity.name} aanpassen mislukt (${error.message}).`;
    render();
  } finally {
    state.busyAction = "";
    render();
  }
}

export async function commitNumber(key, value, successNotice = "") {
  const entity = ENTITY_DEFS[key];
  const normalized = normalizeNumber(key, value);
  const validationError = getNumberSettingValidationError(key, normalized);
  if (validationError) {
    state.controlNotice = "";
    state.controlError = validationError;
    state.inputDrafts[key] = String(value ?? "");
    state.drafts[key] = normalized;
    render();
    return false;
  }
  state.busyAction = `save-${key}`;
  state.controlNotice = "";
  state.controlError = "";
  state.inputDrafts[key] = String(value ?? "");
  state.drafts[key] = normalized;
  render();

  let succeeded = false;
  try {
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?value=${encodeURIComponent(normalized)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    delete state.drafts[key];
    delete state.inputDrafts[key];
    succeeded = true;
    state.controlNotice = successNotice || `${entity.name} bijgewerkt.`;
    await refreshEntities(
      state.appView === "settings"
        ? getSettingsRefreshKeys()
        : [...new Set([key, "setupComplete", "strategy", ...FLOW_SETTING_KEYS, ...LIMIT_KEYS])]
      ,
      "state"
    );
  } catch (error) {
    state.inputDrafts[key] = String(normalized).replace(".", ",");
    state.controlError = `${entity.name} kon niet worden bijgewerkt. ${error.message}`;
  } finally {
    state.busyAction = "";
    render();
  }
  return succeeded;
}

export async function disableRange(minKey, maxKey) {
  const keys = [minKey, maxKey];
  state.inputDrafts[minKey] = state.inputDrafts[maxKey] = "0";
  render();
  const minStored = await commitNumber(minKey, 0);
  const firstError = state.controlError;
  const maxStored = await commitNumber(maxKey, 0);
  const writeError = firstError || state.controlError;
  keys.forEach((key) => {
    delete state.drafts[key];
    delete state.inputDrafts[key];
  });

  let valuesConfirmed = false;
  let verificationError = "";
  try {
    await refreshEntities(keys, "all");
    valuesConfirmed = keys.every((key) => Number(getEntityValue(key)) === 0);
  } catch (error) {
    verificationError = error.message;
  }
  if (minStored && maxStored && valuesConfirmed) {
    state.controlNotice = "Frequentie-uitsluiting uitgeschakeld.";
    state.controlError = "";
    render();
    return true;
  }

  state.controlNotice = "";
  state.controlError = writeError || verificationError || "Frequentie-uitsluiting kon niet volledig worden uitgeschakeld of bevestigd.";
  render();
  return false;
}

export async function commitTime(key, value) {
  const entity = ENTITY_DEFS[key];
  const normalized = normalizeTimeValue(value);
  state.busyAction = `save-${key}`;
  state.controlNotice = "";
  state.controlError = "";
  render();

  try {
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?value=${encodeURIComponent(normalized)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    state.controlNotice = `${entity.name} bijgewerkt.`;
    await refreshEntities(
      state.appView === "settings"
        ? getSettingsRefreshKeys()
        : [key, "setupComplete"],
      "state"
    );
  } catch (error) {
    state.controlError = `${entity.name} kon niet worden bijgewerkt. ${error.message}`;
  } finally {
    state.busyAction = "";
    render();
  }
}

export async function commitText(key, value) {
  const entity = ENTITY_DEFS[key];
  const normalized = String(value || "").trim();
  state.busyAction = `save-${key}`;
  state.controlNotice = "";
  state.controlError = "";
  state.inputDrafts[key] = String(value ?? "");
  state.drafts[key] = normalized;
  render();

  try {
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?value=${encodeURIComponent(normalized)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    state.entities[key] = {
      ...(state.entities[key] || {}),
      value: normalized,
      state: normalized,
    };
    delete state.drafts[key];
    delete state.inputDrafts[key];
    state.controlNotice = `${entity.name} bijgewerkt.`;
    await refreshEntities(
      state.appView === "settings"
        ? getSettingsRefreshKeys()
        : [key, "setupComplete"],
      "state"
    );
  } catch (error) {
    state.inputDrafts[key] = normalized;
    state.controlError = `${entity.name} kon niet worden bijgewerkt. ${error.message}`;
  } finally {
    state.busyAction = "";
    render();
  }
}

export async function postDateTimeValue(key, value) {
  const entity = ENTITY_DEFS[key];
  const normalized = normalizeDateTimeValue(value) || OPENQUATT_RESUME_CLEAR_VALUE;
  const response = await fetch(
    `${buildEntityPath(entity.domain, entity.name, "set")}?value=${encodeURIComponent(normalized)}`,
    { method: "POST" }
  );
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  state.entities[key] = {
    ...(state.entities[key] || {}),
    value: normalized,
    state: normalized,
  };
  return normalized;
}

export async function postSwitchState(key, enabled) {
  const entity = ENTITY_DEFS[key];
  const action = enabled ? "turn_on" : "turn_off";
  const response = await fetch(buildEntityPath(entity.domain, entity.name, action), { method: "POST" });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  state.entities[key] = {
    ...(state.entities[key] || {}),
    value: enabled,
    state: enabled,
  };
  return enabled;
}

export async function refreshOpenQuattControlState() {
  await refreshEntities(
    [...new Set([...OVERVIEW_KEYS, ...HEADER_ENTITY_KEYS, "setupComplete", ...FIRMWARE_ENTITY_KEYS])],
    "state"
  );
}

export async function commitDateTime(key, value) {
  const entity = ENTITY_DEFS[key];
  const normalized = normalizeDateTimeValue(value);
  state.busyAction = `save-${key}`;
  state.controlNotice = "";
  state.controlError = "";
  render();

  try {
    await postDateTimeValue(key, normalized);
    state.controlNotice = `${entity.name} bijgewerkt.`;
    await refreshEntities(
      state.appView === "settings"
        ? getSettingsRefreshKeys()
        : [key, "setupComplete", "openquattEnabled"],
      "state"
    );
  } catch (error) {
    state.controlError = `${entity.name} kon niet worden bijgewerkt. ${error.message}`;
  } finally {
    state.busyAction = "";
    render();
  }
}

export async function commitOpenQuattRegulationPause(rawResumeValue) {
  const scheduledValue = normalizeDateTimeValue(rawResumeValue);
  if (rawResumeValue && !scheduledValue) {
    state.controlError = "Kies een geldig hervatmoment om automatisch weer in te schakelen.";
    render();
    return;
  }
  if (scheduledValue && !hasEntity("openquattResumeAt")) {
    state.controlError = "Automatisch hervatten is op deze firmware nog niet beschikbaar.";
    render();
    return;
  }

  state.busyAction = "openquatt-regulation";
  state.controlNotice = "";
  state.controlError = "";
  render();

  let resumeScheduled = false;
  try {
    if (hasEntity("openquattResumeAt")) {
      await postDateTimeValue("openquattResumeAt", scheduledValue || OPENQUATT_RESUME_CLEAR_VALUE);
      resumeScheduled = Boolean(scheduledValue);
    }
    await postSwitchState("openquattEnabled", false);
    state.pauseResumeDraft = scheduledValue ? toDateTimeInputValue(scheduledValue) : "";
    state.systemModal = "";
    state.controlNotice = scheduledValue
      ? `Openquatt regeling is tijdelijk uitgeschakeld tot ${formatOpenQuattResumeDateTime(scheduledValue)}.`
      : "Openquatt regeling is uitgeschakeld zonder eindmoment.";
    await refreshOpenQuattControlState();
  } catch (error) {
    if (resumeScheduled && hasEntity("openquattResumeAt")) {
      try {
        await postDateTimeValue("openquattResumeAt", OPENQUATT_RESUME_CLEAR_VALUE);
      } catch (_rollbackError) {
        // Best effort rollback to avoid leaving a stray resume moment behind.
      }
    }
    state.controlError = `Openquatt regeling kon niet worden bijgewerkt. ${error.message}`;
  } finally {
    state.busyAction = "";
    render();
  }
}

export async function commitOpenQuattRegulationResumeNow() {
  state.busyAction = "openquatt-regulation";
  state.controlNotice = "";
  state.controlError = "";
  render();

  try {
    await postSwitchState("openquattEnabled", true);
    state.pauseResumeDraft = "";
    state.systemModal = "";
    state.controlNotice = "Openquatt regeling is weer actief.";
    await refreshOpenQuattControlState();
  } catch (error) {
    state.controlError = `Openquatt regeling kon niet worden ingeschakeld. ${error.message}`;
  } finally {
    state.busyAction = "";
    render();
  }
}

export async function triggerButton(action) {
  const entity = ENTITY_DEFS[action];
  state.busyAction = action;
  state.controlError = "";
  state.controlNotice = "";
  render();

  try {
    const response = await fetch(buildEntityPath(entity.domain, entity.name, "press"), {
      method: "POST",
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    state.controlNotice = action === "apply"
      ? "Setup gemarkeerd als afgerond."
      : "Quick Start teruggezet naar het begin. Huidige tuningwaarden blijven voorlopig staan.";
    await refreshEntities(["setupComplete"], "state");
    clearQuickStartSetupInstall();
    state.quickStartSetupUpdateComplete = false;
    if (action === "reset") {
      state.currentStep = QUICK_STEPS[0].id;
      state.quickStartSetupDraft = "";
      state.quickStartSetupConfirmed = false;
      state.quickStartModalMode = "wizard";
      state.quickStartModalOpen = true;
    }
    state.quickStartModalOpen = action !== "apply";
    setAppView("overview", { syncMode: "replace" });
    syncEntities({ forceFast: true });
  } catch (error) {
    state.controlError = `Actie mislukt voor "${entity.name}". ${error.message}`;
  } finally {
    state.busyAction = "";
    render();
  }
}

export function queueHpWaterCalibrationApplyAnchor() {
  window.requestAnimationFrame(() => {
    if (!state.root || state.systemModal !== "service-task-hp-water-calibration") {
      return;
    }
    const scroller = state.root.querySelector("[data-oq-service-task-scroller]");
    const target = state.root.querySelector("[data-oq-hp-water-calibration-actions]");
    if (!scroller || !target) {
      return;
    }
    const scrollerRect = scroller.getBoundingClientRect();
    const targetRect = target.getBoundingClientRect();
    const nextTop = scroller.scrollTop + targetRect.top - scrollerRect.top - 24;
    scroller.scrollTop = Math.max(0, nextTop);
  });
}

export async function triggerIncidentAction(hpIndex, kind) {
  const endpoint = kind === "start_failure_retry"
    ? "/openquatt/incidents/retry-start"
    : kind === "confirm_odu_power_cycle"
      ? "/openquatt/incidents/confirm-odu-power-cycle"
      : "";
  if (!endpoint || (hpIndex !== 1 && hpIndex !== 2)) return;
  const matchingPendingAction = state.incidentAction?.pending
      && state.incidentAction.hp === hpIndex
      && state.incidentAction.kind === kind;
  if (matchingPendingAction &&
      !state.incidentAction.outcomeUnknown) {
    await refreshIncidentMonitoringData({ force: true });
    return;
  }

  const requestId = matchingPendingAction
    ? state.incidentAction.requestId
    : createIncidentActionRequestId();
  state.busyAction = `incident-${kind}-hp${hpIndex}`;
  state.controlError = "";
  state.controlNotice = "";
  state.incidentAction = {
    hp: hpIndex,
    kind,
    requestId,
    pending: true,
    ok: null,
    result: "",
  };
  render();

  try {
    const accepted = await postIncidentActionRequest(
      fetch,
      endpoint,
      hpIndex,
      requestId,
      state.incidentMonitoringSnapshot?.actionCsrfToken || "",
      async () => {
        await refreshIncidentMonitoringData({ force: true });
        return state.incidentMonitoringSnapshot?.actionCsrfToken || "";
      },
    );
    state.incidentAction = {
      hp: hpIndex,
      kind,
      requestId,
      pending: true,
      ok: null,
      result: "",
    };
    state.controlNotice = `Actie voor HP${hpIndex} geaccepteerd; resultaat wordt gecontroleerd.`;
    render();

  } catch (error) {
    const definitive = error.incidentActionDefinitive === true;
    state.incidentAction = definitive
      ? {
          hp: hpIndex,
          kind,
          requestId,
          pending: false,
          ok: false,
          result: "",
          message: error.message || String(error),
        }
      : {
          hp: hpIndex,
          kind,
          requestId,
          pending: true,
          outcomeUnknown: true,
          ok: null,
          result: "",
          message: error.message || String(error),
        };
    if (definitive) {
      state.controlError = `Actie voor HP${hpIndex} niet uitgevoerd. ${error.message || error}`;
    } else {
      state.controlNotice = `Antwoord voor HP${hpIndex} ging verloren; resultaat wordt met hetzelfde actienummer gecontroleerd.`;
    }
  } finally {
    for (const delayMs of [0, 500, 1500]) {
      if (!state.incidentAction.pending) break;
      if (delayMs) {
        await new Promise((resolve) => window.setTimeout(resolve, delayMs));
      }
      await refreshIncidentMonitoringData({ force: true });
    }
    state.busyAction = "";
    render();
  }
}

export async function triggerNamedButton(key, options = {}) {
  const entity = ENTITY_DEFS[key];
  if (!entity) {
    return;
  }
  const refreshAfterRestart = options.reconnectMode === "restart";
  if (refreshAfterRestart) {
    armRestartRefresh();
  }
  state.busyAction = key;
  state.controlError = "";
  state.controlNotice = "";
  render();

  try {
    const response = await fetch(buildEntityPath(entity.domain, entity.name, "press"), {
      method: "POST",
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    if (refreshAfterRestart) {
      state.restartRefresh.ok = 1;
    }
    const keepCommissioningModalOpen = [
      "commissioningCm100Start",
      "commissioningCm100Stop",
      "boilerPowerTestStart",
      "boilerPowerTestAbort",
      "boilerPowerTestApply",
      "flowAutotuneStart",
      "flowAutotuneAbort",
      "flowAutotuneApply",
      "airPurgeStart",
      "airPurgeAbort",
      "hpWaterCalibrationStart",
      "hpWaterCalibrationAbort",
      "hpWaterCalibrationApply",
      "manualFlowStart",
      "manualFlowAbort",
      "manualFlowApplyHeating",
      "manualFlowApplyCooling",
      "manualHpStart",
      "manualHpAbort",
      "trendHistoryFlush",
      "decisionLogHistoryFlush",
      "decisionLogHistoryClear",
      "lifetimeEnergyHistoryCapture",
      "lifetimeEnergyHistoryClear",
    ].includes(key) || ODU_RUNTIME_FREQUENCY_BUTTON_KEYS.has(key);
    if (!keepCommissioningModalOpen) {
      stopLoginAuthStatusPolling();
      state.systemModal = "";
    }
    state.controlNotice = options.successNotice || `${entity.name} gestart.`;
    if (options.reconnectMode) {
      beginDeviceReconnect(options.reconnectMode);
    }
    if (refreshAfterRestart) {
      awaitRestartEvidence();
    }
    if (Array.isArray(options.refreshKeys) && options.refreshKeys.length) {
      const refreshDelayMs = Number(options.refreshDelayMs || 0);
      if (Number.isFinite(refreshDelayMs) && refreshDelayMs > 0) {
        await new Promise((resolve) => window.setTimeout(resolve, refreshDelayMs));
      }
      await refreshEntities(options.refreshKeys, "state");
    }
    if (options.refreshIncidentMonitoring === true) {
      await refreshIncidentMonitoringData({ force: true });
    }
  } catch (error) {
    if (key === "commissioningCm100Start") {
      state.pendingCommissioningCm100Start = false;
      state.commissioningTaskLock = "";
    } else if (key === "boilerPowerTestStart") {
      state.pendingBoilerPowerTestStart = false;
      state.commissioningTaskLock = "";
    } else if (key === "flowAutotuneStart") {
      state.pendingFlowAutotuneStart = false;
      state.commissioningTaskLock = "";
    } else if (key === "airPurgeStart") {
      state.pendingAirPurgeStart = false;
      state.commissioningTaskLock = "";
    } else if (key === "hpWaterCalibrationStart") {
      state.pendingHpWaterCalibrationStart = false;
      state.commissioningTaskLock = "";
    } else if (key === "manualFlowStart") {
      state.pendingManualFlowStart = false;
      state.commissioningTaskLock = "";
    } else if (key === "manualHpStart") {
      state.pendingManualHpStart = false;
      state.commissioningTaskLock = "";
    }
    if (refreshAfterRestart && isLikelyDeviceConnectionError(error.message)) {
      state.restartRefresh.ok = 2;
      awaitRestartEvidence();
      beginDeviceReconnect("restart", error.message);
      state.controlNotice = options.successNotice || `${entity.name} gestart.`;
    } else {
      if (refreshAfterRestart) {
        clearRestartRefresh();
      }
      state.controlError = `${options.errorPrefix || `Actie mislukt voor "${entity.name}"`}. ${error.message}`;
    }
  } finally {
    state.busyAction = "";
    render();
    if (key === "hpWaterCalibrationApply") {
      queueHpWaterCalibrationApplyAnchor();
    }
  }
}

export async function triggerNamedButtonGroup(keys, options = {}) {
  const entities = keys.map((key) => ENTITY_DEFS[key]).filter(Boolean);
  if (entities.length === 0) return;

  const busyAction = String(options.busyAction || "named-button-group");
  state.busyAction = busyAction;
  state.controlError = "";
  state.controlNotice = "";
  render();

  try {
    const results = await Promise.allSettled(entities.map(async (entity) => {
      const response = await fetch(buildEntityPath(entity.domain, entity.name, "press"), { method: "POST" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
    }));

    const failed = results.find((result) => result.status === "rejected");
    const refreshDelayMs = Number(options.refreshDelayMs || 0);
    if (Number.isFinite(refreshDelayMs) && refreshDelayMs > 0) {
      await new Promise((resolve) => window.setTimeout(resolve, refreshDelayMs));
    }
    if (Array.isArray(options.refreshKeys) && options.refreshKeys.length) {
      await refreshEntities(options.refreshKeys, "state");
    }
    if (failed) throw failed.reason;

    if (state.busyAction === busyAction) {
      state.controlNotice = options.successNotice || "Acties gestart.";
    }
  } catch (error) {
    if (state.busyAction === busyAction) {
      state.controlError = `${options.errorPrefix || "Actie mislukt"}. ${error.message}`;
    }
  } finally {
    if (state.busyAction === busyAction) state.busyAction = "";
    render();
  }
}

export function updateCurveDraftFromPointer(clientY) {
  const svg = state.root ? state.root.querySelector(".oq-helper-curve-svg") : null;
  if (!svg || !state.draggingCurveKey) {
    return;
  }

  const rect = svg.getBoundingClientRect();
  const plotTop = 22;
  const plotHeight = 180;
  const localY = ((clientY - rect.top) / rect.height) * 240;
  const clampedY = Math.min(plotTop + plotHeight, Math.max(plotTop, localY));
  const value = 70 - ((clampedY - plotTop) / plotHeight) * 50;
  const normalized = normalizeNumber(state.draggingCurveKey, value);

  if (String(getEntityValue(state.draggingCurveKey)) !== String(normalized)) {
    state.drafts[state.draggingCurveKey] = normalized;
    render();
  }
}
