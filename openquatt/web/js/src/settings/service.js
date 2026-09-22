import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { getEntityValue } from "../core/entity-store.js";
import { formatFailures } from "../core/failure-format.js";
import { state } from "../core/state.js";
import { formatSettingsNumberValue, getCommissioningStatusValue, getSettingsStatValue, getSettingsTemperatureValue, getSettingsTextStatValue, getStatusTextValue, renderNamedActionButton, renderNamedToggleActionButton, renderSettingsCheckboxSwitchField, renderSettingsSection, renderSettingsSelectField, renderSettingsSliderField, renderSettingsStaticField, renderSettingsSystemRow } from "./controls.js";
import { getHpWaterRawValue } from "./water.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { formatNumber, t } from "../i18n/index.js";

  export function getManualHpActualValue(levelKey, frequencyKey) {
    const level = getEntityNumericValue(levelKey);
    const frequency = getEntityNumericValue(frequencyKey);
    const levelText = Number.isNaN(level) ? "F—" : `F${Math.round(level)}`;
    const frequencyText = Number.isNaN(frequency) ? "— Hz" : `${Math.round(frequency)} Hz`;
    return `${levelText} (${frequencyText})`;
  }

  export function getManualHpMaximumLevel(profileKey, modeKey) {
    const configuredV2 = getEntityStateText("hpGeneration").trim() === "V2";
    if (!configuredV2) return 10;

    const profile = getEntityStateText(profileKey).trim();
    if (profile === "V2 F0-F20") return 20;

    const mode = getEntityStateText(modeKey).trim();
    if (mode === "Heating" && profile === "V2 heating F0-F20") return 20;
    if (mode === "Cooling" && profile === "V2 cooling F0-F20") return 20;
    return 10;
  }

  export function isCommissioningTaskStatusBusy(status) {
    const normalized = String(status || "").trim().toUpperCase();
    if (!normalized || normalized === "0" || normalized === "IDLE" || normalized === "CM100 READY" || normalized === "CM100 STOPPED") {
      return false;
    }
    if (normalized.includes("DONE") || normalized.includes("FAILED") || normalized.includes("ABORT") || normalized.includes("APPLIED") || normalized.includes("REFUSED")) {
      return false;
    }
    return normalized.includes("REQUESTED")
      || normalized.includes("WAITING")
      || normalized.includes("WACHTEN")
      || normalized.includes("SETTLING")
      || normalized.includes("MEASUR")
      || normalized.includes("COOLDOWN")
      || normalized.includes("RUNNING")
      || normalized.includes("VALIDATING")
      || normalized.includes("STARTED")
      || normalized.includes("RECOVER")
      || normalized.includes("PHASE")
      || normalized.includes("STEADY")
      || normalized.includes("PULSE")
      || normalized.includes("STABILIZE")
      || normalized.includes("STEP");
  }

  export function isCommissioningTaskStatusTerminal(status) {
    const normalized = String(status || "").trim().toUpperCase();
    if (!normalized) {
      return false;
    }
    return normalized.includes("DONE")
      || normalized.includes("FAILED")
      || normalized.includes("ABORT")
      || normalized.includes("APPLIED")
      || normalized.includes("CONFIRM_REQUIRED")
      || normalized.includes("REFUSED");
  }

  export function isBoilerTestResultReady(status) {
    return /DONE|APPLIED|CONFIRM_REQUIRED/.test(String(status || "").trim().toUpperCase());
  }

  export function isCommissioningTaskStatusWaitingForCm100(status) {
    const normalized = String(status || "").trim().toUpperCase();
    return normalized.includes("WAITING_FOR_CM100")
      || normalized.includes("CM100 REQUESTED")
      || normalized.includes("WACHTEN OP CM100")
      || normalized === "WACHTEN";
  }

  export function isCommissioningTaskStatusActive(status) {
    return isCommissioningTaskStatusBusy(status) && !isCommissioningTaskStatusWaitingForCm100(status);
  }

  export function getCommissioningProgressModel(statusText = "", task = "") {
    const value = String(statusText || "").trim().toUpperCase();
    const taskType = String(task || "").trim().toLowerCase();
    const tokens = value.split(/[^A-Z0-9]+/).filter(Boolean);
    const matchesStatus = (needle) => {
      const normalizedNeedle = String(needle || "").trim().toUpperCase();
      if (!normalizedNeedle) {
        return false;
      }
      return value === normalizedNeedle
        || value.startsWith(`${normalizedNeedle}:`)
        || value.startsWith(`${normalizedNeedle} `)
        || tokens.includes(normalizedNeedle);
    };

    const progressMaps = {
      boiler: [
        { match: ["REQUESTED", "WAITING_FOR_CM100", "REFUSED"], phase: t("settingsService.phasePrep"), percent: 12 },
        { match: ["FLOW_SETTLING"], phase: t("settingsService.phaseFlowSettle"), percent: 28 },
        { match: ["BOILER_SETTLING"], phase: t("settingsService.phaseBoilerSettle"), percent: 48 },
        { match: ["MEASURING"], phase: t("settingsService.phaseMeasuring"), percent: 72 },
        { match: ["COOLDOWN"], phase: t("settingsService.phaseCooldown"), percent: 90 },
        { match: ["DONE", "APPLIED"], phase: t("settingsService.phaseDone"), percent: 100 },
        { match: ["ABORTED", "FAILED", "ABORT"], phase: t("settingsService.phaseAborted"), percent: 100 },
      ],
      autotune: [
        { match: ["REQUESTED", "WAITING_FOR_CM100", "REFUSED"], phase: t("settingsService.phasePrep"), percent: 10 },
        { match: ["WAITING_FOR_FLOW", "SETTLING"], phase: t("settingsService.phaseFlowSettle"), percent: 26 },
        { match: ["STEP2"], phase: t("settingsService.phaseStep2"), percent: 56 },
        { match: ["STEP", "STEP1"], phase: t("settingsService.phaseStep1"), percent: 42 },
        { match: ["VALIDATING_SETTLING"], phase: t("settingsService.phaseValidating"), percent: 70 },
        { match: ["VALIDATING"], phase: t("settingsService.phaseValidating"), percent: 84 },
        { match: ["RECOVERING"], phase: t("settingsService.phaseRecovering"), percent: 92 },
        { match: ["DONE", "APPLIED"], phase: t("settingsService.phaseDone"), percent: 100 },
        { match: ["ABORTED", "FAILED", "ABORT"], phase: t("settingsService.phaseAborted"), percent: 100 },
      ],
      purge: [
        { match: ["REQUESTED", "STARTED", "REFUSED"], phase: t("settingsService.phasePrep"), percent: 8 },
        { match: ["PHASE1", "STEADY"], phase: t("settingsService.phaseQuiet"), percent: 22 },
        { match: ["PHASE2", "PULSE"], phase: t("settingsService.phasePulse"), percent: 62 },
        { match: ["PHASE3", "STABILIZE"], phase: t("settingsService.phaseStab"), percent: 90 },
        { match: ["DONE"], phase: t("settingsService.phaseDone"), percent: 100 },
        { match: ["ABORTED", "FAILED", "ABORT"], phase: t("settingsService.phaseAborted"), percent: 100 },
      ],
      "hp-water-calibration": [
        { match: ["REQUESTED", "STARTED", "REFUSED"], phase: t("settingsService.phasePrep"), percent: 8 },
        { match: ["MIXING"], phase: t("settingsService.phaseMixing"), percent: 42 },
        { match: ["MEASURING"], phase: t("settingsService.phaseSensors"), percent: 78 },
        { match: ["DONE", "APPLIED"], phase: t("settingsService.phaseDone"), percent: 100 },
        { match: ["ABORTED", "FAILED", "ABORT"], phase: t("settingsService.phaseAborted"), percent: 100 },
      ],
      cm100: [
        { match: ["REQUESTED"], phase: t("settingsService.phaseWaitCm100"), percent: 0 },
        { match: ["WAITING_FOR_CM100"], phase: t("settingsService.phaseWaitCm100"), percent: 0 },
        { match: ["CM100 READY"], phase: t("settingsService.phaseDone"), percent: 100 },
        { match: ["IDLE"], phase: t("settingsService.phaseDone"), percent: 100 },
      ],
    };

    if (!value || value === "—" || value === "UNKNOWN" || value === "UNAVAILABLE" || value === "NAN") {
      return { phase: t("settingsService.phaseWaiting"), percent: 0 };
    }

    if (value.includes("WAITING") || value.includes("WACHTEN")) {
      return { phase: t("settingsService.phaseWaiting"), percent: 0 };
    }

    if (taskType !== "cm100" && (
      value === "IDLE"
      || value === "CM0 - STANDBY"
      || value === "CM100 READY"
      || value === "CM100 STOPPED"
      || value === "GEPAUZEERD"
    )) {
      return { phase: t("settingsService.phaseWaiting"), percent: 0 };
    }

    const selected = progressMaps[taskType] || [];
    const match = selected.find((item) => item.match.some((needle) => matchesStatus(needle)));
    if (match) {
      return match;
    }

    if (value.includes("DONE") || value.includes("APPLIED")) {
      return { phase: t("settingsService.phaseDone"), percent: 100 };
    }
    if (value.includes("ABORT") || value.includes("FAILED") || value.includes("REFUSED")) {
      return { phase: t("settingsService.phaseAborted"), percent: 100 };
    }
    if (taskType === "cm100" && value.includes("CM100")) {
      return { phase: t("settingsService.phaseDone"), percent: 100 };
    }
    return { phase: statusText, percent: 0 };
  }

  export function renderCommissioningTaskCard({
    taskKey,
    title,
    copy,
    subcopy = "",
    status,
    statusCopy,
    progressTask,
    actions = "",
    controls = "",
    metrics = "",
    className = "",
  }) {
    return `
      <article class="oq-settings-commissioning-card${className ? ` ${escapeHtml(className)}` : ""}" data-oq-commissioning-task="${escapeHtml(taskKey)}">
        <div class="oq-settings-commissioning-card-head">
          <div class="oq-settings-commissioning-card-copy">
            <h3>${escapeHtml(title)}</h3>
            <p>${escapeHtml(copy)}</p>
            ${subcopy ? `<p class="oq-settings-commissioning-card-subcopy">${escapeHtml(subcopy)}</p>` : ""}
          </div>
        </div>
        ${actions ? `<div class="oq-settings-commissioning-card-actions">${actions}</div>` : ""}
        ${controls}
        <div class="oq-settings-quickstart-status oq-settings-quickstart-status--compact oq-settings-commissioning-card-status">
          <div class="oq-settings-quickstart-status-row">
            <div>
              <p class="oq-settings-quickstart-status-label">${escapeHtml(t("settingsService.currentStatus"))}</p>
              <strong class="oq-settings-quickstart-status-value">${escapeHtml(status)}</strong>
              <p class="oq-settings-quickstart-status-copy">${escapeHtml(statusCopy)}</p>
            </div>
          </div>
        </div>
        ${metrics ? `<div class="oq-settings-grid oq-settings-commissioning-metrics">${metrics}</div>` : ""}
      </article>
    `;
  }

  export function renderHpWaterCalibrationWizard({
    status,
    running,
    resultReady,
    startDisabled,
    abortDisabled,
    applyDisabled,
    busy,
    controlsAvailable,
  }) {
    const normalizedStatus = String(status || "").toUpperCase();
    const failed = normalizedStatus.includes("FAILED") || normalizedStatus.includes("REFUSED") || normalizedStatus.includes("ABORT");
    const applied = normalizedStatus.includes("APPLIED");
    const hasHp2 = hasEntity("hp2WaterIn") || hasEntity("hp2WaterOut") || hasEntity("hp2WaterInRaw") || hasEntity("hp2WaterOutRaw");
    const stableProgress = getEntityNumericValue("hpWaterCalibrationStableProgress");
    const stableRequired = getEntityNumericValue("hpWaterCalibrationStableRequired");
    const remaining = getEntityNumericValue("hpWaterCalibrationRemaining");
    const phaseCode = Math.round(getEntityNumericValue("hpWaterCalibrationPhase"));
    const mixing = running && (phaseCode === 1 || normalizedStatus.includes("MIXING"));
    const measuring = running && !mixing;
    const maxDurationS = 300;
    const minMixingS = 180;
    const elapsed = Number.isFinite(remaining) ? Math.max(0, maxDurationS - remaining) : NaN;
    const mixingRemaining = Number.isFinite(elapsed) ? Math.max(0, minMixingS - elapsed) : NaN;
    const progressValue = mixing && Number.isFinite(elapsed)
      ? Math.max(0, Math.min(100, (elapsed / minMixingS) * 100))
      : measuring && Number.isFinite(stableProgress) && Number.isFinite(stableRequired) && stableRequired > 0
        ? Math.max(0, Math.min(100, (stableProgress / stableRequired) * 100))
        : running && Number.isFinite(remaining)
          ? Math.max(0, Math.min(100, 100 - ((remaining / maxDurationS) * 100)))
          : resultReady
            ? 100
            : 0;
    const spreadValue = resultReady && hasEntity("hpWaterCalibrationResultSpreadBefore")
      ? getSettingsTemperatureValue("hpWaterCalibrationResultSpreadBefore", 2)
      : getSettingsTemperatureValue("hpWaterCalibrationSpread", 2);
    const stableCopy = mixing
      ? t("settingsService.stableMixing")
      : Number.isFinite(stableProgress) && Number.isFinite(stableRequired) && stableRequired > 0
        ? (stableProgress > 0
        ? t("settingsService.stableProgress", { done: formatNumber(Math.max(0, stableProgress), { maximumFractionDigits: 0 }), required: formatNumber(stableRequired, { maximumFractionDigits: 0 }) })
        : t("settingsService.stableNone"))
        : t("settingsService.stableWait");
    const stepIndex = resultReady ? 3 : running ? 2 : 1;
    const statusTitle = applied
      ? t("settingsService.appliedTitle")
      : resultReady
      ? t("settingsService.readyTitle", { spread: spreadValue })
      : running
        ? (mixing
          ? `${t("settingsService.mixingTitle")}${Number.isFinite(mixingRemaining) && mixingRemaining > 0 ? t("settingsService.mixingRemaining", { s: formatNumber(Math.round(mixingRemaining), { maximumFractionDigits: 0 }) }) : ""}`
          : t("settingsService.measuringTitle", { detail: Number.isFinite(remaining) && remaining > 0 ? t("settingsService.measuringRemaining", { s: formatNumber(Math.round(remaining), { maximumFractionDigits: 0 }) }) : stableCopy }))
        : failed
          ? t("settingsService.failedTitle")
          : t("settingsService.prepTitle");
    const statusCopy = applied
      ? t("settingsService.appliedCopy")
      : resultReady
      ? t("settingsService.readyCopy")
      : running
        ? (mixing
          ? t("settingsService.mixingCopy")
          : t("settingsService.measuringCopy"))
        : failed
          ? getSettingsTextStatValue("hpWaterCalibrationStatus", t("settingsService.failedCopyDefault"))
          : (hasHp2
            ? t("settingsService.idleCopyDual")
            : t("settingsService.idleCopySingle"));
    const supplySource = getSettingsTextStatValue(
      "hpWaterCalibrationResultSupplySource",
      getSettingsTextStatValue("waterSupplyTempEffectiveSource", t("settingsWater.sourceActive")),
    );
    const sensorRows = [
      { label: t("settingsWater.hp1WaterIn"), rawKey: "hp1WaterInRaw", liveKey: "hp1WaterIn", resultRawKey: "hpWaterCalibrationResultHp1InRawAvg", offsetKey: "hp1WaterInOffset", suggestedKey: "hp1WaterInOffsetSuggested" },
      { label: t("settingsWater.hp1WaterOut"), rawKey: "hp1WaterOutRaw", liveKey: "hp1WaterOut", resultRawKey: "hpWaterCalibrationResultHp1OutRawAvg", offsetKey: "hp1WaterOutOffset", suggestedKey: "hp1WaterOutOffsetSuggested" },
      { label: t("settingsWater.hp2WaterIn"), rawKey: "hp2WaterInRaw", liveKey: "hp2WaterIn", resultRawKey: "hpWaterCalibrationResultHp2InRawAvg", offsetKey: "hp2WaterInOffset", suggestedKey: "hp2WaterInOffsetSuggested" },
      { label: t("settingsWater.hp2WaterOut"), rawKey: "hp2WaterOutRaw", liveKey: "hp2WaterOut", resultRawKey: "hpWaterCalibrationResultHp2OutRawAvg", offsetKey: "hp2WaterOutOffset", suggestedKey: "hp2WaterOutOffsetSuggested" },
      { label: t("settingsWater.supplyTitle", { source: supplySource }), rawKey: "supplyTemp", liveKey: "supplyTemp", resultRawKey: "hpWaterCalibrationResultSupplyRawAvg", offsetKey: "waterSupplyCalibrationOffset", suggestedKey: "waterSupplyCalibrationOffsetSuggested" },
    ].filter((row) => hasEntity(row.liveKey) || hasEntity(row.rawKey) || hasEntity(row.offsetKey));

    const renderStep = (index, label) => {
      const done = stepIndex > index;
      const active = stepIndex === index;
      return `
        <div class="oq-settings-hp-calibration-step${done ? " is-done" : ""}${active ? " is-active" : ""}">
          <span>${done ? "✓" : index}</span>
          <strong>${escapeHtml(label)}</strong>
        </div>
      `;
    };

    const renderLiveCard = (row) => {
      return `
        <article class="oq-settings-hp-calibration-live-card">
          <span>${escapeHtml(row.label)}</span>
          <strong>${escapeHtml(getSettingsTemperatureValue(row.liveKey, 2))}</strong>
        </article>
      `;
    };

    const renderResultRow = (row) => {
      const rawAverage = getEntityNumericValue(row.resultRawKey);
      const rawValue = Number.isFinite(rawAverage)
        ? rawAverage
        : getHpWaterRawValue(row.rawKey, row.liveKey, row.offsetKey);
      const suggestion = getEntityNumericValue(row.suggestedKey);
      const finalValue = Number.isFinite(rawValue) && Number.isFinite(suggestion)
        ? formatSettingsNumberValue(rawValue + suggestion, state.entities[row.suggestedKey]?.uom || "°C", 2)
        : "—";

      return `
        <tr>
          <th scope="row">${escapeHtml(row.label)}</th>
          <td>${escapeHtml(Number.isFinite(rawValue) ? formatSettingsNumberValue(rawValue, state.entities[row.liveKey]?.uom || "°C", 2) : "—")}</td>
          <td>${escapeHtml(getSettingsTemperatureValue(row.offsetKey, 2))}</td>
          <td><span class="oq-settings-hp-calibration-offset-pill">${escapeHtml(getSettingsTemperatureValue(row.suggestedKey, 2))}</span></td>
          <td>${escapeHtml(finalValue)}</td>
        </tr>
      `;
    };

    return `
      <div class="oq-settings-hp-calibration">
        <div class="oq-settings-hp-calibration-steps">
          ${renderStep(1, t("settingsService.step1"))}
          ${renderStep(2, t("settingsService.step2"))}
          ${renderStep(3, t("settingsService.step3"))}
        </div>

        <div class="oq-settings-hp-calibration-status${resultReady ? " is-success" : running ? " is-active" : failed ? " is-warning" : ""}">
          <div>
            <strong>${escapeHtml(statusTitle)}</strong>
            <p>${escapeHtml(statusCopy)}</p>
          </div>
          ${running || resultReady ? `<span>${escapeHtml(running ? stableCopy : t("settingsService.resultAvailable"))}</span>` : ""}
          ${running ? `<div class="oq-settings-hp-calibration-progress"><i style="width: ${progressValue.toFixed(0)}%"></i></div>` : ""}
        </div>

        ${running ? `
          <div class="oq-settings-hp-calibration-live-grid">
            ${sensorRows.map(renderLiveCard).join("")}
            <article class="oq-settings-hp-calibration-live-card is-highlight">
              <span>${escapeHtml(t("settingsService.spreadLabel"))}</span>
              <strong>${escapeHtml(getSettingsTemperatureValue("hpWaterCalibrationSpread", 2))}</strong>
            </article>
          </div>
          <p class="oq-settings-hp-calibration-note">${escapeHtml(t("settingsService.liveNote"))}</p>
        ` : ""}

        ${resultReady ? `
          <div class="oq-settings-hp-calibration-results">
            <div class="oq-settings-hp-calibration-result-summary">
              <span>${escapeHtml(t("settingsService.refLabel", { value: getSettingsTemperatureValue("hpWaterCalibrationResultReference", 2) }))}</span>
              <span>${escapeHtml(t("settingsService.supplySourceLabel", { source: supplySource }))}</span>
            </div>
            <div class="oq-settings-hp-calibration-table-wrap">
              <table class="oq-settings-hp-calibration-table">
                <thead>
                  <tr>
                    <th scope="col">${escapeHtml(t("settingsService.colSensor"))}</th>
                    <th scope="col">${escapeHtml(t("settingsService.colRawAvg"))}</th>
                    <th scope="col">${escapeHtml(t("settingsService.colCurrent"))}</th>
                    <th scope="col">${escapeHtml(t("settingsService.colProposal"))}</th>
                    <th scope="col">${escapeHtml(t("settingsService.colAfter"))}</th>
                  </tr>
                </thead>
                <tbody>
                  ${sensorRows.map(renderResultRow).join("")}
                </tbody>
              </table>
            </div>
          </div>
        ` : ""}

        ${controlsAvailable ? `
          <div class="oq-settings-hp-calibration-actions" data-oq-hp-water-calibration-actions>
            ${renderNamedToggleActionButton({
              active: running,
              startKey: "hpWaterCalibrationStart",
              stopKey: "hpWaterCalibrationAbort",
              startLabel: t("settingsService.startCal"),
              stopLabel: t("settingsService.stopMeasure"),
              startDisabled: busy || startDisabled,
              stopDisabled: busy || abortDisabled,
            })}
            ${state.entities.hpWaterCalibrationApply ? renderNamedActionButton("hpWaterCalibrationApply", t("settingsService.applyOffsets"), "oq-helper-button oq-helper-button--primary", busy || applyDisabled) : ""}
          </div>
        ` : ""}
      </div>
    `;
  }

  export function getBoilerTestStatusCopy(boilerStatus, flowLph, targetLph = 800) {
    const status = String(boilerStatus || "").trim();
    const upper = status.toUpperCase();
    const flow = Number(flowLph);
    const target = Number(targetLph);
    const flowText = Number.isFinite(flow) ? t("settingsService.flowValue", { value: formatNumber(Math.round(flow), { maximumFractionDigits: 0 }) }) : t("settingsService.flowUnknown");
    const targetText = Number.isFinite(target) ? t("settingsService.flowValue", { value: formatNumber(Math.round(target), { maximumFractionDigits: 0 }) }) : t("settingsService.flowDefault");

    if (upper.includes("FLOW_SETTLING")) {
      return t("settingsService.boilerFlowSettling", { target: targetText, flow: flowText });
    }
    if (upper.includes("BOILER_SETTLING")) {
      return t("settingsService.boilerBoilerSettling", { flow: flowText });
    }
    if (upper.includes("MEASURING")) {
      const heat = getSettingsStatValue("boilerHeatPower");
      return t("settingsService.boilerMeasuringBase", { heat: heat && heat !== "—" ? t("settingsService.boilerMeasuringHeat", { heat }) : "" });
    }
    if (upper.includes("COOLDOWN")) {
      const result = getSettingsStatValue("boilerPowerTestResult");
      return t("settingsService.boilerCooldownBase", { result: result && result !== "—" ? t("settingsService.boilerCooldownResult", { result }) : "" });
    }
    if (upper.startsWith("CONFIRM_REQUIRED")) {
      return t("settingsService.boilerConfirmRequired");
    }
    if (upper.startsWith("DONE:") || upper === "DONE" || upper.includes("APPLIED")) {
      const result = getSettingsStatValue("boilerPowerTestResult");
      const conf = getSettingsStatValue("boilerPowerTestConfidence");
      const isFlowLimited = upper.includes("FLOW LIMITED");
      if (result && result !== "—") {
        if (isFlowLimited) {
          return t("settingsService.boilerDoneFlowLimited", { result, conf: conf && conf !== "—" ? t("settingsService.boilerConfSuffix", { conf }) : "" });
        }
        return t("settingsService.boilerDoneAuto", { result, conf: conf && conf !== "—" ? t("settingsService.boilerConfSuffix", { conf }) : "" });
      }
      return upper.includes("APPLIED") ? t("settingsService.boilerAppliedResult") : t("settingsService.boilerDoneOff");
    }
    if (upper === "ABORTED" || upper === "ABORT") {
      return t("settingsService.boilerAbortedManual");
    }
    if (upper.startsWith("ABORTED:") || upper.startsWith("ABORT:")) {
      const reason = status.slice(status.indexOf(":") + 1).trim();
      return t("settingsService.boilerAbortedReason", { reason });
    }
    if (upper.startsWith("REFUSED:")) {
      const reason = status.slice(status.indexOf(":") + 1).trim();
      return t("settingsService.boilerRefusedReason", { reason });
    }
    if (upper.includes("FAILED: BOILER POWER DID NOT STABILISE")) {
      return t("settingsService.boilerFailedStabilise");
    }
    if (upper.includes("FAILED")) {
      const colonIdx = status.indexOf(":");
      if (colonIdx > 0) {
        const reason = status.slice(colonIdx + 1).trim();
        return t("settingsService.boilerFailedReason", { reason });
      }
      return t("settingsService.boilerFailedRaw", { status });
    }
    if (upper === "REFUSED") {
      return t("settingsService.boilerRefusedRaw", { status });
    }
    return status;
  }

  export function getSettingsServiceModel() {
    const hasBoilerAssist = hasEntity("auxHeatSourcePresent")
      ? isEntityActive("auxHeatSourcePresent")
      : hasEntity("boilerCvAssistEnabled") && isEntityActive("boilerCvAssistEnabled");
    const cm100Status = getCommissioningStatusValue();
    const cm100Active = isEntityActive("cm100Active");
    const cm100StatusUpper = String(cm100Status || "").trim().toUpperCase();
    const cm100WaitingForCm100 = isCommissioningTaskStatusWaitingForCm100(cm100Status);
    const cm100Ready = !cm100WaitingForCm100 && (cm100Active || cm100StatusUpper === "CM100 READY");
    const cm100TaskLocked = state.commissioningTaskLock === "cm100";
    const cm100Busy = state.loadingEntities || state.busyAction === "commissioningCm100Start" || state.busyAction === "commissioningCm100Stop" || cm100TaskLocked;
    const cm100Pending = Boolean(state.pendingCommissioningCm100Start);
    const hp1ManualMaxLevel = getManualHpMaximumLevel("hp1CompressorLevelProfile", "manualHp1Mode");
    const hp2ManualMaxLevel = getManualHpMaximumLevel("hp2CompressorLevelProfile", "manualHp2Mode");
    const cm100StartDisabled = cm100Busy || cm100Ready || cm100WaitingForCm100;
    const cm100StopDisabled = cm100Busy || !cm100Ready;
    const boilerStatus = getStatusTextValue("boilerPowerTestStatus", "IDLE");
    const boilerProgress = getCommissioningProgressModel(boilerStatus, "boiler");
    const boilerActive = isEntityActive("boilerPowerTestActive");
    const boilerBusy = state.loadingEntities || state.busyAction === "boilerPowerTestStart" || state.busyAction === "boilerPowerTestAbort" || state.busyAction === "boilerPowerTestApply";
    const boilerControls = Boolean(state.entities.boilerPowerTestStart || state.entities.boilerPowerTestAbort || state.entities.boilerPowerTestApply);
    const boilerPending = Boolean(state.pendingBoilerPowerTestStart);
    const boilerTaskLocked = state.commissioningTaskLock === "boiler";
    const boilerTaskWaitingForCm100 = isCommissioningTaskStatusWaitingForCm100(boilerStatus);
    const boilerTaskTerminal = isCommissioningTaskStatusTerminal(boilerStatus);
    const boilerTaskRunning = !boilerTaskTerminal &&
      (boilerActive || boilerPending || boilerTaskLocked || isCommissioningTaskStatusActive(boilerStatus)) &&
      !boilerTaskWaitingForCm100;
    const boilerRatedPower = getSettingsStatValue("boilerRatedHeatPower");
    const boilerHeatPowerRaw = getSettingsStatValue("boilerHeatPower");
    const boilerHeatPowerNumeric = getEntityNumericValue("boilerHeatPower");
    const boilerHeatPower = boilerHeatPowerNumeric > 0
      ? boilerHeatPowerRaw
      : (boilerTaskRunning && state.commissioningBoilerHeatPowerDisplay ? state.commissioningBoilerHeatPowerDisplay : boilerHeatPowerRaw);
    if (boilerHeatPowerNumeric > 0) {
      state.commissioningBoilerHeatPowerDisplay = boilerHeatPowerRaw;
    }
    const autotuneStatus = getStatusTextValue("flowAutotuneStatus", "IDLE");
    const autotuneProgress = getCommissioningProgressModel(autotuneStatus, "autotune");
    const autotuneBusy = state.loadingEntities || state.busyAction === "flowAutotuneStart" || state.busyAction === "flowAutotuneAbort" || state.busyAction === "flowAutotuneApply";
    const autotuneControls = Boolean(state.entities.flowAutotuneStart || state.entities.flowAutotuneAbort || state.entities.flowAutotuneApply);
    const autotunePending = Boolean(state.pendingFlowAutotuneStart);
    const autotuneTaskLocked = state.commissioningTaskLock === "autotune";
    const autotuneTaskWaitingForCm100 = isCommissioningTaskStatusWaitingForCm100(autotuneStatus);
    const autotuneTaskTerminal = isCommissioningTaskStatusTerminal(autotuneStatus);
    const autotuneTaskRunning = !autotuneTaskTerminal &&
      (autotunePending || autotuneTaskLocked || isCommissioningTaskStatusActive(autotuneStatus)) &&
      !autotuneTaskWaitingForCm100;
    const airPurgeStatus = getStatusTextValue("airPurgeStatus", "IDLE");
    const airPurgeProgress = getCommissioningProgressModel(airPurgeStatus, "purge");
    const airPurgeActive = isEntityActive("airPurgeActive");
    const airPurgeBusy = state.loadingEntities || state.busyAction === "airPurgeStart" || state.busyAction === "airPurgeAbort";
    const airPurgeControls = Boolean(state.entities.airPurgeStart || state.entities.airPurgeAbort);
    const airPurgePending = Boolean(state.pendingAirPurgeStart);
    const airPurgeTaskLocked = state.commissioningTaskLock === "purge";
    const airPurgeTaskTerminal = isCommissioningTaskStatusTerminal(airPurgeStatus);
    const airPurgeTaskRunning = !airPurgeTaskTerminal &&
      (airPurgeActive || airPurgePending || airPurgeTaskLocked || isCommissioningTaskStatusActive(airPurgeStatus));
    const airPurgeResultReady = /DONE/.test(String(airPurgeStatus || "").toUpperCase());
    const airPurgeAvailable = Boolean(airPurgeControls || state.entities.airPurgeStatus || state.entities.airPurgeReturnToAuto);
    const airPurgeRemaining = getSettingsStatValue("airPurgeRemaining", { decimals: 0 });
    const airPurgePhaseCode = getEntityNumericValue("airPurgePhase");
    const airPurgePhase = airPurgePhaseCode === 1
      ? t("settingsService.phaseQuiet")
      : airPurgePhaseCode === 2
        ? t("settingsService.phasePulse")
        : airPurgePhaseCode === 3
          ? t("settingsService.phaseStab")
          : airPurgeProgress.phase;
    const manualFlowStatus = getStatusTextValue("manualFlowStatus", "IDLE");
    const manualFlowActive = isEntityActive("manualFlowActive");
    const manualFlowBusy = state.loadingEntities || state.busyAction === "manualFlowStart" || state.busyAction === "manualFlowAbort";
    const manualFlowControls = Boolean(state.entities.manualFlowStart || state.entities.manualFlowAbort);
    const manualFlowPending = Boolean(state.pendingManualFlowStart);
    const manualFlowTaskLocked = state.commissioningTaskLock === "manual-flow";
    const manualFlowTaskTerminal = isCommissioningTaskStatusTerminal(manualFlowStatus);
    const manualFlowTaskRunning = !manualFlowTaskTerminal &&
      (manualFlowActive || manualFlowPending || manualFlowTaskLocked || isCommissioningTaskStatusActive(manualFlowStatus));
    const manualHpStatus = getStatusTextValue("manualHpStatus", "IDLE");
    const manualHpActive = isEntityActive("manualHpActive");
    const manualHpBusy = state.loadingEntities || state.busyAction === "manualHpStart" || state.busyAction === "manualHpAbort";
    const manualHpControls = Boolean(state.entities.manualHpStart || state.entities.manualHpAbort);
    const manualHpPending = Boolean(state.pendingManualHpStart);
    const manualHpTaskLocked = state.commissioningTaskLock === "manual-hp";
    const manualHpTaskTerminal = isCommissioningTaskStatusTerminal(manualHpStatus);
    const manualHpTaskRunning = !manualHpTaskTerminal &&
      (manualHpActive || manualHpPending || manualHpTaskLocked || isCommissioningTaskStatusActive(manualHpStatus));
    const manualHpSafetyStopped = /SAFETY STOP/.test(String(manualHpStatus || "").toUpperCase());
    const manualHpStopping = /STOPPING/.test(String(manualHpStatus || "").toUpperCase());
    const hpWaterCalibrationStatus = getStatusTextValue("hpWaterCalibrationStatus", "IDLE");
    const hpWaterCalibrationProgress = getCommissioningProgressModel(hpWaterCalibrationStatus, "hp-water-calibration");
    const hpWaterCalibrationActive = isEntityActive("hpWaterCalibrationActive");
    const hpWaterCalibrationBusy = state.loadingEntities || state.busyAction === "hpWaterCalibrationStart" || state.busyAction === "hpWaterCalibrationAbort" || state.busyAction === "hpWaterCalibrationApply";
    const hpWaterCalibrationControls = Boolean(state.entities.hpWaterCalibrationStart || state.entities.hpWaterCalibrationAbort || state.entities.hpWaterCalibrationApply);
    const hpWaterCalibrationPending = Boolean(state.pendingHpWaterCalibrationStart);
    const hpWaterCalibrationTaskLocked = state.commissioningTaskLock === "hp-water-calibration";
    const hpWaterCalibrationTaskTerminal = isCommissioningTaskStatusTerminal(hpWaterCalibrationStatus);
    const hpWaterCalibrationTaskRunning = !hpWaterCalibrationTaskTerminal &&
      (hpWaterCalibrationActive || hpWaterCalibrationPending || hpWaterCalibrationTaskLocked || isCommissioningTaskStatusActive(hpWaterCalibrationStatus));
    const hpWaterCalibrationResultReady = /DONE|APPLIED/.test(String(hpWaterCalibrationStatus || "").toUpperCase());
    const hpWaterCalibrationApplied = /APPLIED/.test(String(hpWaterCalibrationStatus || "").toUpperCase());
    const flowKpSuggested = getSettingsStatValue("flowKpSuggested", { decimals: 5, trimTrailingZeros: true });
    const flowKiSuggested = getSettingsStatValue("flowKiSuggested", { decimals: 5, trimTrailingZeros: true });
    const boilerResultReady = isBoilerTestResultReady(boilerStatus);
    const boilerResultApplied = /APPLIED/.test(String(boilerStatus || "").toUpperCase());
    const boilerConfirmationRequired = /CONFIRM_REQUIRED/.test(String(boilerStatus || "").toUpperCase());
    const boilerResultQualityRaw = getSettingsTextStatValue("boilerPowerTestResultQuality");
    const boilerResultQualityDenied = String(boilerResultQualityRaw || "").toUpperCase().includes("REJECTED:");
    const autotuneResultReady = /DONE|APPLIED/.test(String(autotuneStatus || "").toUpperCase());
    const boilerStatusDisplay = (() => {
      const upper = String(boilerStatus || "").toUpperCase();
      if (upper.includes("FAILED")) return t("settingsService.boilerFailed");
      if (upper.startsWith("REFUSED:") || upper === "REFUSED") return t("settingsService.boilerRefused");
      if (upper === "ABORTED" || upper === "ABORT") return t("settingsService.boilerAborted");
      if (upper.startsWith("ABORTED:") || upper.startsWith("ABORT:")) return t("settingsService.boilerAbortedShort");
      if (upper.includes("CONFIRM_REQUIRED")) return t("settingsService.boilerConfirm");
      if (upper.startsWith("DONE:") || upper === "DONE" || upper.includes("APPLIED")) return t("settingsService.boilerDone");
      if (boilerTaskWaitingForCm100) return t("settingsService.waitCm100");
      if (boilerTaskRunning) return boilerProgress.phase;
      if (boilerResultReady) return t("settingsService.readyToApply");
      return cm100Ready ? t("settingsService.readyToStart") : t("settingsService.waitCm100");
    })();
    const autotuneStatusDisplay = cm100Ready
      ? (autotuneTaskWaitingForCm100
        ? t("settingsService.waitCm100")
        : (autotuneTaskRunning
          ? autotuneProgress.phase
          : (autotuneResultReady ? t("settingsService.readyToApply") : t("settingsService.readyToStart"))))
      : t("settingsService.waitCm100");
    const airPurgeStatusDisplay = cm100Ready
      ? (airPurgeTaskRunning
        ? airPurgeProgress.phase
        : (airPurgeResultReady ? t("settingsService.boilerDone") : t("settingsService.readyToStart")))
      : t("settingsService.waitCm100");
    const manualFlowStatusDisplay = cm100Ready
      ? (manualFlowTaskRunning ? t("settingsService.flowActive") : t("settingsService.readyToStart"))
      : t("settingsService.waitCm100");
    const manualHpStatusDisplay = cm100Ready
      ? (manualHpTaskRunning ? (manualHpStopping ? t("settingsService.stopping") : (manualHpSafetyStopped ? t("settingsService.safetyStop") : t("settingsService.flowActive"))) : t("settingsService.readyToStart"))
      : t("settingsService.waitCm100");
    const hpWaterCalibrationStatusDisplay = cm100Ready
      ? (hpWaterCalibrationTaskRunning
        ? hpWaterCalibrationProgress.phase
        : (hpWaterCalibrationApplied ? t("settingsService.offsetsApplied") : (hpWaterCalibrationResultReady ? t("settingsService.readyToApply") : t("settingsService.readyToStart"))))
      : t("settingsService.waitCm100");
    const serviceTaskStates = [
      ["boiler", boilerTaskRunning, boilerTaskLocked],
      ["autotune", autotuneTaskRunning, autotuneTaskLocked],
      ["purge", airPurgeTaskRunning, airPurgeTaskLocked],
      ["manual-flow", manualFlowTaskRunning, manualFlowTaskLocked],
      ["manual-hp", manualHpTaskRunning, manualHpTaskLocked],
      ["hp-water-calibration", hpWaterCalibrationTaskRunning, hpWaterCalibrationTaskLocked],
    ];
    const anyTaskRunning = serviceTaskStates.some(([, running]) => running);
    const startDisabled = (key, busy, controls, pending) => !cm100Ready || busy || !controls || pending || anyTaskRunning
      || serviceTaskStates.some(([taskKey, , locked]) => locked && taskKey !== key);
    const boilerStartDisabled = startDisabled("boiler", boilerBusy, boilerControls, boilerPending);
    const boilerAbortDisabled = boilerBusy || !(boilerTaskRunning || boilerTaskLocked || boilerPending);
    const boilerApplyDisabled = boilerBusy || boilerStartDisabled || !boilerResultReady || boilerResultApplied || boilerResultQualityDenied || autotuneTaskRunning || airPurgeTaskRunning || hpWaterCalibrationTaskRunning;
    const autotuneStartDisabled = startDisabled("autotune", autotuneBusy, autotuneControls, autotunePending);
    const autotuneAbortDisabled = autotuneBusy || !(autotuneTaskRunning || autotuneTaskLocked || autotunePending);
    const autotuneApplyDisabled = autotuneBusy || autotuneStartDisabled || !autotuneResultReady || boilerTaskRunning || airPurgeTaskRunning || hpWaterCalibrationTaskRunning;
    const airPurgeStartDisabled = startDisabled("purge", airPurgeBusy, airPurgeControls, airPurgePending);
    const airPurgeAbortDisabled = airPurgeBusy || !(airPurgeTaskRunning || airPurgeTaskLocked || airPurgePending);
    const manualFlowStartDisabled = startDisabled("manual-flow", manualFlowBusy, manualFlowControls, manualFlowPending);
    const manualFlowAbortDisabled = manualFlowBusy || !(manualFlowTaskRunning || manualFlowTaskLocked || manualFlowPending);
    const manualHpStartDisabled = startDisabled("manual-hp", manualHpBusy, manualHpControls, manualHpPending);
    const manualHpAbortDisabled = manualHpBusy || !(manualHpTaskRunning || manualHpTaskLocked || manualHpPending);
    const hpWaterCalibrationStartDisabled = startDisabled("hp-water-calibration", hpWaterCalibrationBusy, hpWaterCalibrationControls, hpWaterCalibrationPending);
    const hpWaterCalibrationAbortDisabled = hpWaterCalibrationBusy || !(hpWaterCalibrationTaskRunning || hpWaterCalibrationTaskLocked || hpWaterCalibrationPending);
    const hpWaterCalibrationApplyDisabled = hpWaterCalibrationBusy || hpWaterCalibrationTaskRunning || !hpWaterCalibrationResultReady || hpWaterCalibrationApplied;

    if (cm100Pending && cm100Ready) {
      state.pendingCommissioningCm100Start = false;
    }
    if (cm100TaskLocked && (cm100Ready || /READY|STOPPED|DONE|FAILED|ABORT|APPLIED|REFUSED/.test(cm100StatusUpper))) {
      state.commissioningTaskLock = "";
    }
    if (boilerPending && (boilerActive || isCommissioningTaskStatusTerminal(boilerStatus))) {
      state.pendingBoilerPowerTestStart = false;
    }
    if (boilerTaskLocked && isCommissioningTaskStatusTerminal(boilerStatus)) {
      state.commissioningTaskLock = "";
    }
    if (autotunePending && isCommissioningTaskStatusTerminal(autotuneStatus)) {
      state.pendingFlowAutotuneStart = false;
    }
    if (autotuneTaskLocked && isCommissioningTaskStatusTerminal(autotuneStatus)) {
      state.commissioningTaskLock = "";
    }
    if (airPurgePending && (airPurgeActive || isCommissioningTaskStatusTerminal(airPurgeStatus))) {
      state.pendingAirPurgeStart = false;
    }
    if (airPurgeTaskLocked && isCommissioningTaskStatusTerminal(airPurgeStatus)) {
      state.commissioningTaskLock = "";
    }
    if (manualFlowPending && (manualFlowActive || isCommissioningTaskStatusTerminal(manualFlowStatus))) {
      state.pendingManualFlowStart = false;
    }
    if (manualFlowTaskLocked && (manualFlowActive || isCommissioningTaskStatusTerminal(manualFlowStatus))) {
      state.commissioningTaskLock = "";
    }
    if (manualHpPending && (manualHpActive || isCommissioningTaskStatusTerminal(manualHpStatus))) {
      state.pendingManualHpStart = false;
    }
    if (manualHpTaskLocked && (manualHpActive || isCommissioningTaskStatusTerminal(manualHpStatus))) {
      state.commissioningTaskLock = "";
    }
    if (hpWaterCalibrationPending && (hpWaterCalibrationActive || isCommissioningTaskStatusTerminal(hpWaterCalibrationStatus))) {
      state.pendingHpWaterCalibrationStart = false;
    }
    if (hpWaterCalibrationTaskLocked && isCommissioningTaskStatusTerminal(hpWaterCalibrationStatus)) {
      state.commissioningTaskLock = "";
    }

    const cm100StatusDisplay = cm100WaitingForCm100 ? t("settingsService.waitCm100") : cm100Status;
    const serviceStatusCopy = cm100WaitingForCm100
      ? t("settingsService.cm100WaitingCopy")
      : (cm100Ready ? t("settingsService.cm100ReadyCopy") : t("settingsService.cm100StartCopy"));

    const tasks = [
      {
        key: "hp-water-calibration",
        title: t("settingsService.calTitle"),
        label: t("settingsService.calLabel"),
        summary: t("settingsService.calSummary"),
        status: hpWaterCalibrationStatusDisplay,
        available: Boolean(hpWaterCalibrationControls || state.entities.hpWaterCalibrationStatus),
        openDisabled: !cm100Ready,
        renderCard: () => renderCommissioningTaskCard({
          taskKey: "hp-water-calibration",
          title: t("settingsService.calTitle"),
          copy: t("settingsService.calCopy"),
          subcopy: t("settingsService.calSubcopy"),
          status: hpWaterCalibrationStatusDisplay,
          statusCopy: hpWaterCalibrationTaskRunning
            ? t("settingsService.calRunning")
            : (hpWaterCalibrationResultReady ? t("settingsService.calCheck") : (cm100Ready ? t("settingsService.calReady") : t("settingsService.calStartCm100"))),
          progressTask: "hp-water-calibration",
          controls: renderHpWaterCalibrationWizard({
            status: hpWaterCalibrationStatus,
            running: hpWaterCalibrationTaskRunning,
            resultReady: hpWaterCalibrationResultReady,
            startDisabled: hpWaterCalibrationStartDisabled,
            abortDisabled: hpWaterCalibrationAbortDisabled,
            applyDisabled: hpWaterCalibrationApplyDisabled,
            busy: hpWaterCalibrationBusy,
            controlsAvailable: Boolean(state.entities.hpWaterCalibrationStart || state.entities.hpWaterCalibrationAbort),
          }),
          className: "oq-settings-commissioning-card--hp-water-calibration",
        }),
      },
      {
        key: "manual-flow",
        title: t("settingsService.flowTitle"),
        label: t("settingsService.flowLabel"),
        summary: t("settingsService.flowSummary"),
        status: manualFlowStatusDisplay,
        available: Boolean(manualFlowControls || state.entities.manualFlowStatus),
        openDisabled: !cm100Ready,
        renderCard: () => renderCommissioningTaskCard({
          taskKey: "manual-flow",
          title: t("settingsService.flowTitle"),
          copy: t("settingsService.flowCopy"),
          subcopy: t("settingsService.flowSubcopy"),
          status: manualFlowStatusDisplay,
          statusCopy: manualFlowTaskRunning
            ? t("settingsService.flowRunning")
            : (cm100Ready ? t("settingsService.flowReady") : t("settingsService.calStartCm100")),
          progressTask: "",
          controls: `
            <div class="oq-settings-manual-flow-control">
              ${renderSettingsSliderField("manualFlowSetpoint", t("settingsService.flowTempTitle"), t("settingsService.flowTempCopy"), "oq-settings-field--compact")}
              ${state.entities.manualFlowStart || state.entities.manualFlowAbort ? renderNamedToggleActionButton({
                active: manualFlowTaskRunning,
                startKey: "manualFlowStart",
                stopKey: "manualFlowAbort",
                startLabel: t("settingsService.pumpStart"),
                stopLabel: t("settingsService.pumpStop"),
                startDisabled: manualFlowBusy || manualFlowStartDisabled,
                stopDisabled: manualFlowBusy || manualFlowAbortDisabled,
              }) : ""}
            </div>
          `,
          metrics: `
            <p class="oq-settings-manual-flow-results-title">${escapeHtml(t("settingsService.resultsTitle"))}</p>
            ${renderSettingsStaticField("flowSelected", t("settingsService.measuredFlow"), t("settingsService.measuredFlowCopy"), getSettingsStatValue("flowSelected"), "oq-settings-field--compact")}
            ${renderSettingsStaticField("manualFlowTargetIpwm", t("settingsService.pumpLevel"), t("settingsService.pumpLevelCopy"), getSettingsStatValue("manualFlowTargetIpwm"), "oq-settings-field--compact")}
          `,
        }),
        renderModalActions: () => `
          ${state.entities.manualFlowApplyHeating ? renderNamedActionButton("manualFlowApplyHeating", t("settingsService.applyHeat"), "oq-helper-button oq-helper-button--ghost", manualFlowBusy) : ""}
          ${state.entities.manualFlowApplyCooling ? renderNamedActionButton("manualFlowApplyCooling", t("settingsService.applyCool"), "oq-helper-button oq-helper-button--ghost", manualFlowBusy) : ""}
        `,
      },
      {
        key: "manual-hp",
        title: t("settingsService.hpTitle"),
        label: t("settingsService.hpLabel"),
        summary: t("settingsService.hpSummary"),
        status: manualHpStatusDisplay,
        available: Boolean(manualHpControls || state.entities.manualHpStatus),
        openDisabled: !cm100Ready,
        renderCard: () => renderCommissioningTaskCard({
          taskKey: "manual-hp",
          title: t("settingsService.hpTitle"),
          copy: t("settingsService.hpCopy"),
          subcopy: t("settingsService.hpSubcopy"),
          status: manualHpStatusDisplay,
          statusCopy: manualHpTaskRunning
            ? (manualHpStopping
              ? t("settingsService.hpStopping")
              : manualHpSafetyStopped
              ? t("settingsService.hpSafety")
              : t("settingsService.hpActive"))
            : (cm100Ready ? t("settingsService.hpReady") : t("settingsService.calStartCm100")),
          progressTask: "",
          actions: `
            ${state.entities.manualHpStart || state.entities.manualHpAbort ? renderNamedToggleActionButton({
              active: manualHpTaskRunning,
              startKey: "manualHpStart",
              stopKey: "manualHpAbort",
              startLabel: t("settingsService.hpStart"),
              stopLabel: t("settingsService.hpStop"),
              startDisabled: manualHpBusy || manualHpStartDisabled,
              stopDisabled: manualHpBusy || manualHpAbortDisabled,
            }) : ""}
          `,
          controls: `
            <div class="oq-settings-manual-hp-controls">
              <div class="oq-settings-manual-hp-unit">
                ${renderSettingsSelectField("manualHp1Mode", t("settingsService.hp1Mode"), t("settingsService.hpModeCopy"), "oq-settings-field--compact")}
                ${renderSettingsSliderField("manualHp1Level", t("settingsService.hp1Level"), t("settingsService.hpLevelCopy", { max: formatNumber(hp1ManualMaxLevel, { maximumFractionDigits: 0 }) }), "oq-settings-field--compact", { maxValue: hp1ManualMaxLevel })}
              </div>
              ${hasEntity("manualHp2Mode") ? `
                <div class="oq-settings-manual-hp-unit">
                  ${renderSettingsSelectField("manualHp2Mode", t("settingsService.hp2Mode"), t("settingsService.hpModeCopy"), "oq-settings-field--compact")}
                  ${renderSettingsSliderField("manualHp2Level", t("settingsService.hp2Level"), t("settingsService.hpLevelCopy", { max: formatNumber(hp2ManualMaxLevel, { maximumFractionDigits: 0 }) }), "oq-settings-field--compact", { maxValue: hp2ManualMaxLevel })}
                </div>
              ` : ""}
            </div>
          `,
          metrics: `
            <p class="oq-settings-manual-flow-results-title">${escapeHtml(t("settingsService.resultsTitle"))}</p>
            <div class="oq-settings-manual-hp-results">
              ${renderSettingsStaticField("flowSelected", t("settingsService.measuredFlow"), t("settingsService.measuredFlowCopy"), getSettingsStatValue("flowSelected"), "oq-settings-field--compact")}
              ${renderSettingsStaticField("hp1Compressor", t("settingsService.hp1Actual"), t("settingsService.hpActualCopy"), getManualHpActualValue("hp1Compressor", "hp1Freq"), "oq-settings-field--compact")}
              ${hasEntity("hp2Compressor") ? renderSettingsStaticField("hp2Compressor", t("settingsService.hp2Actual"), t("settingsService.hpActualCopy"), getManualHpActualValue("hp2Compressor", "hp2Freq"), "oq-settings-field--compact") : ""}
            </div>
            ${renderSettingsStaticField("manualHpGuardStatus", t("settingsService.guardTitle"), t("settingsService.guardCopy"), getEntityValue("manualHpGuardStatus") || t("settingsService.guardReleased"), "oq-settings-field--compact oq-settings-field--full")}
            <div class="oq-settings-manual-hp-statuses">
              ${renderSettingsStaticField("hp1Failures", t("settingsService.hp1Failure"), t("settingsService.hpFailureCopy"), formatFailures(getEntityStateText("hp1Failures", "None")), "oq-settings-field--compact")}
              ${hasEntity("hp2Failures") ? renderSettingsStaticField("hp2Failures", t("settingsService.hp2Failure"), t("settingsService.hpFailureCopy"), formatFailures(getEntityStateText("hp2Failures", "None")), "oq-settings-field--compact") : ""}
            </div>
          `,
        }),
      },
      {
        key: "autotune",
        title: t("settingsService.autotuneTitle"),
        label: t("settingsService.autotuneLabel"),
        summary: t("settingsService.autotuneSummary"),
        status: autotuneStatusDisplay,
        available: true,
        openDisabled: isCommissioningTaskStatusWaitingForCm100(autotuneStatusDisplay),
        renderCard: () => renderCommissioningTaskCard({
          taskKey: "autotune",
          title: t("settingsService.autotuneTitle"),
          copy: t("settingsService.autotuneCopy"),
          subcopy: t("settingsService.autotuneSubcopy"),
          status: autotuneStatusDisplay,
          statusCopy: autotuneTaskWaitingForCm100
            ? t("settingsService.autotuneWait")
            : (autotuneTaskRunning
              ? t("settingsService.autotuneRunning")
              : (cm100Ready ? t("settingsService.autotuneReady") : t("settingsService.autotuneStartFirst"))),
          progressTask: "autotune",
          actions: `
            ${state.entities.flowAutotuneStart || state.entities.flowAutotuneAbort ? renderNamedToggleActionButton({
              active: autotuneTaskRunning,
              startKey: "flowAutotuneStart",
              stopKey: "flowAutotuneAbort",
              startLabel: t("settingsService.autotuneStart"),
              stopLabel: t("settingsService.autotuneStop"),
              startDisabled: autotuneBusy || autotuneStartDisabled,
              stopDisabled: autotuneBusy || autotuneAbortDisabled,
            }) : ""}
            ${state.entities.flowAutotuneApply ? renderNamedActionButton("flowAutotuneApply", t("settingsService.applyBtn"), "oq-helper-button oq-helper-button--ghost", autotuneBusy || autotuneApplyDisabled) : ""}
          `,
          metrics: `
            ${renderSettingsStaticField("flowKpSuggested", t("settingsService.kpSuggested"), t("settingsService.kpCopy"), flowKpSuggested, "oq-settings-field--compact")}
            ${renderSettingsStaticField("flowKiSuggested", t("settingsService.kiSuggested"), t("settingsService.kiCopy"), flowKiSuggested, "oq-settings-field--compact")}
          `,
        }),
      },
      {
        key: "boiler",
        title: t("settingsService.boilerTitle"),
        label: t("settingsService.boilerLabel"),
        summary: t("settingsService.boilerSummary"),
        status: boilerStatusDisplay,
        available: hasBoilerAssist,
        openDisabled: isCommissioningTaskStatusWaitingForCm100(boilerStatusDisplay),
        renderCard: () => renderCommissioningTaskCard({
          taskKey: "boiler",
          title: t("settingsService.boilerTitle"),
          copy: t("settingsService.boilerCopy"),
          subcopy: t("settingsService.boilerRated", { value: boilerRatedPower }),
          status: boilerStatusDisplay,
          statusCopy: boilerTaskWaitingForCm100
            ? t("settingsService.boilerWait")
            : (isCommissioningTaskStatusTerminal(boilerStatus) || boilerTaskRunning
              ? getBoilerTestStatusCopy(
                  boilerStatus,
                  getEntityNumericValue("flowSelected"),
                  getEntityNumericValue("flowSetpoint") || 800,
                )
              : (cm100Ready ? t("settingsService.boilerReady") : t("settingsService.boilerStartFirst"))),
          progressTask: "boiler",
          actions: `
            ${state.entities.boilerPowerTestStart || state.entities.boilerPowerTestAbort ? renderNamedToggleActionButton({
              active: boilerTaskRunning,
              startKey: "boilerPowerTestStart",
              stopKey: "boilerPowerTestAbort",
              startLabel: t("settingsService.boilerStart"),
              stopLabel: t("settingsService.boilerStop"),
              startDisabled: boilerBusy || boilerStartDisabled,
              stopDisabled: boilerBusy || boilerAbortDisabled,
            }) : ""}
            ${state.entities.boilerPowerTestApply ? renderNamedActionButton("boilerPowerTestApply", boilerConfirmationRequired ? t("settingsService.boilerConfirmApply") : t("settingsService.applyBtn"), "oq-helper-button oq-helper-button--ghost", boilerBusy || boilerApplyDisabled) : ""}
          `,
          metrics: `
            ${renderSettingsStaticField("boilerHeatPower", t("settingsService.boilerPower"), t("settingsService.boilerPowerCopy"), boilerHeatPower)}
            ${renderSettingsStaticField("boilerPowerTestResult", t("settingsService.boilerResult"), t("settingsService.boilerResultCopy"), getSettingsStatValue("boilerPowerTestResult"))}
          `,
        }),
      },
      {
        key: "purge",
        title: t("settingsService.purgeTitle"),
        label: t("settingsService.purgeTitle"),
        summary: t("settingsService.purgeSummary"),
        status: airPurgeStatusDisplay,
        available: airPurgeAvailable,
        openDisabled: isCommissioningTaskStatusWaitingForCm100(airPurgeStatusDisplay),
        renderCard: () => renderCommissioningTaskCard({
          taskKey: "purge",
          title: t("settingsService.purgeTitle"),
          copy: t("settingsService.purgeCopy"),
          subcopy: t("settingsService.purgeSubcopy"),
          status: airPurgeStatusDisplay,
          statusCopy: airPurgeTaskRunning
            ? t("settingsService.purgeRunning")
            : (cm100Ready ? t("settingsService.purgeReady") : t("settingsService.purgeStartFirst")),
          progressTask: "purge",
          className: "oq-settings-commissioning-card--air-purge",
          actions: `
            ${state.entities.airPurgeStart || state.entities.airPurgeAbort ? renderNamedToggleActionButton({
              active: airPurgeTaskRunning,
              startKey: "airPurgeStart",
              stopKey: "airPurgeAbort",
              startLabel: t("settingsService.purgeStart"),
              stopLabel: t("settingsService.purgeStop"),
              startDisabled: airPurgeBusy || airPurgeStartDisabled,
              stopDisabled: airPurgeBusy || airPurgeAbortDisabled,
            }) : ""}
          `,
          metrics: `
            ${renderSettingsStaticField("airPurgeRemaining", t("settingsService.purgeRemaining"), t("settingsService.purgeRemainingCopy"), airPurgeRemaining, "oq-settings-field--compact")}
            ${renderSettingsStaticField("airPurgePhase", t("settingsService.purgePhase"), t("settingsService.purgePhaseCopy"), airPurgePhase, "oq-settings-field--compact")}
            ${renderSettingsStaticField("flowSelected", t("settingsService.purgeFlow"), t("settingsService.purgeFlowCopy"), getSettingsStatValue("flowSelected"), "oq-settings-field--compact")}
            ${renderSettingsCheckboxSwitchField(
              "airPurgeReturnToAuto",
              t("settingsService.purgeAfter"),
              "",
              t("settingsService.purgeCloseCm100"),
              "oq-settings-field--span-2 oq-settings-field--compact"
            )}
          `,
        }),
      },
    ].filter((task) => task.available);

    return {
      cm100Status: cm100StatusDisplay,
      cm100StartDisabled,
      cm100StopDisabled,
      serviceStatusCopy,
      tasks,
    };
  }

  export function renderSettingsServiceTaskRow(task) {
    return renderSettingsSystemRow({
      dataAttribute: "data-oq-service-task",
      dataValue: task.key,
      className: "oq-settings-service-row",
      label: task.label,
      value: task.status,
      note: task.summary,
      action: `<button
          class="oq-helper-button oq-helper-button--ghost"
          type="button"
          data-oq-action="open-service-task-modal"
          data-service-task="${escapeHtml(task.key)}"
          ${task.openDisabled ? "disabled" : ""}
        >
          ${task.openDisabled ? escapeHtml(t("settingsService.taskWaitCm100")) : escapeHtml(t("settingsService.taskOpen"))}
        </button>`,
    });
  }

  export function getControlModeOverrideLabel(value) {
    const labels = {
      Auto: t("settingsService.overrideAuto"),
      "Force CM0": t("settingsService.overrideCm0"),
      "Force CM1": t("settingsService.overrideCm1"),
      "Force CM98": t("settingsService.overrideCm98"),
    };
    return labels[String(value || "")] || String(value || t("common.unknown"));
  }

  export function renderSettingsControlModeOverridePanel() {
    if (!hasEntity("controlModeOverride")) {
      return "";
    }

    const currentValue = String(getEntityValue("controlModeOverride") || "Auto");
    const active = currentValue !== "Auto";
    const busy = state.loadingEntities || state.busyAction === "save-controlModeOverride";
    const entity = state.entities.controlModeOverride || {};
    const options = (Array.isArray(entity.option) ? entity.option : entity.options || [])
      .filter((option) => ["Auto", "Force CM0", "Force CM1", "Force CM98"].includes(option));

    return `
      <div class="oq-settings-service-override${active ? " is-active" : ""}">
        <div class="oq-settings-service-override-copy">
          <p class="oq-helper-label">${escapeHtml(active ? t("header.testModeActive") : t("settingsService.overrideIdleKicker"))}</p>
          <h4>${escapeHtml(active ? getControlModeOverrideLabel(currentValue) : t("settingsService.overrideForceTitle"))}</h4>
          <p>${escapeHtml(active
            ? t("settingsService.overrideActiveCopy")
            : t("settingsService.overrideIdleCopy"))}</p>
        </div>
        <div class="oq-settings-service-override-actions">
          ${options.map((option) => {
            if (option === "Auto") {
              return active ? `<button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="clear-control-mode-override" ${busy ? "disabled" : ""}>${escapeHtml(t("header.backToAuto"))}</button>` : "";
            }
            if (option === currentValue) {
              return "";
            }
            return `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="open-control-mode-override-confirm" data-control-mode-option="${escapeHtml(option)}" ${busy ? "disabled" : ""}>${escapeHtml(getControlModeOverrideLabel(option))}</button>`;
          }).join("")}
        </div>
      </div>
    `;
  }

  export function renderSettingsCounterServiceSection() {
    const runtimeResetKey = hasEntity("resetRuntimeCountersHp1Hp2")
      ? "resetRuntimeCountersHp1Hp2"
      : hasEntity("resetRuntimeCountersHp1") ? "resetRuntimeCountersHp1" : "";
    const hasHp1Runtime = hasEntity("hp1RuntimeHours");
    const hasHp2Runtime = hasEntity("hp2RuntimeHours");
    const hp1Hours = getEntityNumericValue("hp1RuntimeHours");
    const hp2Hours = getEntityNumericValue("hp2RuntimeHours");
    const hasRuntimeDifference = Number.isFinite(hp1Hours) && Number.isFinite(hp2Hours);
    const runtimeDifference = hasRuntimeDifference ? Math.abs(hp1Hours - hp2Hours) : Number.NaN;
    const runtimeDifferenceLabel = hasRuntimeDifference
      ? t("settingsService.runtimeDiff", { value: formatNumber(runtimeDifference, { maximumFractionDigits: 1 }) })
      : t("settingsService.runtimeDiffUnknown");
    const runtimeDifferenceDetail = hasRuntimeDifference
      ? hp1Hours === hp2Hours
        ? t("settingsService.runtimeEven")
        : t("settingsService.runtimeLeader", { hp: hp1Hours > hp2Hours ? "HP1" : "HP2" })
      : t("settingsService.runtimeLoading");
    const runtimeDifferenceClass = !hasRuntimeDifference || hp1Hours === hp2Hours
      ? "is-even"
      : hp1Hours > hp2Hours ? "is-hp1-higher" : "is-hp2-higher";
    const runtimeDifferenceSpan = hasRuntimeDifference && Math.max(Math.abs(hp1Hours), Math.abs(hp2Hours)) > 0
      ? Math.min(28, Math.max(8, (runtimeDifference / Math.max(Math.abs(hp1Hours), Math.abs(hp2Hours))) * 500))
      : 0;
    const runtimeLeadValue = hasEntity("runtimeLeadHp") ? getSettingsTextStatValue("runtimeLeadHp", "") : "";
    const runtimeLead = ["HP1", "HP2"].includes(runtimeLeadValue) ? runtimeLeadValue : "";
    const runtimeLeadMarkup = runtimeLead
      ? `<span class="oq-settings-runtime-lead"><span aria-hidden="true"></span>${escapeHtml(t("settingsService.runtimeLead", { hp: runtimeLead }))}</span>`
      : "";
    const runtimeResetMarkup = runtimeResetKey
      ? `<button class="oq-settings-runtime-reset" type="button" data-oq-action="open-runtime-reset-confirm" aria-label="${escapeHtml(t("settingsService.runtimeResetAria"))}" ${state.busyAction === runtimeResetKey ? "disabled" : ""}>${state.busyAction === runtimeResetKey ? escapeHtml(t("settingsService.runtimeResetBusy")) : escapeHtml(t("settingsService.runtimeReset"))}</button>`
      : "";
    const runtimeMarkup = hasHp1Runtime || hasHp2Runtime
      ? `
        <div class="oq-settings-runtime-balance${hasHp2Runtime ? "" : " is-single"}">
          <div class="oq-settings-runtime-balance-head">
            <p>${escapeHtml(t("settingsService.runtimeBalance"))}</p>
            <div class="oq-settings-runtime-balance-head-actions">
              ${runtimeLeadMarkup}
              ${runtimeResetMarkup}
            </div>
          </div>
          <div class="oq-settings-runtime-balance-grid">
            ${hasHp1Runtime ? `
              <div class="oq-settings-runtime-metric oq-settings-runtime-metric--hp1">
                <span>HP1</span>
                <strong>${escapeHtml(getSettingsStatValue("hp1RuntimeHours"))}</strong>
              </div>
            ` : ""}
            ${hasHp2Runtime ? `
              <div class="oq-settings-runtime-comparison" aria-label="${escapeHtml(`${runtimeDifferenceLabel}. ${runtimeDifferenceDetail}`)}">
                <span class="oq-settings-runtime-track ${runtimeDifferenceClass}" style="--oq-runtime-delta-span: ${runtimeDifferenceSpan.toFixed(1)}%;" aria-hidden="true"></span>
                <strong>${escapeHtml(runtimeDifferenceLabel)}</strong>
                <small>${escapeHtml(runtimeDifferenceDetail)}</small>
              </div>
              <div class="oq-settings-runtime-metric oq-settings-runtime-metric--hp2">
                <span>HP2</span>
                <strong>${escapeHtml(getSettingsStatValue("hp2RuntimeHours"))}</strong>
              </div>
            ` : `<p class="oq-settings-runtime-single-copy">${escapeHtml(t("settingsService.runtimeSingle"))}</p>`}
          </div>
        </div>
      `
      : "";

    if (!runtimeMarkup) {
      return "";
    }

    return renderSettingsSection(
      t("settingsService.counterGroup"),
      t("settingsService.counterTitle"),
      t("settingsService.counterCopy"),
      `
        <div class="oq-settings-maintenance-shell" id="oq-settings-maintenance">
          ${runtimeMarkup}
        </div>
      `,
      "",
      "oq-settings-section--maintenance",
    );
  }

  export function renderSettingsServiceSection() {
    const service = getSettingsServiceModel();

    return renderSettingsSection(
      t("settingsService.serviceGroup"),
      t("settingsService.serviceTitle"),
      t("settingsService.serviceCopy"),
      `
        <div class="oq-settings-service-shell">
          ${renderSettingsControlModeOverridePanel()}
          <div class="oq-settings-service-toolbar">
            <div class="oq-settings-commissioning-teaser-status">
              <span class="oq-settings-commissioning-teaser-status-label">${escapeHtml(t("settings.qsCurrentStatus"))}</span>
              <strong>${escapeHtml(service.cm100Status)}</strong>
              <p>${escapeHtml(service.serviceStatusCopy)}</p>
            </div>
            <div class="oq-settings-commissioning-hero-actions oq-settings-service-toolbar-actions">
              ${state.entities.commissioningCm100Start ? renderNamedActionButton("commissioningCm100Start", t("settingsService.serviceStart"), "oq-helper-button oq-helper-button--primary", service.cm100StartDisabled) : ""}
              ${state.entities.commissioningCm100Stop ? renderNamedActionButton("commissioningCm100Stop", t("settingsService.serviceStop"), "oq-helper-button oq-helper-button--ghost", service.cm100StopDisabled) : ""}
            </div>
          </div>

          <div class="oq-settings-system-summary oq-settings-service-task-list">
            ${service.tasks.map((task) => renderSettingsServiceTaskRow(task)).join("")}
            ${renderSettingsOduEepromDumpRow()}
          </div>
        </div>
      `,
    );
  }

  export function renderSettingsOduEepromDumpRow() {
    return renderSettingsSystemRow({
      className: "oq-settings-service-row oq-settings-odu-eeprom-row",
      label: t("settingsService.eepromLabel"),
      value: t("settingsService.eepromValue"),
      note: t("settingsService.eepromNote"),
      action: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="open-odu-eeprom-dump-modal">${escapeHtml(t("settingsInstallation.openAction"))}</button>`,
    });
  }

  export function renderSettingsServiceTaskModal() {
    const taskKey = String(state.systemModal || "").replace(/^service-task-/, "");
    const service = getSettingsServiceModel();
    const task = service.tasks.find((item) => item.key === taskKey);
    if (!task) {
      return "";
    }

    return renderModalShell({
      id: "system",
      titleId: "oq-service-task-modal-title",
      kicker: t("settingsService.taskModalKicker"),
      title: task.title,
      copy: task.summary,
      className: "oq-helper-modal--wide oq-helper-modal--scrollable oq-helper-modal--service-task",
      sectionAttributes: "data-oq-service-task-scroller",
      closeAction: "close-system-modal",
      closeLabel: t("settingsService.taskModalClose", { title: task.title }),
      body: `<div class="oq-settings-service-task-modal-body">${task.renderCard()}</div>`,
      actions: `${task.renderModalActions?.() || ""}<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal">${escapeHtml(t("common.close"))}</button>`,
    });
  }
