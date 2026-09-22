import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { QUICK_STEPS, STRATEGY_OPTION_CURVE, STRATEGY_OPTION_POWER_HOUSE } from "../core/config.js";
import { isCurveMode } from "../core/domain-helpers.js";
import { formatValue, getEntityValue, toTimeInputValue } from "../core/entity-store.js";
import { createScrollKeeper } from "../core/scroll-keeper.js";
import { renderModalShell } from "../core/modal-shell.js";
import { state } from "../core/state.js";
import { formatNumber, optionLabel, t } from "../i18n/index.js";
import { getDeviceMeta, getFirmwareBuildConnection, getInstallationTopology } from "./device-context.js";
import { getFirmwareBuildSwitchModel, getFirmwareChannelLabel, getFirmwareCurrentVersion, getFirmwareLatestVersion, getFirmwareProgressModel, getFirmwareUpdateEntity, isFirmwareEntityAlignedWithChannel, isFirmwareUpdateEntityForBuild, isQuickStartSetupFirmwareCurrent, reconcileStoredQuickStartSetupInstall } from "./firmware-update.js";
import { getOduGenerationDetectionModel } from "./odu-generation-ui.js";
import { formatSettingsOptionLabel, renderSettingsFieldCard, renderSettingsInfoToggle } from "../settings/controls.js";
import { renderCurveGraph, renderFlowSettingsFields, renderHeatingCurveProfileField, renderHeatingStrategyExplainCards, renderPowerHouseAdvancedField, renderPowerHouseBaseFields, renderSettingsCurveInputs, renderStrategySelectionFields } from "../settings/heating.js";
import { getHeatingEnableAdvice } from "../core/heating-strategy-matrix.js";
import { renderBoilerCvFields, renderHpGenerationField } from "../settings/installation.js";
import { renderSilentSettingsGrid } from "../settings/silent.js";
import { renderWaterSettingsFields } from "../settings/water.js";
import { escapeHtml } from "../core/html.js";
import { renderUsageTelemetryConsent, renderUsageTelemetryDisclosure } from "./usage-telemetry.js";
import { renderPerformanceTelemetryConsent, renderPerformanceTelemetryDisclosure } from "./performance-telemetry.js";

  export function getQuickStartSetupModel() {
    const currentTopology = getInstallationTopology();
    const currentConnection = getFirmwareBuildConnection() || (hasEntity("preferredConnection") ? "wifi" : "");
    const currentKey = `${currentTopology}:${currentConnection}`;
    const selectedKey = state.quickStartSetupDraft || currentKey;
    const [targetTopology, targetConnection] = selectedKey.split(":");
    const switchModel = getFirmwareBuildSwitchModel(targetTopology, targetConnection);
    return {
      ...switchModel,
      currentConnection,
      currentKey,
      selectedKey,
      changes: selectedKey !== currentKey,
      targetIsDuo: targetTopology === "duo",
      targetIsEthernet: targetConnection === "eth",
    };
  }

  export function renderSetupWorkspace() {
    const model = getQuickStartSetupModel();
    const progress = getFirmwareProgressModel();
    const busy = Boolean(progress || state.updateInstallBusy);
    const firmwareEntity = getFirmwareUpdateEntity() || {};
    const mainManifestReady = getFirmwareChannelLabel().toLowerCase() === "main"
      && isFirmwareEntityAlignedWithChannel(firmwareEntity, "main")
      && isFirmwareUpdateEntityForBuild(model.targetBuildLabel, firmwareEntity);
    const currentVersion = getFirmwareCurrentVersion(firmwareEntity) || t("common.unknown");
    const mainVersion = mainManifestReady ? getFirmwareLatestVersion(firmwareEntity) || t("common.unknown") : t("quickStart.setupPendingCheck");
    const firmwareCurrent = mainManifestReady && isQuickStartSetupFirmwareCurrent(model);
    const canKeepCurrentSoftware = model.available
      && model.currentTopology === model.targetTopology
      && model.currentConnection === model.targetConnection
      && !firmwareCurrent;
    const unifiedNetworkBuild = hasEntity("preferredConnection");
    const options = [
        ["single:wifi", "Single · Wi-Fi", t("quickStart.setupOptSingleWifiCopy")],
        ["single:eth", "Single · Ethernet", t("quickStart.setupOptSingleEthCopy")],
        ["duo:wifi", "Duo · Wi-Fi", t("quickStart.setupOptDuoWifiCopy")],
        ["duo:eth", "Duo · Ethernet", t("quickStart.setupOptDuoEthCopy")],
      ].filter(([key]) => !unifiedNetworkBuild || key.endsWith(`:${model.currentConnection}`));
    const requirements = [
      model.targetIsDuo ? t("quickStart.setupReqDuo") : t("quickStart.setupReqSingle"),
      model.targetIsEthernet ? t("quickStart.setupReqEth") : t("quickStart.setupReqWifi"),
      firmwareCurrent
        ? t("quickStart.setupReqCurrent")
        : t("quickStart.setupReqOta"),
    ];

    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("setup"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("setup"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(unifiedNetworkBuild
          ? t("quickStart.setupUnifiedCopy")
          : t("quickStart.setupQEditionCopy"))}</p>
        <div class="oq-helper-fields">
          ${options.map(([key, title, copy]) => {
            const selected = model.selectedKey === key;
            const current = model.currentKey === key;
            return `
              <button
                class="oq-helper-field oq-helper-field--step${selected ? " is-current" : ""}"
                type="button"
                data-oq-action="select-quickstart-setup"
                data-setup-target="${escapeHtml(key)}"
                aria-pressed="${selected ? "true" : "false"}"
                ${busy ? "disabled" : ""}
              >
                <div class="oq-helper-field-step-head">
                  <h3>${escapeHtml(title)}</h3>
                  ${current ? `<span class="oq-helper-field-step-state">${escapeHtml(t("quickStart.statusActive"))}</span>` : ""}
                </div>
                <p>${escapeHtml(copy)}</p>
              </button>
            `;
          }).join("")}
        </div>
        <div class="oq-firmware-advanced-detail">
            ${progress ? `
              <div class="oq-helper-modal-progress" aria-live="polite">
                <div class="oq-helper-modal-progress-head">
                  <strong>${escapeHtml(progress.phaseLabel)}</strong>
                  <span>${escapeHtml(`${progress.percent}%`)}</span>
                </div>
                <div class="oq-helper-modal-progress-track" aria-hidden="true">
                  <span class="oq-helper-modal-progress-fill" style="width:${Math.max(0, Math.min(100, progress.percent))}%"></span>
                </div>
                <p class="oq-helper-modal-note">${escapeHtml(progress.copy)}</p>
              </div>
            ` : ""}
            <div class="oq-helper-modal-grid">
              <div class="oq-helper-modal-row"><span class="oq-helper-modal-label">${escapeHtml(t("quickStart.setupCurrentBuild"))}</span><strong class="oq-helper-modal-value">${escapeHtml(model.currentBuildLabel)}</strong></div>
              <div class="oq-helper-modal-row"><span class="oq-helper-modal-label">${escapeHtml(t("quickStart.setupChosenBuild"))}</span><strong class="oq-helper-modal-value">${escapeHtml(model.targetBuildLabel)}</strong></div>
              <div class="oq-helper-modal-row"><span class="oq-helper-modal-label">${escapeHtml(t("quickStart.setupCurrentVersion"))}</span><strong class="oq-helper-modal-value">${escapeHtml(currentVersion)}</strong></div>
              <div class="oq-helper-modal-row"><span class="oq-helper-modal-label">${escapeHtml(t("quickStart.setupLatestMain"))}</span><strong class="oq-helper-modal-value">${escapeHtml(mainVersion)}</strong></div>
            </div>
            <p class="oq-helper-modal-note">${escapeHtml(firmwareCurrent
              ? t("quickStart.setupFirmwareOk")
              : canKeepCurrentSoftware
                ? t("quickStart.setupKeepCurrent")
                : t("quickStart.setupFirmwareCheck"))}</p>
            <label class="oq-helper-modal-check">
              <input type="checkbox" data-oq-quickstart-setup-confirm="true" ${state.quickStartSetupConfirmed ? "checked" : ""} ${busy ? "disabled" : ""}>
              <span>${escapeHtml(requirements.join(" "))}</span>
            </label>
            <div class="oq-firmware-advanced-footer">
              <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="install-quickstart-setup" ${busy || !state.quickStartSetupConfirmed || !model.canInstall ? "disabled" : ""}>
                ${escapeHtml(busy
                  ? t("quickStart.setupChecking")
                  : firmwareCurrent
                    ? t("quickStart.setupConfirmCurrent")
                    : canKeepCurrentSoftware
                      ? t("quickStart.setupCheckInstall")
                      : t("quickStart.setupConfirmCheck"))}
              </button>
              ${canKeepCurrentSoftware ? `
                <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="keep-current-quickstart-setup" ${busy || !state.quickStartSetupConfirmed ? "disabled" : ""}>
                  ${escapeHtml(t("quickStart.setupKeepSoftware"))}
                </button>
              ` : ""}
            </div>
            ${!model.canInstall && !busy && !canKeepCurrentSoftware ? `<p class="oq-helper-modal-note oq-helper-modal-note--muted">${escapeHtml(
              !model.targetEntityAvailable || !model.installActionAvailable || !model.mainChannelAvailable
                ? t("quickStart.setupLoadingFirmware")
                : t("quickStart.setupMissingTarget"),
            )}</p>` : ""}
        </div>
        ${state.controlNotice ? `<p class="oq-helper-notice">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${state.controlError ? `<p class="oq-helper-error">${escapeHtml(state.controlError)}</p>` : ""}
      </section>
    `;
  }

  export function renderGenerationWorkspace(mode = "wizard") {
    const pickerMode = mode === "picker";
    if (pickerMode) {
      return `
        <section class="oq-helper-panel oq-helper-panel--flush">
          ${renderHpGenerationField()}
          <div class="oq-helper-actions oq-settings-generation-actions">
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-quickstart-modal">${escapeHtml(t("common.done"))}</button>
          </div>
        </section>
      `;
    }

    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("generation"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("generation"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(getQuickStepCopyById("generation"))}</p>
        ${renderHpGenerationField()}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function normalizeQuickStartCicFeedUrl(rawValue) {
    const value = String(rawValue || "").trim();
    if (!value) {
      return "";
    }

    try {
      const parsed = new URL(/^[a-z][a-z0-9+.-]*:\/\//i.test(value) ? value : `http://${value}`);
      if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
        return "";
      }
      if (!parsed.port) {
        parsed.port = "8080";
      }
      if (!parsed.pathname || parsed.pathname === "/") {
        parsed.pathname = "/beta/feed/data.json";
      }
      return parsed.toString();
    } catch (_error) {
      return "";
    }
  }

  export function getQuickStartCicFeedUrlModel() {
    const configuredUrl = String(getEntityValue("cicFeedUrl") || "").trim();
    const draftUrl = state.quickStartCicFeedUrlDraft === null
      ? configuredUrl
      : String(state.quickStartCicFeedUrlDraft || "");
    return {
      configuredUrl,
      draftUrl,
      normalizedDraftUrl: normalizeQuickStartCicFeedUrl(draftUrl),
    };
  }

  export function renderQuickStartCicFeedUrlField(model, busy) {
    return `
      <article class="oq-helper-surface oq-settings-field oq-settings-field--span-2" data-oq-settings-field="quickStartCicFeedUrl">
        <div class="oq-settings-field-head">
          <h3>${escapeHtml(t("quickStart.cicTitle"))}</h3>
          ${renderSettingsInfoToggle("quickStartCicFeedUrl", t("quickStart.cicTitle"), t("quickStart.cicCopy"))}
        </div>
        <div class="oq-settings-field-control">
          <label class="oq-settings-control oq-settings-control--text">
            <input
              class="oq-helper-input oq-settings-integration-url-input"
              type="text"
              data-oq-quickstart-cic-url
              value="${escapeHtml(model.draftUrl)}"
              placeholder="192.168.2.117"
              autocomplete="off"
              spellcheck="false"
              ${busy ? "disabled" : ""}
            >
          </label>
          ${model.draftUrl && !model.normalizedDraftUrl ? `<p class="oq-settings-source-warning">${escapeHtml(t("quickStart.cicInvalid"))}</p>` : ""}
          ${model.normalizedDraftUrl ? `<p class="oq-settings-action-note">${escapeHtml(t("quickStart.cicWillSet", { url: model.normalizedDraftUrl }))}</p>` : ""}
        </div>
      </article>
    `;
  }

  export function normalizeQuickStartHardwareProfile(value) {
    const normalized = String(value || "").trim().toLowerCase();
    if (normalized === "heatpump_controller_q" || normalized.includes("q-edition") || normalized.includes("controller q")) {
      return "heatpump_controller_q";
    }
    return "";
  }

  export function getQuickStartHardwareProfileModel() {
    let profile = normalizeQuickStartHardwareProfile(getEntityValue("hardwareProfileText"));
    let inferred = false;
    if (!profile) {
      profile = normalizeQuickStartHardwareProfile(getDeviceMeta().hardwareProfile);
    }
    if (!profile && hasEntity("qFlowSource")) {
      profile = "heatpump_controller_q";
      inferred = true;
    }

    return {
      profile,
      inferred,
      isQEdition: profile === "heatpump_controller_q",
      hardwareKnown: Boolean(profile),
      hardwareLabel: profile === "heatpump_controller_q"
        ? "Heatpump Controller Q-edition"
        : t("quickStart.unknownHardware"),
    };
  }

  export function getQuickStartFlowSourceModel() {
    const generation = String(getEntityValue("hpGeneration") || "").trim();
    const hardware = getQuickStartHardwareProfileModel();
    const isV1 = generation === "V1";
    const { isQEdition, hardwareKnown } = hardware;
    const requiresCic = false;
    const qFlowTarget = isQEdition ? (isV1 ? "Local" : "Outdoor unit") : "";
    const flowSourceTarget = "Outdoor unit";
    const currentFlowSource = String(getEntityValue("flowSource") || "").trim();
    const currentQFlowSource = String(getEntityValue("qFlowSource") || "").trim();
    const cicEnabled = isEntityActive("cicPollingEnabled");
    const cicFeedOk = isEntityActive("cicJsonFeedOk");
    const cicStale = isEntityActive("cicDataStale");
    const cicUrl = getQuickStartCicFeedUrlModel();
    const sourceApplied = currentFlowSource === flowSourceTarget
      && (!qFlowTarget || currentQFlowSource === qFlowTarget);
    const configurationApplied = requiresCic
      ? sourceApplied && cicEnabled && Boolean(cicUrl.configuredUrl)
      : sourceApplied;
    const sensorKey = requiresCic
      ? "cicFlowrate"
      : isQEdition && isV1
        ? "controllerFlow"
        : getInstallationTopology() === "duo"
          ? "flowLocal"
          : "hp1Flow";
    const flowValue = getEntityNumericValue(sensorKey);
    const flowAvailable = Number.isFinite(flowValue);
    const flowTestActive = isEntityActive("quickFlowTest");

    let status = hardwareKnown ? requiresCic ? t("quickStart.flowSourceStatusConfigure") : t("quickStart.flowSourceStatusActivate") : t("quickStart.flowSourceStatusUnknownHardware");
    if (requiresCic && configurationApplied) {
      status = cicFeedOk && flowAvailable
        ? flowValue > 0 ? t("quickStart.flowSourceStatusValid") : t("quickStart.flowSourceStatusNoCirculation")
        : cicStale
          ? t("quickStart.flowSourceStatusNoCicData")
          : cicFeedOk
            ? t("quickStart.flowSourceStatusConnectedWait")
            : t("quickStart.flowSourceStatusCheckConnection");
    } else if (!requiresCic && configurationApplied) {
      status = flowAvailable
        ? flowValue > 0 ? t("quickStart.flowSourceStatusValid") : t("quickStart.flowSourceStatusNoCirculation")
        : t("quickStart.flowSourceStatusWaitFlow");
    }

    const sourceLabel = requiresCic
      ? t("quickStart.cicTitle")
      : isQEdition && isV1
        ? t("quickStart.flowSourceLabelLocal")
        : t("quickStart.flowSourceLabelOdu");
    const explanation = requiresCic
      ? t("quickStart.flowSourceExplainCic")
      : isQEdition && isV1
        ? t("quickStart.flowSourceExplainLocalV1")
        : t("quickStart.flowSourceExplainOdu", { generation: generation || "V1.5/V2" });

    return {
      generation,
      hardwareLabel: hardware.hardwareLabel,
      requiresCic,
      qFlowTarget,
      flowSourceTarget,
      configurationApplied,
      sourceLabel,
      explanation,
      status,
      flowValue,
      flowAvailable,
      flowTestActive,
      canRunFlowTest: configurationApplied,
      ...cicUrl,
      canApply: hardwareKnown
        && hasEntity("flowSource")
        && (!qFlowTarget || hasEntity("qFlowSource"))
        && (!requiresCic || (hasEntity("cicPollingEnabled") && hasEntity("cicFeedUrl") && Boolean(cicUrl.normalizedDraftUrl))),
    };
  }

  export function getQuickStartThermostatSourceModel() {
    const hardware = getQuickStartHardwareProfileModel();
    const { isQEdition } = hardware;
    const currentRoomTempSource = String(getEntityValue("roomTempSource") || "").trim();
    const currentRoomSetpointSource = String(getEntityValue("roomSetpointSource") || "").trim();
    const pairedCurrentSource = currentRoomTempSource === currentRoomSetpointSource
      && ["CIC", "OT thermostat", "HA input"].includes(currentRoomTempSource)
      ? currentRoomTempSource
      : "";
    const selectedSource = isQEdition
      ? "OT thermostat"
      : state.quickStartThermostatSourceDraft || (pairedCurrentSource === "CIC" || pairedCurrentSource === "HA input" ? pairedCurrentSource : "CIC");
    const cicUrl = getQuickStartCicFeedUrlModel();
    const sourceApplied = currentRoomTempSource === selectedSource && currentRoomSetpointSource === selectedSource;
    const configurationApplied = sourceApplied
      && (selectedSource !== "OT thermostat" || isEntityActive("otEnabled"))
      && (selectedSource !== "CIC" || (isEntityActive("cicPollingEnabled") && Boolean(cicUrl.configuredUrl)));
    const sourceValueKeys = selectedSource === "OT thermostat"
      ? ["otRoomTemp", "otRoomSetpoint"]
      : selectedSource === "CIC"
        ? ["cicRoomTemp", "cicRoomSetpoint"]
        : ["roomTempHa", "roomSetpointHa"];
    const roomTempValue = getEntityNumericValue(sourceValueKeys[0]);
    const roomSetpointValue = getEntityNumericValue(sourceValueKeys[1]);
    const valuesAvailable = Number.isFinite(roomTempValue) && Number.isFinite(roomSetpointValue);
    const sourceHealthy = selectedSource === "OT thermostat"
      ? isEntityActive("otEnabled") && !isEntityActive("otLinkProblem") && valuesAvailable
      : selectedSource === "CIC"
        ? isEntityActive("cicJsonFeedOk") && !isEntityActive("cicDataStale") && valuesAvailable
        : isEntityActive("roomTempHaValid") && isEntityActive("roomSetpointHaValid") && valuesAvailable;

    let status = isQEdition ? t("quickStart.thermostatStatusActivate") : t("quickStart.thermostatStatusUnknown");
    if (configurationApplied) {
      status = sourceHealthy ? t("quickStart.thermostatStatusValid") : selectedSource === "OT thermostat"
        ? t("quickStart.thermostatStatusCheckOt")
        : selectedSource === "CIC"
          ? t("quickStart.thermostatStatusCheckCic")
          : t("quickStart.thermostatStatusCheckHa");
    }

    const sourceLabel = selectedSource === "OT thermostat"
      ? t("quickStart.thermostatLabelOt")
      : selectedSource === "CIC"
        ? t("quickStart.cicTitle")
        : t("quickStart.thermostatLabelHa");
    const explanation = isQEdition
      ? t("quickStart.thermostatExplainQ")
      : selectedSource === "CIC"
        ? t("quickStart.thermostatExplainCic")
        : t("quickStart.thermostatExplainHa");

    return {
      hardwareLabel: hardware.hardwareLabel,
      isQEdition,
      selectedSource,
      sourceLabel,
      explanation,
      configurationApplied,
      status,
      roomTempValue,
      roomSetpointValue,
      valuesAvailable,
      ...cicUrl,
      canApply: isQEdition
        && hasEntity("roomTempSource")
        && hasEntity("roomSetpointSource")
        && (selectedSource !== "OT thermostat" || hasEntity("otEnabled"))
        && (selectedSource !== "CIC" || (hasEntity("cicPollingEnabled") && hasEntity("cicFeedUrl") && Boolean(cicUrl.normalizedDraftUrl))),
    };
  }

  export function renderFlowSourceWorkspace() {
    const model = getQuickStartFlowSourceModel();
    const busy = state.busyAction === "quickstart-flow-source" || state.busyAction === "quickstart-flow-refresh";
    const flowTestBusy = state.busyAction === "quickstart-flow-test-start" || state.busyAction === "quickstart-flow-test-abort";
    const controlsBusy = busy || flowTestBusy || model.flowTestActive;
    const statusClass = model.status === t("quickStart.flowSourceStatusValid") || model.status === t("quickStart.flowSourceStatusNoCirculation") ? " is-active" : "";
    const flowLabel = model.flowAvailable ? `${formatNumber(Math.round(model.flowValue), { maximumFractionDigits: 0 })} L/h` : t("quickStart.flowNoValue");
    const cicField = model.requiresCic ? renderQuickStartCicFeedUrlField(model, controlsBusy) : "";

    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("flow-source"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("flow-source"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(t("quickStart.flowCopy"))}</p>
        <div class="oq-settings-grid oq-settings-grid--quickstart">
          ${renderSettingsFieldCard(
            "quickStartFlowSource",
            t("quickStart.flowFieldTitle"),
            model.explanation,
            `
              <div class="oq-settings-quickstart-status">
                <div class="oq-settings-quickstart-status-row">
                  <div>
                    <p class="oq-settings-quickstart-status-label">${escapeHtml(model.hardwareLabel)} · Quatt ${escapeHtml(model.generation || t("quickStart.unknownGeneration"))}</p>
                    <strong class="oq-settings-quickstart-status-value">${escapeHtml(model.sourceLabel)}</strong>
                    <p class="oq-settings-quickstart-status-copy">${escapeHtml(model.explanation)}</p>
                  </div>
                </div>
                <div class="oq-settings-source-rows">
                  <div class="oq-settings-source-row${statusClass}"><span>${escapeHtml(t("quickStart.flowStatusLabel"))}</span><strong>${escapeHtml(model.status)}</strong></div>
                  <div class="oq-settings-source-row"><span>${escapeHtml(t("quickStart.flowCurrentFlow"))}</span><strong>${escapeHtml(flowLabel)}</strong></div>
                </div>
              </div>
            `,
            "oq-settings-field--span-2",
          )}
          ${cicField}
        </div>
        <div class="oq-helper-actions">
          <button
            class="oq-helper-button oq-helper-button--primary"
            type="button"
            data-oq-action="apply-quickstart-flow-source"
            ${controlsBusy || !model.canApply ? "disabled" : ""}
          >
            ${escapeHtml(state.busyAction === "quickstart-flow-source" ? t("quickStart.flowSaveBusy") : model.configurationApplied ? t("quickStart.flowSaveAgain") : model.requiresCic ? t("quickStart.flowSaveCic") : t("quickStart.flowActivate"))}
          </button>
          <button
            class="oq-helper-button oq-helper-button--ghost"
            type="button"
            data-oq-action="refresh-quickstart-flow-signal"
            ${controlsBusy || !model.configurationApplied ? "disabled" : ""}
          >
            ${escapeHtml(state.busyAction === "quickstart-flow-refresh" ? t("quickStart.flowCheckBusy") : t("quickStart.flowCheckAgain"))}
          </button>
          ${model.canRunFlowTest ? `
            <button
              class="oq-helper-button ${model.flowTestActive ? "" : "oq-helper-button--ghost"}"
              type="button"
              data-oq-action="${model.flowTestActive ? "abort-quickstart-flow-test" : "start-quickstart-flow-test"}"
              ${busy || flowTestBusy ? "disabled" : ""}
            >
              ${escapeHtml(flowTestBusy
                ? model.flowTestActive ? t("quickStart.flowTestStopBusy") : t("quickStart.flowTestStartBusy")
                : model.flowTestActive
                  ? t("quickStart.flowTestStop")
                  : t("quickStart.flowTestStart"))}
            </button>
          ` : ""}
        </div>
        <p class="oq-settings-action-note">${escapeHtml(model.flowTestActive
          ? t("quickStart.flowNoteActive")
          : t("quickStart.flowNoteIdle"))}</p>
        ${renderQuickStartStepNav({
          nextDisabled: !model.configurationApplied || model.flowTestActive || flowTestBusy,
          nextDisabledLabel: flowTestBusy
            ? t("quickStart.flowWait")
            : model.flowTestActive
              ? t("quickStart.flowTestRunning")
              : model.requiresCic ? t("quickStart.flowSaveFirst") : t("quickStart.flowActivateFirst"),
        })}
      </section>
    `;
  }

  export function renderThermostatSourceWorkspace() {
    const model = getQuickStartThermostatSourceModel();
    const busy = state.busyAction === "quickstart-thermostat-source";
    const statusClass = model.status === t("quickStart.thermostatStatusValid") ? " is-active" : "";
    const cicField = model.selectedSource === "CIC" ? renderQuickStartCicFeedUrlField(model, busy) : "";
    const haNote = model.selectedSource === "HA input" ? `
      <article class="oq-helper-surface oq-settings-field oq-settings-field--span-2">
        <div class="oq-settings-field-head"><h3>${escapeHtml(t("quickStart.haContractTitle"))}</h3></div>
        <div class="oq-settings-field-control">
          <p class="oq-settings-action-note">${t("quickStart.haContractCopy", {
            temperature: "<strong>sensor.openquatt_ext_room_temperature</strong>",
            setpoint: "<strong>sensor.openquatt_ext_room_setpoint</strong>",
            valid: "<strong>_valid</strong>",
          })}</p>
          <p class="oq-settings-action-note"><a href="https://github.com/OpenQuatt/OpenQuatt/tree/main/docs/dashboard#optioneel-dynamische-bronselectie-via-home-assistant" target="_blank" rel="noreferrer">${escapeHtml(t("quickStart.haContractLink"))}</a>.</p>
        </div>
      </article>
    ` : "";

    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("thermostat-source"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("thermostat-source"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(t("quickStart.thermostatCopy"))}</p>
        <div class="oq-settings-grid oq-settings-grid--quickstart">
          ${renderSettingsFieldCard(
            "quickStartThermostatSourceStatus",
            model.isQEdition ? t("quickStart.thermostatFixedTitle") : t("quickStart.thermostatChosenTitle"),
            model.explanation,
            `
              <div class="oq-settings-quickstart-status">
                <div class="oq-settings-quickstart-status-row">
                  <div>
                    <p class="oq-settings-quickstart-status-label">${escapeHtml(model.hardwareLabel)}</p>
                    <strong class="oq-settings-quickstart-status-value">${escapeHtml(model.sourceLabel)}</strong>
                    <p class="oq-settings-quickstart-status-copy">${escapeHtml(model.explanation)}</p>
                  </div>
                </div>
                <div class="oq-settings-source-rows">
                  <div class="oq-settings-source-row${statusClass}"><span>${escapeHtml(t("quickStart.flowStatusLabel"))}</span><strong>${escapeHtml(model.status)}</strong></div>
                  <div class="oq-settings-source-row"><span>${escapeHtml(t("quickStart.reviewRoomTemp"))}</span><strong>${escapeHtml(Number.isFinite(model.roomTempValue) ? `${formatNumber(model.roomTempValue, { minimumFractionDigits: 1, maximumFractionDigits: 1 })} °C` : t("quickStart.flowNoValue"))}</strong></div>
                  <div class="oq-settings-source-row"><span>${escapeHtml(t("quickStart.reviewRoomSetpoint"))}</span><strong>${escapeHtml(Number.isFinite(model.roomSetpointValue) ? `${formatNumber(model.roomSetpointValue, { minimumFractionDigits: 1, maximumFractionDigits: 1 })} °C` : t("quickStart.flowNoValue"))}</strong></div>
                </div>
              </div>
            `,
            "oq-settings-field--span-2",
          )}
          ${cicField}
          ${haNote}
        </div>
        <div class="oq-helper-actions">
          <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="apply-quickstart-thermostat-source" ${busy || !model.canApply ? "disabled" : ""}>
            ${escapeHtml(busy ? t("quickStart.thermostatSaveBusy") : model.configurationApplied ? t("quickStart.thermostatSaveAgain") : model.selectedSource === "OT thermostat" ? t("quickStart.thermostatActivateOt") : t("quickStart.thermostatSave"))}
          </button>
        </div>
        ${renderQuickStartStepNav({
          nextDisabled: !model.configurationApplied,
          nextDisabledLabel: model.isQEdition ? t("quickStart.flowActivateFirst") : t("quickStart.flowSaveFirst"),
        })}
      </section>
    `;
  }

  export function renderQuickStartModal() {
    if (!state.quickStartModalOpen || state.loadingEntities || state.complete === null || (state.complete && state.quickStartModalMode !== "generation")) {
      return "";
    }

    if (state.quickStartModalMode === "generation") {
      return renderModalShell({
        id: "quickstart-forced",
        titleId: "oq-generation-modal-title",
        kicker: t("quickStart.modalGenKicker"),
        title: t("quickStart.modalGenTitle"),
        copy: t("quickStart.modalGenCopy"),
        copyInHeader: true,
        backdropClass: "oq-helper-modal-backdrop--quickstart",
        className: "oq-helper-modal--wide oq-helper-modal--scrollable",
        sectionAttributes: 'data-oq-quickstart-scroller data-oq-quickstart-step="generation"',
        closeAction: "close-quickstart-modal",
        closeLabel: t("quickStart.modalGenClose"),
        body: renderGenerationWorkspace("picker"),
      });
    }

    return renderModalShell({
      id: "quickstart-forced",
      titleId: "oq-quickstart-modal-title",
      kicker: t("quickStart.modalKicker"),
      title: t("quickStart.modalTitle"),
      copy: t("quickStart.modalCopy"),
      copyInHeader: true,
      backdropClass: "oq-helper-modal-backdrop--quickstart",
      className: "oq-helper-modal--wide oq-helper-modal--quickstart",
      sectionAttributes: `data-oq-quickstart-scroller data-oq-quickstart-step="${escapeHtml(getCurrentQuickStep().id)}"`,
      closeAction: "close-quickstart-modal",
      closeLabel: t("quickStart.modalClose"),
      body: `<div class="oq-helper-grid oq-helper-grid--quickstart oq-helper-grid--quickstart-modal">${renderActiveStep()}${renderQuickStartSidebar()}</div>`,
    });
  }

  export function getQuickStartModalScrollerElement() {
    if (!state.root) {
      return null;
    }
    return state.root.querySelector("[data-oq-quickstart-scroller]");
  }

  const quickStartScrollKeeper = createScrollKeeper({
    getScroller: getQuickStartModalScrollerElement,
    getToken: () => state.quickStartScrollRestoreToken,
    setToken: (token) => { state.quickStartScrollRestoreToken = token; },
    isActive: () => state.quickStartModalOpen,
    getIdentity: (scroller) => String(scroller.dataset.oqQuickstartStep || ""),
    preserveGrowth: true,
    stickToBottom: true,
  });

  export const captureQuickStartScrollState = quickStartScrollKeeper.capture;
  export const queueQuickStartScrollRestore = quickStartScrollKeeper.queue;

  export function renderHeatingEnableQuickStartAdvice() {
    if (!hasEntity("heatingEnableSource")) {
      return "";
    }
    const advice = getHeatingEnableAdvice();
    const deviant = Boolean(advice.deviant);
    return `
      <div class="oq-helper-surface oq-settings-field oq-settings-field--span-2${deviant ? " is-warning" : ""}">
        <div class="oq-settings-field-head">
          <h3>${escapeHtml(t("quickStart.heatingAdviceTitle"))}</h3>
          <p class="oq-settings-action-note" style="margin:0">${escapeHtml(t("quickStart.heatingAdviceCopy"))}</p>
        </div>
        <div class="oq-settings-field-control">
          <button class="oq-helper-button ${deviant ? "oq-helper-button--warning-soft" : "oq-helper-button--ghost"}" type="button" data-oq-action="open-heating-strategy-advice-modal">${deviant ? `<span class="oq-advice-warn-icon"><svg viewBox="0 0 20 18" aria-hidden="true"><path d="M10 1.6 L18.2 16.4 H1.8 Z"/><rect x="9.1" y="5.4" width="1.8" height="5.8" rx="0.9"/><circle cx="10" cy="13.6" r="1.1"/></svg></span> ${escapeHtml(t("quickStart.heatingAdviceAction"))}` : escapeHtml(t("quickStart.heatingAdviceAction"))}</button>
        </div>
      </div>
    `;
  }

  export function renderStrategyWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("strategy"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("strategy"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(getQuickStepCopyById("strategy"))}</p>
        ${renderHeatingStrategyExplainCards()}
        ${renderStrategySelectionFields("oq-settings-grid oq-settings-grid--quickstart")}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderBoilerWorkspace() {
    const boilerConnectionMismatch = isEntityActive("otbConnectionMismatch");
    const boilerConnection = String(getEntityValue("boilerConnection") || "R1");
    const sourcePresent = hasEntity("auxHeatSourcePresent")
      ? isEntityActive("auxHeatSourcePresent")
      : isEntityActive("boilerCvAssistEnabled");
    const otbConnectionStatePresent = hasEntity("otbConnectionState");
    const otbConnectionState = otbConnectionStatePresent
      ? String(getEntityValue("otbConnectionState") || "")
      : "";
    const openthermNotVerified = sourcePresent && boilerConnection === "OpenTherm"
      && otbConnectionStatePresent && otbConnectionState !== "ot_verified";
    const openthermChecking =
      !otbConnectionState ||
      otbConnectionState === "unknown" ||
      otbConnectionState === "ot_checking";
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("boiler"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("boiler"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(t("quickStart.boilerWorkspaceCopy"))}</p>
        ${renderBoilerCvFields("oq-settings-grid oq-settings-grid--quickstart oq-settings-boiler-simple-grid", true)}
        ${renderQuickStartStepNav({
          nextDisabled: boilerConnectionMismatch || openthermNotVerified,
          nextDisabledLabel: openthermNotVerified
            ? openthermChecking ? t("quickStart.boilerChecking") : t("quickStart.boilerCheckConnection")
            : t("quickStart.boilerChooseOt"),
        })}
      </section>
    `;
  }

  export function renderFlowWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("flow"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("flow"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(t("quickStart.flowStepCopy"))}</p>
        ${renderFlowSettingsFields("oq-settings-grid oq-settings-grid--quickstart")}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderHeatingWorkspace() {
    const curveMode = isCurveMode();
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("heating"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(t(curveMode ? "quickStart.heatingCurveTitle" : "quickStart.heatingPhTitle"))}</h2>
        <p class="oq-helper-section-copy">
          ${escapeHtml(t(curveMode ? "quickStart.heatingCurveCopy" : "quickStart.heatingPhCopy"))}
        </p>
        ${curveMode
          ? `
            <div class="oq-settings-grid oq-settings-grid--quickstart">${renderHeatingCurveProfileField()}</div>
            <div class="oq-settings-curve-shell">
              ${renderCurveGraph()}
            </div>
            ${renderSettingsCurveInputs()}
          `
          : `
            ${renderPowerHouseBaseFields("oq-settings-grid oq-settings-grid--quickstart")}
            ${renderPowerHouseAdvancedField()}
          `}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderWaterWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("water"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("water"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(t("quickStart.waterWorkspaceCopy"))}</p>
        ${renderWaterSettingsFields("oq-settings-grid oq-settings-grid--quickstart", { includeSensorCorrections: false })}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderSilentWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("silent"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("silent"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(t("quickStart.silentWorkspaceCopy"))}</p>
        ${renderSilentSettingsGrid("oq-settings-grid oq-settings-grid--quickstart")}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderUsageTelemetryWorkspace() {
    const enabled = isEntityActive("usageTelemetryEnabled");
    const choiceConfigured = isEntityActive("usageTelemetryChoiceConfigured");
    const busy = state.loadingEntities || Boolean(state.busyAction);
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("usage-telemetry"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("usage-telemetry"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(t("quickStart.usageCopy"))}</p>
        ${renderUsageTelemetryConsent({ enabled, busy })}
        ${renderUsageTelemetryDisclosure()}
        ${state.controlNotice ? `<p class="oq-helper-notice">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${state.controlError ? `<p class="oq-helper-error">${escapeHtml(state.controlError)}</p>` : ""}
        ${state.controlError ? `
          <div class="oq-helper-actions">
            <button class="oq-helper-button" type="button" data-oq-action="retry-usage-telemetry-choice" ${busy ? "disabled" : ""}>${escapeHtml(t("quickStart.usageRetrySave"))}</button>
          </div>
        ` : ""}
        ${!choiceConfigured && !busy ? `
          <div class="oq-helper-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="confirm-no-usage-telemetry">${escapeHtml(t("quickStart.usageConfirmNoShare"))}</button>
          </div>
        ` : ""}
        ${renderQuickStartStepNav({
          nextDisabled: busy || !choiceConfigured || Boolean(state.controlError),
          nextDisabledLabel: busy || !choiceConfigured ? t("quickStart.usageSaving") : t("quickStart.usageCheckChoice"),
        })}
      </section>
    `;
  }

  export function renderPerformanceTelemetryWorkspace() {
    const enabled = isEntityActive("performanceTelemetryEnabled");
    const choiceConfigured = isEntityActive("performanceTelemetryChoiceConfigured");
    const busy = state.loadingEntities || Boolean(state.busyAction);
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("performance-telemetry"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("performance-telemetry"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(t("quickStart.perfCopy"))}</p>
        ${renderPerformanceTelemetryConsent({ enabled, busy })}
        ${renderPerformanceTelemetryDisclosure()}
        ${state.controlNotice ? `<p class="oq-helper-notice">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${state.controlError ? `<p class="oq-helper-error">${escapeHtml(state.controlError)}</p>` : ""}
        ${state.controlError ? `
          <div class="oq-helper-actions">
            <button class="oq-helper-button" type="button" data-oq-action="retry-performance-telemetry-choice" ${busy ? "disabled" : ""}>${escapeHtml(t("quickStart.usageRetrySave"))}</button>
          </div>
        ` : ""}
        ${!choiceConfigured && !busy ? `
          <div class="oq-helper-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="confirm-no-performance-telemetry">${escapeHtml(t("quickStart.usageConfirmNoShare"))}</button>
          </div>
        ` : ""}
        ${renderQuickStartStepNav({
          nextDisabled: busy || !choiceConfigured || Boolean(state.controlError),
          nextDisabledLabel: busy || !choiceConfigured ? t("quickStart.usageSaving") : t("quickStart.usageCheckChoice"),
        })}
      </section>
    `;
  }

  export function renderConfirmWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("confirm"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(getQuickStepTitleById("confirm"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(getQuickStepCopyById("confirm"))}</p>
        ${renderConfirmReviewCards()}
        <section class="oq-quickstart-history" aria-label="${escapeHtml(t("quickStart.confirmHistoryTitle"))}">
          <div>
            <p class="oq-quickstart-history-label">${escapeHtml(t("quickStart.confirmHistoryTitle"))}</p>
            <p class="oq-quickstart-history-copy">${escapeHtml(t("quickStart.confirmHistoryCopy"))}</p>
          </div>
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="open-history-storage-modal">
            ${escapeHtml(t("quickStart.confirmHistoryAction"))}
          </button>
        </section>
        ${state.controlNotice ? `<p class="oq-helper-notice">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${state.controlError ? `<p class="oq-helper-error">${escapeHtml(state.controlError)}</p>` : ""}
        <div class="oq-helper-actions oq-helper-actions--step">
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="previous-step" ${state.busyAction ? "disabled" : ""}>
            ${escapeHtml(t("quickStart.navPrevious"))}
          </button>
        </div>
        <div class="oq-helper-actions">
          <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="apply" ${state.busyAction ? "disabled" : ""}>
            ${escapeHtml(state.busyAction === "apply" ? t("quickStart.confirmFinishing") : t("quickStart.confirmFinish"))}
          </button>
          <button class="oq-helper-button" type="button" data-oq-action="reset" ${state.busyAction ? "disabled" : ""}>
            ${escapeHtml(state.busyAction === "reset" ? t("quickStart.confirmResetting") : t("quickStart.confirmResetSetup"))}
          </button>
        </div>
      </section>
    `;
  }

  export function renderActiveStep() {
    reconcileStoredQuickStartSetupInstall();
    const activeStep = getCurrentQuickStep().id;
    if (activeStep === "setup") {
      return renderSetupWorkspace();
    }
    if (activeStep === "generation") {
      return renderGenerationWorkspace();
    }
    if (activeStep === "boiler") {
      return hasEntity("boilerCvAssistEnabled") ? renderBoilerWorkspace() : renderStrategyWorkspace();
    }
    if (activeStep === "flow-source") {
      return renderFlowSourceWorkspace();
    }
    if (activeStep === "thermostat-source") {
      return renderThermostatSourceWorkspace();
    }
    if (activeStep === "heating") {
      return renderHeatingWorkspace();
    }
    if (activeStep === "flow") {
      return renderFlowWorkspace();
    }
    if (activeStep === "water") {
      return renderWaterWorkspace();
    }
    if (activeStep === "silent") {
      return renderSilentWorkspace();
    }
    if (activeStep === "usage-telemetry") {
      return renderUsageTelemetryWorkspace();
    }
    if (activeStep === "performance-telemetry") {
      return renderPerformanceTelemetryWorkspace();
    }
    if (activeStep === "confirm") {
      return renderConfirmWorkspace();
    }
    return renderStrategyWorkspace();
  }

  export function getQuickSteps() {
    const isQEdition = getQuickStartHardwareProfileModel().isQEdition;
    return QUICK_STEPS.filter((step) => (step.id !== "setup" || isQEdition) && (!step.optionalEntity || hasEntity(step.optionalEntity)));
  }

  export function getQuickStepDefinition(stepId) {
    return QUICK_STEPS.find((step) => step.id === stepId) || null;
  }

  export function getQuickStepTitle(step) {
    return step?.titleKey ? t(step.titleKey) : "";
  }

  export function getQuickStepCopy(step) {
    return step?.copyKey ? t(step.copyKey) : "";
  }

  export function getQuickStepTitleById(stepId) {
    return getQuickStepTitle(getQuickStepDefinition(stepId));
  }

  export function getQuickStepCopyById(stepId) {
    return getQuickStepCopy(getQuickStepDefinition(stepId));
  }

  export function isQuickStartStepSelectionAllowed(stepId) {
    const steps = getQuickSteps();
    const setupIndex = steps.findIndex((step) => step.id === "setup");
    const targetIndex = steps.findIndex((step) => step.id === stepId);
    return targetIndex !== -1
      && (setupIndex === -1 || state.complete === true || state.quickStartSetupUpdateComplete || targetIndex <= setupIndex);
  }

  export function getQuickStepKicker(stepId) {
    const index = getQuickSteps().findIndex((step) => step.id === stepId);
    return t("quickStart.step", { n: Math.max(0, index) + 1 });
  }

  export function getQuickStepStatus(index) {
    const currentIndex = getCurrentQuickStepIndex();
    const isSelected = index === currentIndex;
    const isDone = state.complete === true || index < currentIndex;
    return {
      tone: isSelected ? "current" : isDone ? "done" : "upcoming",
      label: isSelected ? t("quickStart.statusActive") : isDone ? t("common.done") : t("quickStart.statusUpcoming"),
      current: isSelected,
    };
  }

  export function renderStepOverview(compact = false) {
    return getQuickSteps().map((step, index) => {
      const stepStatus = getQuickStepStatus(index);
      const selectionAllowed = isQuickStartStepSelectionAllowed(step.id);
      return `
        <button
          class="oq-helper-field oq-helper-field--step${compact ? " oq-helper-field--compact" : ""} is-${stepStatus.tone}"
          type="button"
          data-oq-action="select-step"
          data-step-id="${escapeHtml(step.id)}"
          aria-current="${stepStatus.current ? "step" : "false"}"
          ${selectionAllowed ? "" : "disabled"}
        >
          <div class="oq-helper-field-step-head">
            <h3>${String(index + 1).padStart(2, "0")}. ${escapeHtml(getQuickStepTitle(step))}</h3>
            <span class="oq-helper-field-step-state">${escapeHtml(stepStatus.label)}</span>
          </div>
          <p>${escapeHtml(getQuickStepCopy(step))}</p>
        </button>
      `;
    }).join("");
  }

  export function getCurrentQuickStep() {
    const steps = getQuickSteps();
    return steps.find((step) => step.id === state.currentStep) || steps[0] || QUICK_STEPS[0];
  }

  export function getCurrentQuickStepIndex() {
    return Math.max(0, getQuickSteps().findIndex((step) => step.id === state.currentStep));
  }

  export function selectQuickStepByOffset(offset) {
    const steps = getQuickSteps();
    const nextIndex = Math.min(steps.length - 1, Math.max(0, getCurrentQuickStepIndex() + offset));
    const nextStepId = steps[nextIndex]?.id || QUICK_STEPS[0].id;
    if (!isQuickStartStepSelectionAllowed(nextStepId)) {
      return false;
    }
    state.currentStep = nextStepId;
    return true;
  }

  export function renderQuickStartStepNav(options = {}) {
    const index = getCurrentQuickStepIndex();
    const steps = getQuickSteps();
    const previousStep = index > 0 ? steps[index - 1] : null;
    const nextStep = index < steps.length - 1 ? steps[index + 1] : null;

    return `
      <div class="oq-helper-step-nav">
        <div class="oq-helper-step-nav-meta">
          <strong>${escapeHtml(t("quickStart.navOf", { index: index + 1, total: steps.length }))}</strong>
          <span>${escapeHtml(nextStep ? t("quickStart.navNext", { title: getQuickStepTitle(nextStep) }) : t("quickStart.navLast"))}</span>
        </div>
        <div class="oq-helper-actions oq-helper-actions--step">
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="previous-step" ${previousStep ? "" : "disabled"}>
            ${escapeHtml(t("quickStart.navPrevious"))}
          </button>
          <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="next-step" ${nextStep && !options.nextDisabled ? "" : "disabled"}>
            ${escapeHtml(nextStep ? options.nextDisabled ? options.nextDisabledLabel || t("quickStart.navConfigureFirst") : t("common.next") : t("quickStart.navLastStep"))}
          </button>
        </div>
      </div>
    `;
  }

  export function renderQuickStartSidebar() {
    const stepIndex = getCurrentQuickStepIndex();
    const steps = getQuickSteps();
    return `
      <section class="oq-helper-panel oq-helper-panel--aside">
        <p class="oq-helper-label">Quick Start</p>
        <h2 class="oq-helper-section-title">${escapeHtml(t("quickStart.sidebarTitle"))}</h2>
        <p class="oq-helper-panel-note">${escapeHtml(t("quickStart.sidebarCopy"))}</p>
        <h3 class="oq-helper-aside-title">${escapeHtml(t("quickStart.navOf", { index: stepIndex + 1, total: steps.length }))}</h3>
        <div class="oq-helper-fields oq-helper-fields--compact">
          ${renderStepOverview(true)}
        </div>
        ${state.controlNotice ? `<p class="oq-helper-notice">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${state.controlError ? `<p class="oq-helper-error">${escapeHtml(state.controlError)}</p>` : ""}
      </section>
    `;
  }

  export function renderConfirmReviewCards() {
    const selectedGeneration = formatSettingsOptionLabel(getEntityStateText("hpGeneration"));
    const generationTitle = selectedGeneration ? t("quickStart.reviewSelected", { value: selectedGeneration }) : "";
    const generationDetection = getOduGenerationDetectionModel();
    const strategyTitle = optionLabel(isCurveMode() ? STRATEGY_OPTION_CURVE : STRATEGY_OPTION_POWER_HOUSE);
    const formatReviewOption = (key) => formatSettingsOptionLabel(getEntityStateText(key));
    const formatReviewTemp = (value) => Number.isFinite(value)
      ? `${formatNumber(value, { minimumFractionDigits: 1, maximumFractionDigits: 1 })} °C`
      : t("quickStart.reviewNoFlowValue");
    const generationLines = generationDetection.available
      ? [
          ...generationDetection.heatPumps.map((heatPump) => [
            t("quickStart.reviewDetectedHp", { index: heatPump.index }),
            heatPump.known ? heatPump.generation : t("common.unknown"),
          ]),
          [t("quickStart.reviewRecommended"), generationDetection.recommendation || t("quickStart.reviewNoAdvice")],
        ]
      : [];
    const strategyLines = isCurveMode()
      ? [
          [t("quickStart.reviewControlProfile"), formatReviewOption("curveControlProfile")],
          [t("quickStart.reviewSupplyAt", { label: "-20°C" }), formatValue("curveM20")],
          [t("quickStart.reviewSupplyAt", { label: "-10°C" }), formatValue("curveM10")],
          [t("quickStart.reviewSupplyAt", { label: "0°C" }), formatValue("curve0")],
          [t("quickStart.reviewSupplyAt", { label: "5°C" }), formatValue("curve5")],
          [t("quickStart.reviewSupplyAt", { label: "10°C" }), formatValue("curve10")],
          [t("quickStart.reviewSupplyAt", { label: "15°C" }), formatValue("curve15")],
          [t("quickStart.reviewFallbackSupply"), formatValue("curveFallbackSupply")],
        ]
      : [
          [t("quickStart.reviewProfile"), formatReviewOption("phResponseProfile")],
          ["Rated maximum house power", formatValue("housePower")],
          ["Maximum heating outdoor temperature", formatValue("houseOutdoorMax")],
          [t("quickStart.reviewTempReaction"), formatValue("phKp")],
          [t("quickStart.reviewComfortBelow"), formatValue("phComfortBelow")],
          [t("quickStart.reviewComfortAbove"), formatValue("phComfortAbove")],
        ];

    const flowMode = String(getEntityValue("flowControlMode") || "");
    const flowSourceModel = getQuickStartFlowSourceModel();
    const flowSourceLines = [
      [t("quickStart.reviewStatus"), flowSourceModel.status],
      [t("quickStart.reviewCurrentFlow"), flowSourceModel.flowAvailable ? `${formatNumber(Math.round(flowSourceModel.flowValue), { maximumFractionDigits: 0 })} L/h` : t("quickStart.reviewNoFlowValue")],
    ];
    const thermostatSourceModel = getQuickStartThermostatSourceModel();
    const thermostatSourceLines = [
      [t("quickStart.reviewStatus"), thermostatSourceModel.status],
      [t("quickStart.reviewRoomTemp"), formatReviewTemp(thermostatSourceModel.roomTempValue)],
      [t("quickStart.reviewRoomSetpoint"), formatReviewTemp(thermostatSourceModel.roomSetpointValue)],
    ];
    const flowLines = [
      [t("quickStart.reviewFlowControl"), flowMode === "Manual PWM" ? t("quickStart.reviewFixedPump") : t("quickStart.reviewDesiredFlow")],
      flowMode === "Manual PWM"
        ? [t("quickStart.reviewFixedPump"), formatValue("manualIpwm")]
        : [t("quickStart.reviewDesiredFlow"), formatValue("flowSetpoint")],
    ];

    const sourcePresent = hasEntity("auxHeatSourcePresent")
      ? isEntityActive("auxHeatSourcePresent")
      : isEntityActive("boilerCvAssistEnabled");
    const boilerLines = hasEntity("auxHeatSourcePresent") || hasEntity("boilerCvAssistEnabled")
      ? [
          [t("quickStart.reviewHeatSourceConnected"), sourcePresent ? t("common.yes") : t("common.no")],
          ...(sourcePresent
            ? [
                ...(hasEntity("boilerConnection")
                  ? [[t("quickStart.reviewSourceControl"), String(getEntityValue("boilerConnection") || "R1") === "OpenTherm" ? t("quickStart.reviewOtbControl") : t("quickStart.reviewRelayControl")]]
                  : []),
                [t("quickStart.reviewAvailablePower"), formatValue("boilerRatedHeatPower")],
                ...(hasEntity("boilerCvAssistEnabled")
                  ? [[t("quickStart.reviewHybridAssist"), isEntityActive("boilerCvAssistEnabled") ? t("common.on") : t("common.off")]]
                  : []),
                ...(hasEntity("boilerFaultFallbackEnabled")
                  ? [[t("quickStart.reviewFaultFallback"), isEntityActive("boilerFaultFallbackEnabled") ? t("common.on") : t("common.off")]]
                  : []),
              ]
            : []),
        ]
      : [];

    const waterLines = [
      [t("quickStart.reviewMaxWaterTemp"), formatValue("maxWater")],
    ];

    const silentLines = [
      [t("quickStart.reviewSilentStart"), toTimeInputValue(getEntityValue("silentStartTime")) || t("common.notAvailable")],
      [t("quickStart.reviewSilentEnd"), toTimeInputValue(getEntityValue("silentEndTime")) || t("common.notAvailable")],
      [t("quickStart.reviewSilentMax"), formatValue("silentMaxHz")],
      [t("quickStart.reviewDayMax"), formatValue("dayMaxHz")],
    ];

    const usageTelemetryLines = hasEntity("usageTelemetryEnabled")
      ? [[t("quickStart.reviewUsageStats"), isEntityActive("usageTelemetryEnabled") ? t("quickStart.reviewSharing") : t("quickStart.reviewNotSharing")]]
      : [];

    const performanceTelemetryLines = hasEntity("performanceTelemetryEnabled")
      ? [[t("quickStart.reviewPerfSharing"), isEntityActive("performanceTelemetryEnabled") ? t("common.on") : t("common.off")]]
      : [];

    const renderReviewList = (lines) => `
      <div class="oq-helper-review-list">
        ${lines
          .filter((line) => line && line[1])
          .map(
            ([label, value]) => `
              <div class="oq-helper-review-row">
                <span class="oq-helper-review-label">${escapeHtml(label)}</span>
                <strong class="oq-helper-review-value">${escapeHtml(value)}</strong>
              </div>
            `,
          )
          .join("")}
      </div>
    `;
    const renderReviewCard = (title, lines, summary = "") => `
      <article class="oq-helper-field oq-helper-field--review">
        <h3>${escapeHtml(title)}</h3>
        ${summary ? `<p class="oq-helper-review-summary"><strong>${escapeHtml(summary)}</strong></p>` : ""}
        ${renderReviewList(lines)}
      </article>
    `;

    return `
      <div class="oq-helper-fields oq-helper-fields--review">
        ${renderReviewCard(t("quickStart.reviewCardGeneration"), generationLines, generationTitle)}
        ${renderReviewCard(t("quickStart.reviewCardFlowMeasurement"), flowSourceLines, flowSourceModel.sourceLabel)}
        ${renderReviewCard(t("quickStart.reviewCardStrategy"), strategyLines, strategyTitle)}
        ${renderReviewCard(t("quickStart.reviewCardWaterTemp"), waterLines)}
        ${renderReviewCard(t("quickStart.reviewCardThermostat"), thermostatSourceLines, thermostatSourceModel.sourceLabel)}
        ${renderReviewCard(t("quickStart.reviewCardFlowControl"), flowLines)}
        ${boilerLines.length ? renderReviewCard(t("quickStart.reviewCardBoiler"), boilerLines) : ""}
        ${renderReviewCard(t("quickStart.reviewCardSilent"), silentLines)}
        ${usageTelemetryLines.length ? renderReviewCard(t("quickStart.reviewCardUsage"), usageTelemetryLines) : ""}
        ${performanceTelemetryLines.length ? renderReviewCard(t("quickStart.reviewCardPerformance"), performanceTelemetryLines) : ""}
      </div>
    `;
  }
