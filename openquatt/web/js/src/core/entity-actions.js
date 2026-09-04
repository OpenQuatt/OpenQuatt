import { hasEntity } from "./app-shared.js";
import { ENTITY_DEFS } from "./config.js";
import { getInputDraftValue } from "./control-drafts.js";
import { reportUnknownAction } from "./action-router.js";
import { commitQuickStartStrategySelection, handleControlAction } from "./control-actions.js";
import { isCurveMode } from "./domain-helpers.js";
import { formatValue, getEntityValue, getNumberMeta, normalizeDateTimeValue, normalizeNumber, normalizeTimeValue, parseLooseNumber } from "./entity-store.js";
import { commitDateTime, commitNumber, commitSelect, commitText, commitTime, disableRange, triggerNamedButton, updateCurveDraftFromPointer } from "./entity-write-actions.js";
import { handleNamedButtonAction } from "./named-button-actions.js";
import { state } from "./state.js";
import { getCommittedElectricalLimitRaw, getElectricalLimitChangePlan, renderElectricalLimitEstimate, renderElectricalLimitFooter, renderElectricalLimitRestore, resolveElectricalLimitView } from "../settings/electrical-limit.js";
import { setInterfacePanelOpen } from "./runtime.js";
import { handleDebugRecordingAction } from "../features/debug-recording.js";
import { handleControlReplayAction } from "../features/control-replay-actions.js";
import { handleFirmwareAction } from "../features/firmware-actions.js";
import { updateFirmwareState, updateEnergyHistoryState } from "./feature-state.js";
import { getFirmwareLatestVersion, getFirmwareTestAssetUrls, getFirmwareTestPrNumber, getFirmwareTestTargetModel, resetFirmwareManualUploadSelection, resetFirmwareTestSelection } from "../features/firmware-update.js";
import { handleMqttAction, syncMqttDraftFromInput } from "../features/mqtt-actions.js";
import { handleOduEepromDumpAction } from "../features/odu-eeprom-dump.js";
import { handleOduRuntimeFrequencyAction, handleOduRuntimeFrequencyInputKeyDown, updateOduRuntimeFrequencyDraft } from "../features/odu-runtime-frequency.js";
import { handleQuickStartAction } from "../features/quickstart-ui-actions.js";
import { handleSecurityAction, stopLoginAuthStatusPolling } from "../features/security-actions.js";
import { clearSettingsBackupDraft, handleSettingsBackupFileSelection, handleStorageHistoryAction, normalizeEnergyHistoryExportMode } from "../features/storage-history.js";
import { handleSystemAction } from "../features/system-actions.js";
import { handleShellAction } from "../features/shell-actions.js";
import { handleViewAction } from "../features/view-actions.js";
import { handleWebServerLogAction } from "../features/webserver-logs.js";
import { handleEnergyHistoryPointerMove, setEnergyHistoryPeriodValue } from "../views/energy.js";
import { escapeHtml } from "./html.js";
import { render } from "./render-scheduler.js";

const actionDelegates = [
  handleViewAction,
  handleControlReplayAction,
  handleQuickStartAction,
  handleDebugRecordingAction,
  handleOduEepromDumpAction,
  handleOduRuntimeFrequencyAction,
  handleSecurityAction,
  handleMqttAction,
  (action) => handleStorageHistoryAction(action, { triggerNamedButton }),
  handleFirmwareAction,
  handleControlAction,
  handleWebServerLogAction,
  handleSystemAction,
  handleNamedButtonAction,
  handleShellAction,
];

function updateFrequencyRangeControl(input) {
  const control = input.closest('[data-oq-dual-range="true"]');
  if (!control) {
    return;
  }
  const minInput = control.querySelector('[data-oq-range-role="min"]');
  const maxInput = control.querySelector('[data-oq-range-role="max"]');
  if (!minInput || !maxInput) {
    return;
  }
  const inputValue = Number(input.value);
  if (inputValue === 0) {
    minInput.value = maxInput.value = "0";
  }
  if (inputValue > 0 && inputValue < 20) {
    input.value = "20";
  }
  let minValue = Number(minInput.value);
  let maxValue = Number(maxInput.value);
  if (input.dataset.oqRangeRole === "min" && minValue > 0 && maxValue > 0 && minValue > maxValue) {
    minValue = maxValue;
    minInput.value = String(minValue);
  } else if (input.dataset.oqRangeRole === "max" && minValue > 0 && maxValue > 0 && maxValue < minValue) {
    maxValue = minValue;
    maxInput.value = String(maxValue);
  }
  const scaleMin = Number(minInput.min);
  const scaleMax = Number(minInput.max);
  const span = Math.max(1, scaleMax - scaleMin);
  const disabled = minValue === 0 || maxValue === 0;
  control.classList.toggle("is-disabled", disabled);
  control.classList.remove("is-invalid");
  control.style.setProperty("--oq-range-start", `${((minValue - scaleMin) / span) * 100}%`);
  control.style.setProperty("--oq-range-end", `${((maxValue - scaleMin) / span) * 100}%`);
  const value = control.querySelector("[data-oq-range-value]");
  if (value) {
    value.textContent = disabled ? "Geen uitsluiting" : `${minValue}–${maxValue} Hz`;
  }
}

  export function requestElectricalLimitChange(rawValue) {
    const meta = getNumberMeta("electricalCurrentLimit");
    const plan = getElectricalLimitChangePlan(rawValue, getCommittedElectricalLimitRaw(), meta.min);
    if (!plan.valid) {
      state.inputDrafts.electricalCurrentLimit = String(rawValue ?? "");
      render();
      return false;
    }
    state.inputDrafts.electricalCurrentLimit = String(rawValue ?? "");
    state.drafts.electricalCurrentLimit = plan.clamped;
    if (plan.requiresConfirmation) {
      state.pendingElectricalLimit = { fromA: plan.fromA, toA: plan.clamped, standardA: plan.info.standardA };
      state.systemModal = "electrical-limit-confirm";
      render();
      return true;
    }
    state.pendingElectricalLimit = null;
    void commitNumber("electricalCurrentLimit", plan.clamped);
    return false;
  }

  export function refreshElectricalLimitLiveRegions() {
    // Live inline feedback tijdens het typen: alleen footer en herstelknop
    // worden bijgewerkt, het invoerveld zelf (en daarmee de focus) blijft
    // onaangeroerd. Geen volledige render(), die zou de focus stelen.
    if (!state.root || state.systemModal) {
      return;
    }
    const card = state.root.querySelector('[data-oq-settings-field="electricalCurrentLimit"]');
    if (!card) {
      return;
    }
    const view = resolveElectricalLimitView();
    const estimate = card.querySelector(".oq-settings-electrical-estimate");
    if (estimate) {
      estimate.outerHTML = renderElectricalLimitEstimate(view);
    }
    const restore = card.querySelector(".oq-settings-electrical-restore");
    if (restore) {
      restore.outerHTML = renderElectricalLimitRestore(view);
    }
    const body = card.querySelector(".oq-settings-electrical-body");
    if (body) {
      body.outerHTML = renderElectricalLimitFooter(view);
    }
  }

  export function handleFocusChange() {
    window.setTimeout(() => {
      const active = document.activeElement;
      state.focusedField = active && active.dataset ? active.dataset.oqField || "" : "";
      state.settingsInteractionLock = Boolean(active && active.closest && active.closest(".oq-ph-concept-hotspot"));
      if (!state.focusedField
          && state.incidentMonitoringRenderPending
          && state.appView === "settings"
          && state.settingsGroup === "service") {
        state.incidentMonitoringRenderPending = false;
        render();
      }
    }, 0);
  }

  export function handleSettingsInteractionStart(event) {
    if (event.target.closest(".oq-ph-concept-hotspot")) {
      state.settingsInteractionLock = true;
    }
  }

  export function handleSettingsInteractionEnd(event) {
    const hotspot = event.target.closest(".oq-ph-concept-hotspot");
    if (!hotspot) {
      return;
    }

    if (event.relatedTarget && hotspot.contains(event.relatedTarget)) {
      return;
    }

    const hoveredHotspot = state.root && state.root.querySelector(".oq-ph-concept-hotspot:hover");
    const focusedHotspot = document.activeElement && document.activeElement.closest
      ? document.activeElement.closest(".oq-ph-concept-hotspot")
      : null;

    state.settingsInteractionLock = Boolean(hoveredHotspot || focusedHotspot);
  }

  export function handleInput(event) {
    if (event.target.dataset.oqOduRuntimeHp) {
      updateOduRuntimeFrequencyDraft(event.target);
      return;
    }

    if (event.target.dataset.oqQuickstartSetupConfirm) {
      state.quickStartSetupConfirmed = Boolean(event.target.checked);
      render();
      return;
    }

    if (event.target.dataset.oqFirmwareDowngradeConfirm) {
      updateFirmwareState({
        firmwareDowngradeConfirmedVersion: event.target.checked ? getFirmwareLatestVersion() : "",
      });
      render();
      return;
    }

    if (event.target.dataset.oqFirmwareConnectionConfirm) {
      updateFirmwareState({ firmwareConnectionSwitchConfirmed: Boolean(event.target.checked) });
      render();
      return;
    }

    if (event.target.dataset.oqFirmwareTopologyConfirm) {
      updateFirmwareState({ firmwareTopologySwitchConfirmed: Boolean(event.target.checked) });
      render();
      return;
    }

    if (event.target.dataset.oqFirmwareTestConfirm) {
      updateFirmwareState({
        updateTestFirmwareConfirmed: Boolean(event.target.checked),
        updateTestFirmwareError: "",
      });
      const section = event.target.closest(".oq-helper-modal-callout");
      const installButton = section?.querySelector('[data-oq-action="install-firmware-test"]');
      if (installButton) {
        installButton.disabled = !state.updateTestFirmwareConfirmed || !getFirmwareTestPrNumber();
      }
      section?.querySelector('[data-oq-firmware-test-runtime-error="true"]')?.remove();
      return;
    }

    if (event.target.dataset.oqFirmwareTestPr) {
      updateFirmwareState({
        updateTestFirmwarePr: String(event.target.value || ""),
        updateTestFirmwareConfirmed: false,
        updateTestFirmwareError: "",
        updateTestFirmwareBuild: null,
      });
      const section = event.target.closest(".oq-helper-modal-callout");
      const confirmInput = section?.querySelector('[data-oq-firmware-test-confirm="true"]');
      if (confirmInput) {
        confirmInput.checked = false;
      }
      const installButton = section?.querySelector('[data-oq-action="install-firmware-test"]');
      if (installButton) {
        installButton.disabled = true;
      }
      const target = getFirmwareTestTargetModel();
      const urls = getFirmwareTestAssetUrls(getFirmwareTestPrNumber(), target);
      const assetNote = section?.querySelector('[data-oq-firmware-test-asset-note="true"]');
      if (assetNote) {
        assetNote.textContent = urls ? target.otaFileName : "Vul een PR-nummer in om de OTA-build te kiezen.";
      }
      section?.querySelector('[data-oq-firmware-test-build-row="true"]')?.remove();
      section?.querySelector('[data-oq-firmware-test-runtime-error="true"]')?.remove();
      return;
    }

    const mqttField = event.target.dataset.oqMqttField;
    if (mqttField) {
      syncMqttDraftFromInput(event.target);
      return;
    }

    if (event.target.dataset.oqBackupMqttPassword !== undefined) {
      state.settingsBackupMqttPassword = String(event.target.value || "");
      state.settingsBackupError = "";
      const restoreButton = event.target.closest(".oq-helper-modal")?.querySelector('[data-oq-action="confirm-settings-backup-restore"]');
      if (restoreButton) {
        restoreButton.disabled = !state.settingsBackupMqttPassword;
        restoreButton.textContent = state.settingsBackupMqttPassword ? "Herstellen" : "Vul MQTT-wachtwoord in";
      }
      event.target.closest(".oq-helper-modal")?.querySelector(".oq-settings-backup-error")?.remove();
      return;
    }

    const field = event.target.dataset.oqField;
    if (!field) {
      if (event.target.dataset.oqQuickstartCicUrl !== undefined) {
        state.quickStartCicFeedUrlDraft = String(event.target.value || "");
        return;
      }
      if (event.target.dataset.oqQuickstartThermostatSource !== undefined) {
        state.quickStartThermostatSourceDraft = String(event.target.value || "");
        render();
        return;
      }
      const authField = event.target.dataset.oqAuthField;
      if (authField) {
        state.authNotice = "";
        state.authError = "";
        if (authField === "username") {
          state.authDraftUsername = String(event.target.value || "");
        } else if (authField === "currentPassword") {
          state.authDraftCurrentPassword = String(event.target.value || "");
        } else if (authField === "newPassword") {
          state.authDraftNewPassword = String(event.target.value || "");
        } else if (authField === "confirmPassword") {
          state.authDraftConfirmPassword = String(event.target.value || "");
        }
        return;
      }

      return;
    }

    if (event.target.dataset.oqPauseDraft) {
      state.pauseResumeDraft = String(event.target.value || "");
      return;
    }

    if (ENTITY_DEFS[field]?.domain === "text") {
      state.inputDrafts[field] = String(event.target.value || "");
      return;
    }

    if (event.target.type === "range" || event.target.type === "number") {
      if (event.target.dataset.oqRangeRole) {
        updateFrequencyRangeControl(event.target);
      }
      if (event.target.type === "number") {
        state.inputDrafts[field] = event.target.value;
      }

      const numeric = parseLooseNumber(event.target.value);
      if (!Number.isNaN(numeric)) {
        const normalized = normalizeNumber(field, event.target.value);
        state.drafts[field] = normalized;
        if (event.target.type === "range") {
          const sliderValue = event.target.closest(".oq-helper-slider-field")?.querySelector(".oq-helper-slider-meta strong");
          if (sliderValue) {
            sliderValue.textContent = formatValue(field, normalized);
          }
        }
        if (field === "electricalCurrentLimit" && event.target.type === "number") {
          refreshElectricalLimitLiveRegions();
        }
      }
    }
  }

  export function handleKeyDown(event) {
    handleOduRuntimeFrequencyInputKeyDown(event);
  }

  export function getWheelDeltaPixels(event, value) {
    if (event.deltaMode === 1) {
      return value * 16;
    }
    if (event.deltaMode === 2) {
      return value * window.innerHeight;
    }
    return value;
  }

  export function getWheelScrollContainer(element) {
    let node = element ? element.parentElement : null;
    while (node && node !== document.body && node !== document.documentElement) {
      const style = window.getComputedStyle(node);
      const canScrollY = /(auto|scroll)/.test(style.overflowY) && node.scrollHeight > node.clientHeight;
      const canScrollX = /(auto|scroll)/.test(style.overflowX) && node.scrollWidth > node.clientWidth;
      if (canScrollY || canScrollX) {
        return node;
      }
      node = node.parentElement;
    }
    return document.scrollingElement || document.documentElement;
  }

  export function handleWheel(event) {
    const input = event.target && event.target.closest
      ? event.target.closest('input[type="number"]')
      : null;
    if (!input || !state.root || !state.root.contains(input) || document.activeElement !== input) {
      return;
    }

    event.preventDefault();
    input.blur();

    const scroller = getWheelScrollContainer(input);
    if (scroller && typeof scroller.scrollBy === "function") {
      scroller.scrollBy({
        left: getWheelDeltaPixels(event, event.deltaX || 0),
        top: getWheelDeltaPixels(event, event.deltaY || 0),
        behavior: "auto",
      });
    }
  }

  export function handleChange(event) {
    if (__OQ_PREVIEW__ && event.target.dataset.oqDevControl === "boiler" && typeof window.__OQ_SET_MOCK_BOILER__ === "function") {
      window.__OQ_SET_MOCK_BOILER__(event.target.value);
      return;
    }

    if (event.target.dataset.oqBackupFileInput) {
      const file = event.target.files && event.target.files[0] ? event.target.files[0] : null;
      event.target.value = "";
      void handleSettingsBackupFileSelection(file);
      return;
    }

    if (event.target.dataset.oqFirmwareUploadFileInput) {
      const file = event.target.files && event.target.files[0] ? event.target.files[0] : null;
      event.target.value = "";
      if (file) {
        updateFirmwareState({
          firmwareAdvancedOpen: true,
          updateManualUploadOpen: true,
          updateManualUploadFile: file,
          updateManualUploadFileName: file.name || "",
          updateManualUploadError: "",
        });
      } else {
        resetFirmwareManualUploadSelection();
      }
      render();
      return;
    }

    if (event.target.dataset.oqEnergyHistoryPeriodInput) {
      if (typeof setEnergyHistoryPeriodValue === "function") {
        setEnergyHistoryPeriodValue(event.target.dataset.oqEnergyHistoryPeriodInput, event.target.value);
      }
      return;
    }

    if (event.target.dataset.oqEnergyHistoryExportMode !== undefined) {
      updateEnergyHistoryState({
        energyHistoryExportMode: normalizeEnergyHistoryExportMode(event.target.value),
        energyHistoryExportError: "",
        energyHistoryExportNotice: "",
      });
      render();
      return;
    }

    const field = event.target.dataset.oqField;
    if (!field) {
      return;
    }

    const entity = ENTITY_DEFS[field];
    if (!entity) {
      return;
    }

    if (entity.domain === "select") {
      if (field === "firmwareUpdateChannel") {
        updateFirmwareState({ firmwareDowngradeConfirmedVersion: "" });
      }
      const value = String(event.target.value);
      if (field === "strategy" && state.quickStartModalOpen) {
        void commitQuickStartStrategySelection(value);
      } else {
        commitSelect(field, value);
      }
      return;
    }

    if (entity.domain === "number") {
      if (event.target.dataset.oqRangeRole && Number(event.target.value) === 0) {
        const minKey = field.replace("MaxHz", "MinHz");
        void disableRange(minKey, minKey.replace("MinHz", "MaxHz"));
        return;
      }
      if (field === "electricalCurrentLimit") {
        void requestElectricalLimitChange(event.target.value);
        return;
      }
      commitNumber(field, event.target.value);
      return;
    }

    if (entity.domain === "text") {
      commitText(field, event.target.value);
      return;
    }

    if (entity.domain === "time") {
      const normalized = normalizeTimeValue(event.target.value);
      if (!normalized) {
        state.controlError = `${entity.name} verwacht tijd als HH:MM.`;
        render();
        return;
      }
      commitTime(field, normalized);
      return;
    }

    if (entity.domain === "datetime") {
      const normalized = normalizeDateTimeValue(event.target.value);
      if (!normalized) {
        state.controlError = `${entity.name} verwacht datum en tijd.`;
        render();
        return;
      }
      commitDateTime(field, normalized);
      return;
    }

  }

  export function handleClick(event) {
    const dateTimeControl = event.target.closest(".oq-settings-control--time, .oq-settings-control--datetime");
    if (dateTimeControl) {
      const pickerInput = dateTimeControl.querySelector('input[data-oq-field]');
      if (pickerInput && (pickerInput.type === "time" || pickerInput.type === "datetime-local") && typeof pickerInput.showPicker === "function") {
        try {
          pickerInput.showPicker();
        } catch (_error) {
          // Ignore browsers that block this call.
        }
      }
    }

    const infoButton = event.target.closest('[data-oq-action="toggle-settings-info"]');
    const infoWrap = event.target.closest("[data-oq-settings-info]");
    const helperHub = event.target.closest(".oq-helper-hub");
    const controlReplayPeriodMenu = event.target.closest("[data-oq-control-replay-period-menu]");
    const modalBackdrop = event.target.closest("[data-oq-modal]");
    if (infoButton) {
      const infoId = infoButton.dataset.infoId || "";
      state.settingsInfoOpen = state.settingsInfoOpen === infoId ? "" : infoId;
      render();
      return;
    }

    const button = event.target.closest("[data-oq-action]");
    const clickedOutsideInterfacePanel = state.interfacePanelOpen && !helperHub;
    if (!button) {
      let shouldRender = false;
      if (state.settingsInfoOpen && !infoWrap) {
        state.settingsInfoOpen = "";
        shouldRender = true;
      }
      if (clickedOutsideInterfacePanel) {
        setInterfacePanelOpen(false);
        shouldRender = true;
      }
      if (state.controlReplayPeriodMenuOpen && !controlReplayPeriodMenu) {
        state.controlReplayPeriodMenuOpen = false;
        state.controlReplayCustomPeriodOpen = false;
        shouldRender = true;
      }
      if (modalBackdrop && event.target === modalBackdrop) {
        if (modalBackdrop.dataset.oqModal === "quickstart-forced") {
          return;
        }
        if (state.updateModalOpen) {
          updateFirmwareState({
            updateModalOpen: false,
            firmwareAdvancedOpen: false,
            firmwareConnectionSwitchOpen: false,
            firmwareTopologySwitchOpen: false,
            updateManualUploadOpen: false,
            updateTestFirmwareOpen: false,
            firmwareConnectionSwitchConfirmed: false,
            firmwareTopologySwitchConfirmed: false,
            firmwareDowngradeConfirmedVersion: "",
          });
          resetFirmwareManualUploadSelection();
          resetFirmwareTestSelection();
          shouldRender = true;
        }
        if (state.systemModal) {
          clearSettingsBackupDraft();
          stopLoginAuthStatusPolling();
          state.systemModal = "";
          shouldRender = true;
        }
      }
      if (shouldRender) {
        render();
      }
      return;
    }
    if (clickedOutsideInterfacePanel && button.dataset.oqAction !== "toggle-interface-panel") {
      setInterfacePanelOpen(false);
    }

    const action = button.dataset.oqAction;
    if (action === "disable-range") {
      const minKey = button.dataset.oqRangeKey || "";
      const maxKey = minKey.replace("MinHz", "MaxHz");
      if (ENTITY_DEFS[minKey]?.domain === "number" && ENTITY_DEFS[maxKey]?.domain === "number") {
        void disableRange(minKey, maxKey);
      }
      return;
    }
    if (actionDelegates.some((delegate) => delegate(action, button, event))) {
      return;
    }
    reportUnknownAction(action, button);

  }

  export function updateControlReplayGraphMinute(rawMinute) {
    const minute = Math.max(0, Math.min(1440, Math.round(rawMinute / 5) * 5));
    if (!Number.isNaN(minute) && state.controlReplayGraphMinute !== minute) {
      state.controlReplayGraphMinute = minute;
      render();
    }
  }

  export function updateControlReplayGraphMinuteFromPointer(clientX, scrubber) {
    const control = scrubber || state.root?.querySelector("[data-oq-control-replay-scrub]");
    if (!control) {
      return;
    }
    const rect = control.getBoundingClientRect();
    if (!rect.width) {
      return;
    }
    const ratio = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
    updateControlReplayGraphMinute(ratio * 1440);
  }

  export function handlePointerDown(event) {
    const replayScrubber = event.target.closest("[data-oq-control-replay-scrub]");
    if (replayScrubber) {
      state.controlReplayScrubbing = true;
      event.preventDefault();
      updateControlReplayGraphMinuteFromPointer(event.clientX, replayScrubber);
      return;
    }

    const point = event.target.closest("[data-curve-key]");
    if (!point || !isCurveMode()) {
      return;
    }

    state.draggingCurveKey = point.dataset.curveKey || "";
    updateCurveDraftFromPointer(event.clientY);
  }

  export function handlePointerMove(event) {
    if (typeof handleEnergyHistoryPointerMove === "function") {
      handleEnergyHistoryPointerMove(event);
    }
    if (state.controlReplayScrubbing) {
      event.preventDefault();
      updateControlReplayGraphMinuteFromPointer(event.clientX);
      return;
    }
    if (!state.draggingCurveKey) {
      return;
    }
    updateCurveDraftFromPointer(event.clientY);
  }

  export function handlePointerUp() {
    if (state.controlReplayScrubbing) {
      state.controlReplayScrubbing = false;
      return;
    }

    if (!state.draggingCurveKey) {
      return;
    }

    const key = state.draggingCurveKey;
    const value = normalizeNumber(key, getEntityValue(key));
    state.draggingCurveKey = "";
    commitNumber(key, value, "Curvepunt bijgewerkt.");
  }
