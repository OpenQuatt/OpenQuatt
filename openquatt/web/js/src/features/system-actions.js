import { hasEntity } from "../core/app-shared.js";
import { setEntityBackupValue, verifyEntityBackupSelectState } from "../core/entity-backup.js";
import { getOpenQuattPauseDraftValue, getOpenQuattPausePresetValue } from "../core/entity-store.js";
import { commitOpenQuattRegulationPause, commitOpenQuattRegulationResumeNow, commitSelect, triggerNamedButton } from "../core/entity-write-actions.js";
import { refreshEntities } from "../core/entity-sync.js";
import { invokeActionMap } from "../core/action-router.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { clearDebugRecordingDevicePollTimer, scheduleDebugRecordingDeviceStatusPoll } from "./debug-recording.js";
import { stopLoginAuthStatusPolling } from "./security-actions.js";
import { clearSettingsBackupDraft } from "./storage-history.js";

function closeSystemModal() {
  stopLoginAuthStatusPolling();
  clearDebugRecordingDevicePollTimer();
  state.systemModal = "";
  state.authDraftCurrentPassword = "";
  state.authDraftNewPassword = "";
  state.authDraftConfirmPassword = "";
  state.authNotice = "";
  state.authError = "";
  state.apiSecurityNotice = "";
  state.apiSecurityError = "";
  state.pendingControlModeOverride = "";
  clearSettingsBackupDraft();
  render();
  scheduleDebugRecordingDeviceStatusPoll();
}

const systemActionHandlers = {
  "open-connectivity-modal": () => {
    state.systemModal = "connectivity";
    render();
  },
  "open-water-sensor-corrections-modal": () => {
    state.systemModal = "water-sensor-corrections";
    render();
  },
  "open-restart-confirm": () => {
    state.systemModal = "restart-confirm";
    render();
  },
  "open-control-mode-override-confirm": (button) => {
    const option = String(button.dataset.controlModeOption || "");
    if (!["Force CM0", "Force CM1", "Force CM98"].includes(option)) {
      return;
    }
    state.controlError = "";
    state.controlNotice = "";
    state.pendingControlModeOverride = option;
    state.systemModal = "control-mode-override-confirm";
    render();
  },
  "confirm-control-mode-override": () => {
    const option = String(state.pendingControlModeOverride || "");
    if (!["Force CM0", "Force CM1", "Force CM98"].includes(option)) {
      closeSystemModal();
      return;
    }
    state.pendingControlModeOverride = "";
    state.systemModal = "";
    return commitSelect("controlModeOverride", option);
  },
  "clear-control-mode-override": () => commitSelect("controlModeOverride", "Auto"),
  "open-runtime-reset-confirm": () => {
    state.controlError = "";
    state.controlNotice = "";
    state.systemModal = "runtime-reset-confirm";
    render();
  },
  "confirm-runtime-reset": () => {
    const key = hasEntity("resetRuntimeCountersHp1Hp2")
      ? "resetRuntimeCountersHp1Hp2"
      : hasEntity("resetRuntimeCountersHp1") ? "resetRuntimeCountersHp1" : "";
    if (!key) {
      closeSystemModal();
      return;
    }
    return triggerNamedButton(key, {
      successNotice: "De draaitijdbalans is teruggezet. Nieuwe tellerwaarden kunnen binnen ongeveer één minuut zichtbaar worden.",
      errorPrefix: "Draaiurentellers resetten mislukt",
    });
  },
  "open-energy-counter-reset-confirm": () => {
    state.controlError = "";
    state.controlNotice = "";
    state.systemModal = "energy-counter-reset-confirm";
    render();
  },
  "confirm-energy-counter-reset": () => triggerNamedButton("resetCumulativeEnergyCounters", {
    successNotice: "De cumulatieve energietellers zijn teruggezet.",
    errorPrefix: "Energietellers resetten mislukt",
  }),
  "open-silent-settings-modal": () => {
    state.systemModal = "silent-settings";
    render();
  },
  "open-openquatt-pause-modal": () => {
    state.pauseResumeDraft = getOpenQuattPauseDraftValue();
    state.systemModal = "openquatt-pause";
    render();
  },
  "open-heating-strategy-advice-modal": () => {
    state.systemModal = "heating-strategy-advice";
    render();
  },
  "apply-heating-strategy-advice": async (button) => {
    const target = String(button.dataset.heatingEnableTarget || "").trim() || "Disabled";
    state.busyAction = "quickstart-heating-enable";
    state.controlNotice = "";
    state.controlError = "";
    render();
    try {
      const applied = await setEntityBackupValue("heatingEnableSource", target);
      if (!await verifyEntityBackupSelectState("heatingEnableSource", applied)) {
        throw new Error("de controller heeft de gekozen bron niet bevestigd.");
      }
      state.entities.heatingEnableSource = { ...(state.entities.heatingEnableSource || {}), value: applied, state: applied };
      state.controlNotice = target === "Disabled" ? "Warmtetoestemming op Niet gebruiken gezet — je ziet nu ‘Komt overeen’." : `Warmtetoestemming op ${target} gezet — je ziet nu ‘Komt overeen’.`;
      await refreshEntities(["heatingEnableSource", "heatingEnableValid", "heatingEnableSelected"], "all");
    } catch (error) {
      state.controlError = `Warmtetoestemming kon niet worden opgeslagen. ${error.message}`;
    } finally {
      state.busyAction = "";
      render();
    }
  },
  "enable-openquatt-now": () => commitOpenQuattRegulationResumeNow(),
  "apply-openquatt-preset": (button) => {
    const presetValue = getOpenQuattPausePresetValue(button.dataset.pausePreset || "");
    state.pauseResumeDraft = presetValue;
    commitOpenQuattRegulationPause(presetValue);
  },
  "apply-openquatt-indefinite": () => commitOpenQuattRegulationPause(""),
  "apply-openquatt-custom-pause": () => {
    if (!String(state.pauseResumeDraft || "").trim()) {
      state.controlError = "Kies eerst een datum en tijd om automatisch te hervatten.";
      render();
      return;
    }
    commitOpenQuattRegulationPause(state.pauseResumeDraft || "");
  },
  "close-system-modal": () => closeSystemModal(),
  "confirm-restart": () => triggerNamedButton("restartAction", {
    successNotice: "OpenQuatt wordt opnieuw opgestart. Wacht even tot de webinterface weer terugkomt.",
    errorPrefix: "Herstart mislukt",
    reconnectMode: "restart",
  }),
};

export function handleSystemAction(action, button) {
  return invokeActionMap(systemActionHandlers, action, button);
}
