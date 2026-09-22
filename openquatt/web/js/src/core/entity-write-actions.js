import { hasEntity } from "./app-shared.js";
import { fetchWithTimeout } from "./browser-utils.js";
import { CURVE_POINTS, ENTITY_DEFS, FIRMWARE_ENTITY_KEYS, FLOW_SETTING_KEYS, HEADER_ENTITY_KEYS, LIMIT_KEYS, OPENQUATT_RESUME_CLEAR_VALUE, OVERVIEW_KEYS, POWER_HOUSE_KEYS, QUICK_STEPS } from "./config.js";
import { armRestartRefresh, awaitRestartEvidence, beginDeviceReconnect, clearRestartRefresh } from "./device-reconnect.js";
import { buildEntityPath, isCurveMode } from "./domain-helpers.js";
import { formatOpenQuattResumeDateTime, getEntityValue, normalizeDateTimeValue, normalizeNumber, normalizeTimeValue, parseLooseNumber, toDateTimeInputValue } from "./entity-store.js";
import { ENTITY_REQUEST_TIMEOUT_MS, getSettingsRefreshKeys, isLikelyDeviceConnectionError, refreshEntities, refreshIncidentMonitoringData, syncEntities } from "./entity-sync.js";
import {
  createIncidentActionRequestId,
  postIncidentActionRequest,
} from "./incident-monitoring.js";
import { setAppView } from "./navigation.js";
import { render } from "./render-scheduler.js";
import { t } from "../i18n/index.js";
import { clearQuickStartSetupInstall, state } from "./state.js";
import { pollFirmwareUpdateState, primeFirmwareUpdateState } from "../features/firmware-update.js";
import { updateFirmwareState } from "./feature-state.js";
import { stopLoginAuthStatusPolling } from "../features/security-actions.js";
import { refreshSettingsStorageStateSoon, SETTINGS_STORAGE_KEYS } from "../features/storage-history.js";
import { refreshWebServerLogHistory } from "../features/webserver-logs.js";
import { waitForPerformanceTelemetryChoiceConfirmation } from "./performance-telemetry-domain.js";
import { waitForUsageTelemetryChoiceConfirmation } from "./usage-telemetry-domain.js";

async function commitTelemetrySwitch({ key, choiceKey, extraRefreshKeys = [], waitForChoiceConfirmation, entity, enabled }) {
  const confirmChoice = (expectedEnabled) => waitForChoiceConfirmation({
    refresh: async () => {
      await refreshEntities([key, choiceKey, ...extraRefreshKeys], "all");
      return [getEntityValue(key), getEntityValue(choiceKey)];
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
      throw new Error(t("actions.savedChoiceNotConfirmed"));
    }
    state.controlError = "";
    state.controlNotice = t(enabled ? "actions.entityEnabled" : "actions.entityDisabled", { name: entity.name });
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
        ? t("actions.sharingEnableUnconfirmed")
        : t("actions.sharingDisabled");
    } else {
      if (previousEntity) {
        state.entities[key] = previousEntity;
      } else {
        delete state.entities[key];
      }
      state.controlError = t("actions.choiceConfirmFailed", { error: error.message });
    }
  } finally {
    state.busyAction = "";
    render();
  }
}

async function commitUsageTelemetrySwitch(entity, enabled) {
  await commitTelemetrySwitch({
    key: "usageTelemetryEnabled",
    choiceKey: "usageTelemetryChoiceConfigured",
    extraRefreshKeys: ["usageTelemetryInstallationId"],
    waitForChoiceConfirmation: waitForUsageTelemetryChoiceConfirmation,
    entity,
    enabled,
  });
}

async function commitPerformanceTelemetrySwitch(entity, enabled) {
  await commitTelemetrySwitch({
    key: "performanceTelemetryEnabled",
    choiceKey: "performanceTelemetryChoiceConfigured",
    waitForChoiceConfirmation: waitForPerformanceTelemetryChoiceConfirmation,
    entity,
    enabled,
  });
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
        throw new Error(t("actions.controllerStatusUnconfirmed", { error: error.message }));
      }
      const confirmedValue = String(confirmationPayload?.value ?? confirmationPayload?.state ?? "");
      state.entities[key] = {
        ...(previousEntity || {}),
        ...(confirmationPayload || {}),
      };
      if (confirmedValue !== option) {
        throw new Error(t("actions.controllerMismatch", { value: confirmedValue || t("common.unknown") }));
      }
    }
    delete state.drafts[key];
    delete state.inputDrafts[key];
    state.controlNotice = verifyControlModeOverride
      ? option === "Auto"
        ? t("actions.overrideRestored")
        : t("actions.overrideTemp", { option })
      : key === "preferredConnection"
        ? option === "Automatic"
          ? t("actions.autoDetect")
          : t("actions.switchStarted", { option })
      : t("actions.entityUpdated", { name: entity.name });
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
      state.controlNotice = t("actions.releaseChannelUpdated");
    } else if (key === "debugLevel") {
      state.controlNotice = t("actions.loggerLevelUpdated");
      if (state.systemModal === "webserver-logs") {
        void refreshWebServerLogHistory();
      }
    } else if (key === "preferredConnection") {
      // The controller confirms this asynchronous switch once the target is stable.
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
    state.controlError = t("actions.entityUpdateFailed", { name: entity.name, error: error.message });
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
      return t("actions.startAboveStop", { value: stopThreshold });
    }
  }
  if (key === "boilerSupportStopThreshold") {
    const startThreshold = parseLooseNumber(entities.boilerSupportStartThreshold?.value ?? entities.boilerSupportStartThreshold?.state);
    if (Number.isFinite(startThreshold) && normalized >= startThreshold) {
      return t("actions.stopBelowStart", { value: startThreshold });
    }
  }
  const exclusionBoundary = key.match(/^hp[12]Exclude(Min|Max)Hz$/);
  if (exclusionBoundary && normalized > 0) {
    const isMinimum = exclusionBoundary[1] === "Min";
    const pairedKey = key.replace(isMinimum ? "MinHz" : "MaxHz", isMinimum ? "MaxHz" : "MinHz");
    const pairedValue = parseLooseNumber(entities[pairedKey]?.value ?? entities[pairedKey]?.state);
    if (Number.isFinite(pairedValue) && pairedValue > 0) {
      if (isMinimum && normalized > pairedValue) {
        return t("actions.minBelowMax", { value: pairedValue });
      }
      if (!isMinimum && normalized < pairedValue) {
        return t("actions.maxAboveMin", { value: pairedValue });
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
  if (key === "performanceTelemetryEnabled") {
    await commitPerformanceTelemetrySwitch(entity, enabled);
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
    state.controlNotice = t(enabled ? "actions.entityEnabled" : "actions.entityDisabled", { name: entity.name });
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
    render();
  } catch (error) {
    state.controlError = t("actions.entityAdjustFailed", { name: entity.name, error: error.message });
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
    state.controlNotice = successNotice || t("actions.entityUpdated", { name: entity.name });
    await refreshEntities(
      state.appView === "settings"
        ? getSettingsRefreshKeys()
        : [...new Set([key, "setupComplete", "strategy", ...FLOW_SETTING_KEYS, ...LIMIT_KEYS])]
      ,
      "state"
    );
  } catch (error) {
    state.inputDrafts[key] = String(normalized).replace(".", ",");
    state.controlError = t("actions.entityUpdateFailed", { name: entity.name, error: error.message });
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
    state.controlNotice = t("actions.frequencyExclusionDisabled");
    state.controlError = "";
    render();
    return true;
  }

  state.controlNotice = "";
  state.controlError = writeError || verificationError || t("actions.frequencyExclusionDisableFailed");
  render();
  return false;
}

export async function commitTime(key, value) {
  const entity = ENTITY_DEFS[key];
  const normalized = normalizeTimeValue(value);
  if (!normalized || state.savingTimeFields.has(key)) return false;
  state.savingTimeFields.add(key);
  state.timeWriteRevision += 1;
  state.inputDrafts[key] = String(value);
  state.busyAction = `save-${key}`;
  state.controlNotice = "";
  state.controlError = "";
  render();

  try {
    const response = await fetchWithTimeout(
      `${buildEntityPath(entity.domain, entity.name, "set")}?value=${encodeURIComponent(normalized)}`,
      { method: "POST" }, ENTITY_REQUEST_TIMEOUT_MS
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    const payload = await fetchWithTimeout(
      buildEntityPath(entity.domain, entity.name), { cache: "no-store" }, ENTITY_REQUEST_TIMEOUT_MS, "",
      async (confirmation) => {
        if (!confirmation.ok) throw new Error(`HTTP ${confirmation.status}`);
        return confirmation.json();
      }
    );
    state.entities[key] = { ...(state.entities[key] || {}), ...payload, value: payload.value ?? payload.state ?? "" };
    if (normalizeTimeValue(payload.value ?? payload.state) !== normalized) {
      throw new Error(t("actions.timeNotConfirmed"));
    }
    delete state.inputDrafts[key];
    state.controlNotice = t("actions.entityUpdated", { name: entity.name });
    return true;
  } catch (error) {
    // Keep the attempted value visible so the user can retry without retyping.
    state.controlError = t("actions.entityUpdateFailed", { name: entity.name, error: error.message });
    return false;
  } finally {
    state.savingTimeFields.delete(key);
    state.timeWriteRevision += 1;
    if (state.busyAction === `save-${key}`) state.busyAction = "";
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
    state.controlNotice = t("actions.entityUpdated", { name: entity.name });
    await refreshEntities(
      state.appView === "settings"
        ? getSettingsRefreshKeys()
        : [key, "setupComplete"],
      "state"
    );
  } catch (error) {
    state.inputDrafts[key] = normalized;
    state.controlError = t("actions.entityUpdateFailed", { name: entity.name, error: error.message });
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
    state.controlNotice = t("actions.entityUpdated", { name: entity.name });
    await refreshEntities(
      state.appView === "settings"
        ? getSettingsRefreshKeys()
        : [key, "setupComplete", "openquattEnabled"],
      "state"
    );
  } catch (error) {
    state.controlError = t("actions.entityUpdateFailed", { name: entity.name, error: error.message });
  } finally {
    state.busyAction = "";
    render();
  }
}

export async function commitOpenQuattRegulationPause(rawResumeValue) {
  const scheduledValue = normalizeDateTimeValue(rawResumeValue);
  if (rawResumeValue && !scheduledValue) {
    state.controlError = t("actions.pauseInvalidMoment");
    render();
    return;
  }
  if (scheduledValue && !hasEntity("openquattResumeAt")) {
    state.controlError = t("actions.pauseUnavailableFw");
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
      ? t("actions.pauseUntil", { when: formatOpenQuattResumeDateTime(scheduledValue) })
      : t("actions.pauseIndefinite");
    await refreshOpenQuattControlState();
  } catch (error) {
    if (resumeScheduled && hasEntity("openquattResumeAt")) {
      try {
        await postDateTimeValue("openquattResumeAt", OPENQUATT_RESUME_CLEAR_VALUE);
      } catch (_rollbackError) {
        // Best effort rollback to avoid leaving a stray resume moment behind.
      }
    }
    state.controlError = t("actions.regulationUpdateFailed", { error: error.message });
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
    state.controlNotice = t("actions.regulationResumed");
    await refreshOpenQuattControlState();
  } catch (error) {
    state.controlError = t("actions.regulationEnableFailed", { error: error.message });
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
      ? t("actions.setupCompleted")
      : t("actions.quickStartReset");
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
    state.controlError = t("actions.actionFailedFor", { name: entity.name, error: error.message });
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
    state.controlNotice = t("actions.hpActionAccepted", { hp: hpIndex });
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
      state.controlError = t("actions.hpActionFailed", { hp: hpIndex, error: error.message || error });
    } else {
      state.controlNotice = t("actions.hpActionResponseLost", { hp: hpIndex });
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
    ].includes(key);
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
      state.controlError = t("actions.actionFailedWithPrefix", { prefix: options.errorPrefix || t("actions.actionFailedName", { name: entity.name }), error: error.message });
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

    const refreshUntil = typeof options.refreshUntil === "function" ? options.refreshUntil : null;
    const refreshIntervalMs = Math.max(0, Number(options.refreshIntervalMs || 0));
    const refreshTimeoutMs = Math.max(0, Number(options.refreshTimeoutMs || 0));
    const refreshStartedAt = Date.now();
    while (refreshUntil && !refreshUntil()) {
      if (state.busyAction !== busyAction) return;
      if (Date.now() - refreshStartedAt >= refreshTimeoutMs) {
        throw new Error(options.refreshTimeoutMessage || t("actions.resultTimeout"));
      }
      await new Promise((resolve) => window.setTimeout(resolve, refreshIntervalMs));
      await refreshEntities(options.refreshKeys, "state");
    }

    if (state.busyAction === busyAction) {
      state.controlNotice = options.successNotice || t("actions.actionsStarted");
    }
  } catch (error) {
    if (state.busyAction === busyAction) {
      state.controlError = t("actions.actionFailedWithPrefix", { prefix: options.errorPrefix || t("actions.actionFailed"), error: error.message });
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
