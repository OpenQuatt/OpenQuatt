import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { getEntityValue } from "../core/entity-store.js";
import { formatFailures } from "../core/failure-format.js";
import { state } from "../core/state.js";
import { formatSettingsNumberValue, getCommissioningStatusValue, getSettingsStatValue, getSettingsTemperatureValue, getSettingsTextStatValue, getStatusTextValue, renderNamedActionButton, renderNamedToggleActionButton, renderSettingsCheckboxSwitchField, renderSettingsSection, renderSettingsSelectField, renderSettingsSliderField, renderSettingsStaticField, renderSettingsSystemRow } from "./controls.js";
import { getHpWaterRawValue } from "./water.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";

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
      || normalized.includes("REFUSED");
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
        { match: ["REQUESTED", "WAITING_FOR_CM100", "REFUSED"], phase: "Voorbereiden", percent: 12 },
        { match: ["FLOW_SETTLING"], phase: "Flow stabiliseren", percent: 28 },
        { match: ["BOILER_SETTLING"], phase: "Ketel starten", percent: 48 },
        { match: ["MEASURING"], phase: "Vermogen meten", percent: 72 },
        { match: ["COOLDOWN"], phase: "Test afronden", percent: 90 },
        { match: ["DONE", "APPLIED"], phase: "Klaar", percent: 100 },
        { match: ["ABORTED", "FAILED", "ABORT"], phase: "Afgebroken", percent: 100 },
      ],
      autotune: [
        { match: ["REQUESTED", "WAITING_FOR_CM100", "REFUSED"], phase: "Voorbereiden", percent: 10 },
        { match: ["WAITING_FOR_FLOW", "SETTLING"], phase: "Flow stabiliseren", percent: 26 },
        { match: ["STEP2"], phase: "Staptest 2", percent: 56 },
        { match: ["STEP", "STEP1"], phase: "Staptest 1", percent: 42 },
        { match: ["VALIDATING_SETTLING"], phase: "Flow valideren", percent: 70 },
        { match: ["VALIDATING"], phase: "Flow valideren", percent: 84 },
        { match: ["RECOVERING"], phase: "Herstellen", percent: 92 },
        { match: ["DONE", "APPLIED"], phase: "Klaar", percent: 100 },
        { match: ["ABORTED", "FAILED", "ABORT"], phase: "Afgebroken", percent: 100 },
      ],
      purge: [
        { match: ["REQUESTED", "STARTED", "REFUSED"], phase: "Voorbereiden", percent: 8 },
        { match: ["PHASE1", "STEADY"], phase: "Rustige doorstroming", percent: 22 },
        { match: ["PHASE2", "PULSE"], phase: "Pulsen", percent: 62 },
        { match: ["PHASE3", "STABILIZE"], phase: "Stabiliseren", percent: 90 },
        { match: ["DONE"], phase: "Klaar", percent: 100 },
        { match: ["ABORTED", "FAILED", "ABORT"], phase: "Afgebroken", percent: 100 },
      ],
      "hp-water-calibration": [
        { match: ["REQUESTED", "STARTED", "REFUSED"], phase: "Voorbereiden", percent: 8 },
        { match: ["MIXING"], phase: "Water mengen", percent: 42 },
        { match: ["MEASURING"], phase: "Sensoren meten", percent: 78 },
        { match: ["DONE", "APPLIED"], phase: "Klaar", percent: 100 },
        { match: ["ABORTED", "FAILED", "ABORT"], phase: "Afgebroken", percent: 100 },
      ],
      cm100: [
        { match: ["REQUESTED"], phase: "Wachten op CM100", percent: 0 },
        { match: ["WAITING_FOR_CM100"], phase: "Wachten op CM100", percent: 0 },
        { match: ["CM100 READY"], phase: "Klaar", percent: 100 },
        { match: ["IDLE"], phase: "Klaar", percent: 100 },
      ],
    };

    if (!value || value === "—" || value === "UNKNOWN" || value === "UNAVAILABLE" || value === "NAN") {
      return { phase: "Wachten", percent: 0 };
    }

    if (value.includes("WAITING") || value.includes("WACHTEN")) {
      return { phase: "Wachten", percent: 0 };
    }

    if (taskType !== "cm100" && (
      value === "IDLE"
      || value === "CM0 - STANDBY"
      || value === "CM100 READY"
      || value === "CM100 STOPPED"
      || value === "GEPAUZEERD"
    )) {
      return { phase: "Wachten", percent: 0 };
    }

    const selected = progressMaps[taskType] || [];
    const match = selected.find((item) => item.match.some((needle) => matchesStatus(needle)));
    if (match) {
      return match;
    }

    if (value.includes("DONE") || value.includes("APPLIED")) {
      return { phase: "Klaar", percent: 100 };
    }
    if (value.includes("ABORT") || value.includes("FAILED") || value.includes("REFUSED")) {
      return { phase: "Afgebroken", percent: 100 };
    }
    if (taskType === "cm100" && value.includes("CM100")) {
      return { phase: "Klaar", percent: 100 };
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
              <p class="oq-settings-quickstart-status-label">Huidige status</p>
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
      ? "Water mengen"
      : Number.isFinite(stableProgress) && Number.isFinite(stableRequired) && stableRequired > 0
        ? (stableProgress > 0
        ? `${Math.round(Math.max(0, stableProgress))} / ${Math.round(stableRequired)} s binnen grenzen`
        : "Nog niet binnen grenzen")
        : "Wachten op stabiel venster";
    const stepIndex = resultReady ? 3 : running ? 2 : 1;
    const statusTitle = applied
      ? "Offsets toegepast"
      : resultReady
      ? `Meting klaar - spreiding ${spreadValue}`
      : running
        ? (mixing
          ? `Water mengen${Number.isFinite(mixingRemaining) && mixingRemaining > 0 ? ` - meting start over ${Math.round(mixingRemaining)} s` : ""}`
          : `Meting bezig - ${Number.isFinite(remaining) && remaining > 0 ? `max. ${Math.round(remaining)} s resterend` : stableCopy}`)
        : failed
          ? "Meting niet voltooid"
          : "Voorbereiding";
    const statusCopy = applied
      ? "De voorgestelde offsets zijn opgeslagen. De aanvoercorrectie blijft alleen actief voor deze bron."
      : resultReady
      ? "Controleer de voorgestelde offsets en pas ze toe."
      : running
        ? (mixing
          ? "De waterpomp circuleert zonder compressor zodat de watertemperaturen eerst kunnen mengen."
          : "De firmware stopt zodra het laatste meetvenster binnen de spreiding- en driftgrenzen valt.")
        : failed
          ? getSettingsTextStatValue("hpWaterCalibrationStatus", "Controleer de voorwaarden en start opnieuw.")
          : (hasHp2
            ? "Start alleen wanneer compressor en boiler uit zijn. HP1, HP2 en de actieve aanvoerbron worden samen naar een relatieve referentie gebracht."
            : "Start alleen wanneer compressor en boiler uit zijn. HP1 water in/out en de actieve aanvoerbron worden samen gekalibreerd.");
    const supplySource = getSettingsTextStatValue(
      "hpWaterCalibrationResultSupplySource",
      getSettingsTextStatValue("waterSupplyTempEffectiveSource", "Actieve bron"),
    );
    const sensorRows = [
      { label: "HP1 water in", rawKey: "hp1WaterInRaw", liveKey: "hp1WaterIn", resultRawKey: "hpWaterCalibrationResultHp1InRawAvg", offsetKey: "hp1WaterInOffset", suggestedKey: "hp1WaterInOffsetSuggested" },
      { label: "HP1 water uit", rawKey: "hp1WaterOutRaw", liveKey: "hp1WaterOut", resultRawKey: "hpWaterCalibrationResultHp1OutRawAvg", offsetKey: "hp1WaterOutOffset", suggestedKey: "hp1WaterOutOffsetSuggested" },
      { label: "HP2 water in", rawKey: "hp2WaterInRaw", liveKey: "hp2WaterIn", resultRawKey: "hpWaterCalibrationResultHp2InRawAvg", offsetKey: "hp2WaterInOffset", suggestedKey: "hp2WaterInOffsetSuggested" },
      { label: "HP2 water uit", rawKey: "hp2WaterOutRaw", liveKey: "hp2WaterOut", resultRawKey: "hpWaterCalibrationResultHp2OutRawAvg", offsetKey: "hp2WaterOutOffset", suggestedKey: "hp2WaterOutOffsetSuggested" },
      { label: `Aanvoer (${supplySource})`, rawKey: "supplyTemp", liveKey: "supplyTemp", resultRawKey: "hpWaterCalibrationResultSupplyRawAvg", offsetKey: "waterSupplyCalibrationOffset", suggestedKey: "waterSupplyCalibrationOffsetSuggested" },
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
          ${renderStep(1, "Voorbereiding")}
          ${renderStep(2, "Meting")}
          ${renderStep(3, "Offsets toepassen")}
        </div>

        <div class="oq-settings-hp-calibration-status${resultReady ? " is-success" : running ? " is-active" : failed ? " is-warning" : ""}">
          <div>
            <strong>${escapeHtml(statusTitle)}</strong>
            <p>${escapeHtml(statusCopy)}</p>
          </div>
          ${running || resultReady ? `<span>${escapeHtml(running ? stableCopy : "Resultaat beschikbaar")}</span>` : ""}
          ${running ? `<div class="oq-settings-hp-calibration-progress"><i style="width: ${progressValue.toFixed(0)}%"></i></div>` : ""}
        </div>

        ${running ? `
          <div class="oq-settings-hp-calibration-live-grid">
            ${sensorRows.map(renderLiveCard).join("")}
            <article class="oq-settings-hp-calibration-live-card is-highlight">
              <span>Spreiding</span>
              <strong>${escapeHtml(getSettingsTemperatureValue("hpWaterCalibrationSpread", 2))}</strong>
            </article>
          </div>
          <p class="oq-settings-hp-calibration-note">De actieve aanvoerbron wordt raw gemeten. Een bestaande aanvoercorrectie telt niet mee in het nieuwe voorstel.</p>
        ` : ""}

        ${resultReady ? `
          <div class="oq-settings-hp-calibration-results">
            <div class="oq-settings-hp-calibration-result-summary">
              <span>Referentie ${escapeHtml(getSettingsTemperatureValue("hpWaterCalibrationResultReference", 2))}</span>
              <span>Aanvoerbron ${escapeHtml(supplySource)}</span>
            </div>
            <div class="oq-settings-hp-calibration-table-wrap">
              <table class="oq-settings-hp-calibration-table">
                <thead>
                  <tr>
                    <th scope="col">Sensor</th>
                    <th scope="col">Raw gemiddelde</th>
                    <th scope="col">Huidig actief</th>
                    <th scope="col">Voorstel</th>
                    <th scope="col">Na toepassen</th>
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
              startLabel: "Kalibratie starten",
              stopLabel: "Meting stoppen",
              startDisabled: busy || startDisabled,
              stopDisabled: busy || abortDisabled,
            })}
            ${state.entities.hpWaterCalibrationApply ? renderNamedActionButton("hpWaterCalibrationApply", "Offsets toepassen", "oq-helper-button oq-helper-button--primary", busy || applyDisabled) : ""}
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
    const flowText = Number.isFinite(flow) ? `${Math.round(flow)} L/h` : "— L/h";
    const targetText = Number.isFinite(target) ? `${Math.round(target)} L/h` : "800 L/h";

    if (upper.includes("FLOW_SETTLING")) {
      return `Flow naar ${targetText} ±40. Ketel start daarna. Min. 2 min. Nu ${flowText}.`;
    }
    if (upper.includes("BOILER_SETTLING")) {
      return `Warmtevraag verstuurd; wachten op ketel. Flow ${flowText} (doel ±40).`;
    }
    if (upper.includes("MEASURING")) {
      const heat = getSettingsStatValue("boilerHeatPower");
      return `Ketel actief; meten.${heat && heat !== "—" ? ` Nu ${heat}.` : ""} Min. 3 min; daarna auto uit zodra meting compleet is.`;
    }
    if (upper.includes("COOLDOWN")) {
      const result = getSettingsStatValue("boilerPowerTestResult");
      return `Metingen klaar; ketel uit.${result && result !== "—" ? ` ${result}.` : ""} 15s afkoelen.`;
    }
    if (upper.startsWith("DONE:") || upper === "DONE" || upper.includes("APPLIED")) {
      const result = getSettingsStatValue("boilerPowerTestResult");
      const conf = getSettingsStatValue("boilerPowerTestConfidence");
      const isFlowLimited = upper.includes("FLOW LIMITED");
      if (result && result !== "—") {
        if (isFlowLimited) {
          return `Klaar - ${result}${conf && conf !== "—" ? ` (${conf})` : ""} - test begrensd door flow/temperatuurmarge.`;
        }
        return `Klaar - ${result}${conf && conf !== "—" ? ` (${conf})` : ""}. Ketel auto uit.`;
      }
      return upper.includes("APPLIED") ? "Resultaat toegepast." : "Klaar - ketel auto uit.";
    }
    if (upper === "ABORTED" || upper === "ABORT") {
      return "Handmatig gestopt. Flow en ketel zijn hersteld naar vorige instelling.";
    }
    if (upper.startsWith("ABORTED:") || upper.startsWith("ABORT:")) {
      const reason = status.slice(status.indexOf(":") + 1).trim();
      return `Afgebroken: ${reason}`;
    }
    if (upper.startsWith("REFUSED:")) {
      const reason = status.slice(status.indexOf(":") + 1).trim();
      return `Start geweigerd: ${reason}`;
    }
    if (upper.includes("FAILED")) {
      const colonIdx = status.indexOf(":");
      if (colonIdx > 0) {
        const reason = status.slice(colonIdx + 1).trim();
        return `Mislukt: ${reason}`;
      }
      return `Mislukt: ${status}`;
    }
    if (upper === "REFUSED") {
      return `Start geweigerd: ${status}`;
    }
    return status;
  }

  export function getSettingsServiceModel() {
    const hasBoilerAssist = hasEntity("boilerCvAssistEnabled") && isEntityActive("boilerCvAssistEnabled");
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
    const hp1CompressorProfile = getSettingsTextStatValue("hp1CompressorLevelProfile", "F0-F10 veilig");
    const hp2CompressorProfile = getSettingsTextStatValue("hp2CompressorLevelProfile", "F0-F10 veilig");
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
      ? "Rustig"
      : airPurgePhaseCode === 2
        ? "Pulsen"
        : airPurgePhaseCode === 3
          ? "Stabiliseren"
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
    const boilerResultReady = /DONE|APPLIED/.test(String(boilerStatus || "").toUpperCase());
    const autotuneResultReady = /DONE|APPLIED/.test(String(autotuneStatus || "").toUpperCase());
    const boilerStatusDisplay = (() => {
      const upper = String(boilerStatus || "").toUpperCase();
      if (upper.includes("FAILED")) return "Mislukt";
      if (upper.startsWith("REFUSED:") || upper === "REFUSED") return "Start geweigerd";
      if (upper === "ABORTED" || upper === "ABORT") return "Handmatig gestopt";
      if (upper.startsWith("ABORTED:") || upper.startsWith("ABORT:")) return "Afgebroken";
      if (upper.startsWith("DONE:") || upper === "DONE" || upper.includes("APPLIED")) return "Klaar";
      if (boilerTaskWaitingForCm100) return "Wachten op CM100";
      if (boilerTaskRunning) return boilerProgress.phase;
      if (boilerResultReady) return "Klaar om toe te passen";
      return cm100Ready ? "Klaar om te starten" : "Wachten op CM100";
    })();
    const autotuneStatusDisplay = cm100Ready
      ? (autotuneTaskWaitingForCm100
        ? "Wachten op CM100"
        : (autotuneTaskRunning
          ? autotuneProgress.phase
          : (autotuneResultReady ? "Klaar om toe te passen" : "Klaar om te starten")))
      : "Wachten op CM100";
    const airPurgeStatusDisplay = cm100Ready
      ? (airPurgeTaskRunning
        ? airPurgeProgress.phase
        : (airPurgeResultReady ? "Klaar" : "Klaar om te starten"))
      : "Wachten op CM100";
    const manualFlowStatusDisplay = cm100Ready
      ? (manualFlowTaskRunning ? "Actief" : "Klaar om te starten")
      : "Wachten op CM100";
    const manualHpStatusDisplay = cm100Ready
      ? (manualHpTaskRunning ? (manualHpStopping ? "Bezig met stoppen" : (manualHpSafetyStopped ? "Veiligheidsstop" : "Actief")) : "Klaar om te starten")
      : "Wachten op CM100";
    const hpWaterCalibrationStatusDisplay = cm100Ready
      ? (hpWaterCalibrationTaskRunning
        ? hpWaterCalibrationProgress.phase
        : (hpWaterCalibrationApplied ? "Offsets toegepast" : (hpWaterCalibrationResultReady ? "Klaar om toe te passen" : "Klaar om te starten")))
      : "Wachten op CM100";
    const boilerStartDisabled = !cm100Ready || boilerBusy || !boilerControls || autotuneTaskRunning || airPurgeTaskRunning || manualFlowTaskRunning || manualHpTaskRunning || hpWaterCalibrationTaskRunning || boilerTaskRunning || autotuneTaskLocked || airPurgeTaskLocked || manualFlowTaskLocked || manualHpTaskLocked || hpWaterCalibrationTaskLocked || boilerPending;
    const boilerAbortDisabled = boilerBusy || !(boilerTaskRunning || boilerTaskLocked || boilerPending);
    const boilerApplyDisabled = boilerBusy || boilerStartDisabled || !boilerResultReady || autotuneTaskRunning || airPurgeTaskRunning || hpWaterCalibrationTaskRunning;
    const autotuneStartDisabled = !cm100Ready || autotuneBusy || !autotuneControls || boilerTaskRunning || airPurgeTaskRunning || manualFlowTaskRunning || manualHpTaskRunning || hpWaterCalibrationTaskRunning || autotuneTaskRunning || boilerTaskLocked || airPurgeTaskLocked || manualFlowTaskLocked || manualHpTaskLocked || hpWaterCalibrationTaskLocked || autotunePending;
    const autotuneAbortDisabled = autotuneBusy || !(autotuneTaskRunning || autotuneTaskLocked || autotunePending);
    const autotuneApplyDisabled = autotuneBusy || autotuneStartDisabled || !autotuneResultReady || boilerTaskRunning || airPurgeTaskRunning || hpWaterCalibrationTaskRunning;
    const airPurgeStartDisabled = !cm100Ready || airPurgeBusy || !airPurgeControls || boilerTaskRunning || autotuneTaskRunning || manualFlowTaskRunning || manualHpTaskRunning || hpWaterCalibrationTaskRunning || airPurgeTaskRunning || boilerTaskLocked || autotuneTaskLocked || manualFlowTaskLocked || manualHpTaskLocked || hpWaterCalibrationTaskLocked || airPurgePending;
    const airPurgeAbortDisabled = airPurgeBusy || !(airPurgeTaskRunning || airPurgeTaskLocked || airPurgePending);
    const manualFlowStartDisabled = !cm100Ready || manualFlowBusy || !manualFlowControls || boilerTaskRunning || autotuneTaskRunning || airPurgeTaskRunning || manualHpTaskRunning || hpWaterCalibrationTaskRunning || manualFlowTaskRunning || boilerTaskLocked || autotuneTaskLocked || airPurgeTaskLocked || manualHpTaskLocked || hpWaterCalibrationTaskLocked || manualFlowPending;
    const manualFlowAbortDisabled = manualFlowBusy || !(manualFlowTaskRunning || manualFlowTaskLocked || manualFlowPending);
    const manualHpStartDisabled = !cm100Ready || manualHpBusy || !manualHpControls || boilerTaskRunning || autotuneTaskRunning || airPurgeTaskRunning || manualFlowTaskRunning || hpWaterCalibrationTaskRunning || manualHpTaskRunning || boilerTaskLocked || autotuneTaskLocked || airPurgeTaskLocked || manualFlowTaskLocked || hpWaterCalibrationTaskLocked || manualHpPending;
    const manualHpAbortDisabled = manualHpBusy || !(manualHpTaskRunning || manualHpTaskLocked || manualHpPending);
    const hpWaterCalibrationStartDisabled = !cm100Ready || hpWaterCalibrationBusy || !hpWaterCalibrationControls || boilerTaskRunning || autotuneTaskRunning || airPurgeTaskRunning || manualFlowTaskRunning || manualHpTaskRunning || hpWaterCalibrationTaskRunning || boilerTaskLocked || autotuneTaskLocked || airPurgeTaskLocked || manualFlowTaskLocked || manualHpTaskLocked || hpWaterCalibrationPending;
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

    const cm100StatusDisplay = cm100WaitingForCm100 ? "Wachten op CM100" : cm100Status;
    const serviceStatusCopy = cm100WaitingForCm100
      ? "Service-stand wordt geopend. Wacht tot CM100 klaar staat."
      : (cm100Ready ? "CM100 is actief en klaar voor service-taken." : "Start de service-stand voordat je een taak uitvoert.");

    const tasks = [
      {
        key: "hp-water-calibration",
        title: "Temperatuursensoren kalibreren",
        label: "Sensor kalibratie",
        summary: "Laat de waterpomp draaien zonder compressor en bepaal offsets voor HP1/HP2 water in/out en de actieve aanvoerbron.",
        status: hpWaterCalibrationStatusDisplay,
        available: Boolean(hpWaterCalibrationControls || state.entities.hpWaterCalibrationStatus),
        openDisabled: !cm100Ready,
        cardMarkup: renderCommissioningTaskCard({
          taskKey: "hp-water-calibration",
          title: "Temperatuursensoren kalibreren",
          copy: "Reken op ongeveer 3 tot 5 minuten. Eerst mengt het water 3 minuten; daarna stopt de meting zodra de sensoren stabiel genoeg zijn.",
          subcopy: "De voorgestelde waarden worden pas actief wanneer je ze toepast. De aanvoer-offset wordt per bron opgeslagen en bij een latere bronwissel automatisch teruggezet; een CIC-URL-wijziging verwijdert hem niet.",
          status: hpWaterCalibrationStatusDisplay,
          statusCopy: hpWaterCalibrationTaskRunning
            ? "De pomp draait en de firmware wacht op een stabiel temperatuurbeeld."
            : (hpWaterCalibrationResultReady ? "Controleer de voorgestelde offsets voordat je ze toepast." : (cm100Ready ? "CM100 staat klaar. Start de meting wanneer compressor en boiler uit zijn." : "Start CM100 eerst.")),
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
        title: "Handmatige flowregeling",
        label: "Handmatige flow",
        summary: "Laat de waterpomp draaien op een tijdelijk flow-setpoint en luister naar het leidingwerk.",
        status: manualFlowStatusDisplay,
        available: Boolean(manualFlowControls || state.entities.manualFlowStatus),
        openDisabled: !cm100Ready,
        cardMarkup: renderCommissioningTaskCard({
          taskKey: "manual-flow",
          title: "Handmatige flowregeling",
          copy: "Gebruik een tijdelijk flow-setpoint om het leidingwerk rustig te controleren. De normale instellingen wijzigen pas wanneer je een waarde bewust overneemt.",
          subcopy: "De bestaande PI-regeling blijft de pomp aansturen.",
          status: manualFlowStatusDisplay,
          statusCopy: manualFlowTaskRunning
            ? "De waterpomp draait. Pas het tijdelijke setpoint aan en controleer de gemeten flow."
            : (cm100Ready ? "CM100 staat klaar. Kies een tijdelijk setpoint en start de waterpomp." : "Start CM100 eerst."),
          progressTask: "",
          controls: `
            <div class="oq-settings-manual-flow-control">
              ${renderSettingsSliderField("manualFlowSetpoint", "Tijdelijke gewenste flow", "Pas deze waarde aan terwijl de waterpomp draait.", "oq-settings-field--compact")}
              ${state.entities.manualFlowStart || state.entities.manualFlowAbort ? renderNamedToggleActionButton({
                active: manualFlowTaskRunning,
                startKey: "manualFlowStart",
                stopKey: "manualFlowAbort",
                startLabel: "Waterpomp starten",
                stopLabel: "Waterpomp stoppen",
                startDisabled: manualFlowBusy || manualFlowStartDisabled,
                stopDisabled: manualFlowBusy || manualFlowAbortDisabled,
              }) : ""}
            </div>
          `,
          metrics: `
            <p class="oq-settings-manual-flow-results-title">Resultaten</p>
            ${renderSettingsStaticField("flowSelected", "Gemeten flow", "Actuele doorstroming in het watercircuit.", getSettingsStatValue("flowSelected"), "oq-settings-field--compact")}
            ${renderSettingsStaticField("manualFlowTargetIpwm", "Actuele pompstand", "Door de PI-regeling aangevraagde pompstand.", getSettingsStatValue("manualFlowTargetIpwm"), "oq-settings-field--compact")}
          `,
        }),
        modalActions: `
          ${state.entities.manualFlowApplyHeating ? renderNamedActionButton("manualFlowApplyHeating", "Overnemen voor verwarmen", "oq-helper-button oq-helper-button--ghost", manualFlowBusy) : ""}
          ${state.entities.manualFlowApplyCooling ? renderNamedActionButton("manualFlowApplyCooling", "Overnemen voor koelen", "oq-helper-button oq-helper-button--ghost", manualFlowBusy) : ""}
        `,
      },
      {
        key: "manual-hp",
        title: "Handmatige warmtepompbediening",
        label: "Handmatige warmtepomp",
        summary: "Selecteer een werkmodus en vraag per warmtepomp een compressorstand aan binnen de bestaande bewaking.",
        status: manualHpStatusDisplay,
        available: Boolean(manualHpControls || state.entities.manualHpStatus),
        openDisabled: !cm100Ready,
        cardMarkup: renderCommissioningTaskCard({
          taskKey: "manual-hp",
          title: "Handmatige warmtepompbediening",
          copy: "Start eerst de service-taak zodat de waterpomp draait. Zodra voldoende flow is gemeten kun je per warmtepomp vanuit Standby naar verwarmen of koelen schakelen en daarna een compressorstand aanvragen.",
          subcopy: "Low-flow, maximale watertemperatuur, minimum draaitijd, minimum uit-tijd en veilige modusovergangen blijven actief. De koelvloer, silent-modus, dag/nacht-cap en normaal uitgesloten compressorstanden worden voor deze handmatige test bewust genegeerd.",
          status: manualHpStatusDisplay,
          statusCopy: manualHpTaskRunning
            ? (manualHpStopping
              ? "De compressorvraag staat op 0. De waterpomp blijft draaien totdat de minimale draaitijd veilig is afgerond."
              : manualHpSafetyStopped
              ? "De bewaking heeft de aangevraagde standen teruggezet naar 0. Controleer de oorzaak voordat je opnieuw opschaalt."
              : "De service-taak is actief. Een veiligheidsstop zet de aangevraagde standen terug naar 0; opnieuw opschalen vereist een bewuste handeling.")
            : (cm100Ready ? "CM100 staat klaar. Start de taak om handmatige warmtepompbediening vrij te geven." : "Start CM100 eerst."),
          progressTask: "",
          actions: `
            ${state.entities.manualHpStart || state.entities.manualHpAbort ? renderNamedToggleActionButton({
              active: manualHpTaskRunning,
              startKey: "manualHpStart",
              stopKey: "manualHpAbort",
              startLabel: "Bediening starten",
              stopLabel: "Bediening stoppen",
              startDisabled: manualHpBusy || manualHpStartDisabled,
              stopDisabled: manualHpBusy || manualHpAbortDisabled,
            }) : ""}
          `,
          controls: `
            <div class="oq-settings-manual-hp-controls">
              <div class="oq-settings-manual-hp-unit">
                ${renderSettingsSelectField("manualHp1Mode", "Warmtepomp 1 werkmodus", "Start in Standby. Verwarmen of koelen kan pas worden gekozen zodra voldoende flow is gemeten.", "oq-settings-field--compact")}
                ${renderSettingsSliderField("manualHp1Level", "Warmtepomp 1 compressorstand", `Aangevraagde fysieke stand F0 tot en met F${hp1ManualMaxLevel}. F11-F20 vereisen zowel Quatt Hybrid version V2 als een bevestigd uitgebreid hardwareprofiel.`, "oq-settings-field--compact", { maxValue: hp1ManualMaxLevel })}
              </div>
              ${hasEntity("hp2ExcludedA") ? `
                <div class="oq-settings-manual-hp-unit">
                  ${renderSettingsSelectField("manualHp2Mode", "Warmtepomp 2 werkmodus", "Start in Standby. Verwarmen of koelen kan pas worden gekozen zodra voldoende flow is gemeten.", "oq-settings-field--compact")}
                  ${renderSettingsSliderField("manualHp2Level", "Warmtepomp 2 compressorstand", `Aangevraagde fysieke stand F0 tot en met F${hp2ManualMaxLevel}. F11-F20 vereisen zowel Quatt Hybrid version V2 als een bevestigd uitgebreid hardwareprofiel.`, "oq-settings-field--compact", { maxValue: hp2ManualMaxLevel })}
                </div>
              ` : ""}
            </div>
          `,
          metrics: `
            <p class="oq-settings-manual-flow-results-title">Resultaten</p>
            <div class="oq-settings-manual-hp-results">
              ${renderSettingsStaticField("flowSelected", "Gemeten flow", "Actuele doorstroming in het watercircuit.", getSettingsStatValue("flowSelected"), "oq-settings-field--compact")}
              ${renderSettingsStaticField("hp1Compressor", "Warmtepomp 1 actueel", "Door de actuator werkelijk toegepaste compressorstand en gemeten compressorfrequentie.", getManualHpActualValue("hp1Compressor", "hp1Freq"), "oq-settings-field--compact")}
              ${hasEntity("hp2Compressor") ? renderSettingsStaticField("hp2Compressor", "Warmtepomp 2 actueel", "Door de actuator werkelijk toegepaste compressorstand en gemeten compressorfrequentie.", getManualHpActualValue("hp2Compressor", "hp2Freq"), "oq-settings-field--compact") : ""}
              ${renderSettingsStaticField("hp1CompressorLevelProfile", "Warmtepomp 1 profiel", "Automatisch gedetecteerd fysiek compressorbereik.", hp1CompressorProfile, "oq-settings-field--compact")}
              ${hasEntity("hp2CompressorLevelProfile") ? renderSettingsStaticField("hp2CompressorLevelProfile", "Warmtepomp 2 profiel", "Automatisch gedetecteerd fysiek compressorbereik.", hp2CompressorProfile, "oq-settings-field--compact") : ""}
            </div>
            ${renderSettingsStaticField("manualHpGuardStatus", "Bewaking", "Toont waarom een handmatig verzoek tijdelijk niet of nog niet volledig wordt toegepast.", getEntityValue("manualHpGuardStatus") || "Vrijgegeven", "oq-settings-field--compact oq-settings-field--full")}
            <div class="oq-settings-manual-hp-statuses">
              ${renderSettingsStaticField("hp1Failures", "Warmtepomp 1 statusmelding", "Actuele melding die de warmtepomp zelf rapporteert.", formatFailures(getEntityStateText("hp1Failures", "None")), "oq-settings-field--compact")}
              ${hasEntity("hp2Failures") ? renderSettingsStaticField("hp2Failures", "Warmtepomp 2 statusmelding", "Actuele melding die de warmtepomp zelf rapporteert.", formatFailures(getEntityStateText("hp2Failures", "None")), "oq-settings-field--compact") : ""}
            </div>
          `,
        }),
      },
      {
        key: "autotune",
        title: "Flow autotune",
        label: "Autotune",
        summary: "Berekent een voorstel voor de flowregeling en kan Kp/Ki daarna toepassen.",
        status: autotuneStatusDisplay,
        available: true,
        openDisabled: isCommissioningTaskStatusWaitingForCm100(autotuneStatusDisplay),
        cardMarkup: renderCommissioningTaskCard({
          taskKey: "autotune",
          title: "Flow autotune",
          copy: "Bereken een voorstel voor de flowregeling en pas dat daarna toe in de installatie-instellingen. Autotune duurt meestal ongeveer 5 tot 10 minuten.",
          subcopy: "Na toepassen worden de flow-instellingen bijgewerkt.",
          status: autotuneStatusDisplay,
          statusCopy: autotuneTaskWaitingForCm100
            ? "Wacht totdat CM100 actief is voordat je autotune start."
            : (autotuneTaskRunning
              ? "Autotune draait op dit moment."
              : (cm100Ready ? "CM100 staat klaar. Start de autotune wanneer je wilt." : "Start CM100 eerst en voer daarna autotune uit.")),
          progressTask: "autotune",
          actions: `
            ${state.entities.flowAutotuneStart || state.entities.flowAutotuneAbort ? renderNamedToggleActionButton({
              active: autotuneTaskRunning,
              startKey: "flowAutotuneStart",
              stopKey: "flowAutotuneAbort",
              startLabel: "Autotune starten",
              stopLabel: "Autotune stoppen",
              startDisabled: autotuneBusy || autotuneStartDisabled,
              stopDisabled: autotuneBusy || autotuneAbortDisabled,
            }) : ""}
            ${state.entities.flowAutotuneApply ? renderNamedActionButton("flowAutotuneApply", "Toepassen", "oq-helper-button oq-helper-button--ghost", autotuneBusy || autotuneApplyDisabled) : ""}
          `,
          metrics: `
            ${renderSettingsStaticField("flowKpSuggested", "Voorgestelde Kp", "Kp bepaalt hoe sterk de regeling meteen corrigeert.", flowKpSuggested, "oq-settings-field--compact")}
            ${renderSettingsStaticField("flowKiSuggested", "Voorgestelde Ki", "Ki corrigeert kleine afwijkingen langzaam weg.", flowKiSuggested, "oq-settings-field--compact")}
          `,
        }),
      },
      {
        key: "boiler",
        title: "Boiler power test",
        label: "Boiler test",
        summary: "Meet het effectieve boilervermogen bij stabiele flow en kan het resultaat toepassen.",
        status: boilerStatusDisplay,
        available: hasBoilerAssist,
        openDisabled: isCommissioningTaskStatusWaitingForCm100(boilerStatusDisplay),
        cardMarkup: renderCommissioningTaskCard({
          taskKey: "boiler",
          title: "Boiler power test",
          copy: "Meet het effectieve boilervermogen bij stabiele flow en schrijf daarna een afgerond voorstel weg naar de boilerinstelling. Boilertest duurt meestal ongeveer 5 tot 10 minuten.",
          subcopy: `Ingesteld boilervermogen: ${escapeHtml(boilerRatedPower)}`,
          status: boilerStatusDisplay,
          statusCopy: boilerTaskWaitingForCm100
            ? "Wacht totdat CM100 actief is voordat je de boiler-test start."
            : (isCommissioningTaskStatusTerminal(boilerStatus) || boilerTaskRunning
              ? getBoilerTestStatusCopy(
                  boilerStatus,
                  getEntityNumericValue("flowSelected"),
                  getEntityNumericValue("flowSetpoint") || 800,
                )
              : (cm100Ready ? "CM100 staat klaar. Start de boiler-test wanneer je wilt." : "Start CM100 eerst en voer daarna de boilervermogentest uit.")),
          progressTask: "boiler",
          actions: `
            ${state.entities.boilerPowerTestStart || state.entities.boilerPowerTestAbort ? renderNamedToggleActionButton({
              active: boilerTaskRunning,
              startKey: "boilerPowerTestStart",
              stopKey: "boilerPowerTestAbort",
              startLabel: "Boiler test starten",
              stopLabel: "Boiler test stoppen",
              startDisabled: boilerBusy || boilerStartDisabled,
              stopDisabled: boilerBusy || boilerAbortDisabled,
            }) : ""}
            ${state.entities.boilerPowerTestApply ? renderNamedActionButton("boilerPowerTestApply", "Toepassen", "oq-helper-button oq-helper-button--ghost", boilerBusy || boilerApplyDisabled) : ""}
          `,
          metrics: `
            ${renderSettingsStaticField("boilerHeatPower", "Actueel vermogen", "Live meting tijdens de boiler-test.", boilerHeatPower)}
            ${renderSettingsStaticField("boilerPowerTestResult", "Gemeten testresultaat", "Afgerond resultaat van de laatste boiler-test.", getSettingsStatValue("boilerPowerTestResult"))}
          `,
        }),
      },
      {
        key: "purge",
        title: "Ontluchten",
        label: "Ontluchten",
        summary: "Draait een vaste ontluchtingsrun van 5 minuten met rustige flow, pomp-pulsen en stabilisatie.",
        status: airPurgeStatusDisplay,
        available: airPurgeAvailable,
        openDisabled: isCommissioningTaskStatusWaitingForCm100(airPurgeStatusDisplay),
        cardMarkup: renderCommissioningTaskCard({
          taskKey: "purge",
          title: "Ontluchten",
          copy: "Draait 5 minuten met rustige doorstroming, korte pomp-pulsen en een stabilisatiefase.",
          subcopy: "Na afloop kan OpenQuatt de service mode (CM100) afsluiten of actief laten.",
          status: airPurgeStatusDisplay,
          statusCopy: airPurgeTaskRunning
            ? "Ontluchten loopt vast 5 minuten door en stopt daarna automatisch."
            : (cm100Ready ? "CM100 staat klaar. Start ontluchten wanneer het circuit open staat." : "Start CM100 eerst en voer daarna ontluchten uit."),
          progressTask: "purge",
          className: "oq-settings-commissioning-card--air-purge",
          actions: `
            ${state.entities.airPurgeStart || state.entities.airPurgeAbort ? renderNamedToggleActionButton({
              active: airPurgeTaskRunning,
              startKey: "airPurgeStart",
              stopKey: "airPurgeAbort",
              startLabel: "Ontluchten starten",
              stopLabel: "Ontluchten stoppen",
              startDisabled: airPurgeBusy || airPurgeStartDisabled,
              stopDisabled: airPurgeBusy || airPurgeAbortDisabled,
            }) : ""}
          `,
          metrics: `
            ${renderSettingsStaticField("airPurgeRemaining", "Resterende tijd", "Ontluchten loopt maximaal 5 minuten.", airPurgeRemaining, "oq-settings-field--compact")}
            ${renderSettingsStaticField("airPurgePhase", "Fase", "Laat zien welk deel van het ontluchten nu actief is.", airPurgePhase, "oq-settings-field--compact")}
            ${renderSettingsStaticField("flowSelected", "Actuele flow", "Gemeten flow tijdens het ontluchten.", getSettingsStatValue("flowSelected"), "oq-settings-field--compact")}
            ${renderSettingsCheckboxSwitchField(
              "airPurgeReturnToAuto",
              "Na afloop",
              "",
              "Service mode (CM100) afsluiten",
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
          ${task.openDisabled ? "Wachten op CM100" : "Openen"}
        </button>`,
    });
  }

  export function getControlModeOverrideLabel(value) {
    const labels = {
      Auto: "Automatische regeling",
      "Force CM0": "CM0 · stand-by",
      "Force CM1": "CM1 · alleen circulatie",
      "Force CM98": "CM98 · vorstcirculatie",
    };
    return labels[String(value || "")] || String(value || "Onbekend");
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
          <p class="oq-helper-label">${active ? "Testmodus actief" : "Tijdelijke testmodus"}</p>
          <h4>${escapeHtml(active ? getControlModeOverrideLabel(currentValue) : "Regelmodus tijdelijk forceren")}</h4>
          <p>${escapeHtml(active
            ? "De normale moduskeuze is overruled. De controller keert uiterlijk 30 minuten na activering automatisch terug naar de normale regeling."
            : "Alleen voor een gerichte test. Een geforceerde modus omzeilt tijdelijk de normale moduskeuze en verloopt automatisch na maximaal 30 minuten.")}</p>
        </div>
        <div class="oq-settings-service-override-actions">
          ${options.map((option) => {
            if (option === "Auto") {
              return active ? `<button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="clear-control-mode-override" ${busy ? "disabled" : ""}>Terug naar automatisch</button>` : "";
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
      ? `${Number.isInteger(runtimeDifference) ? runtimeDifference.toFixed(0) : runtimeDifference.toFixed(1).replace(".", ",")} h verschil`
      : "Verschil onbekend";
    const runtimeDifferenceDetail = hasRuntimeDifference
      ? hp1Hours === hp2Hours
        ? "Beide warmtepompen hebben evenveel gedraaid."
        : `${hp1Hours > hp2Hours ? "HP1" : "HP2"} heeft meer gedraaid.`
      : "De runtimebalans wordt geladen.";
    const runtimeDifferenceClass = !hasRuntimeDifference || hp1Hours === hp2Hours
      ? "is-even"
      : hp1Hours > hp2Hours ? "is-hp1-higher" : "is-hp2-higher";
    const runtimeDifferenceSpan = hasRuntimeDifference && Math.max(Math.abs(hp1Hours), Math.abs(hp2Hours)) > 0
      ? Math.min(28, Math.max(8, (runtimeDifference / Math.max(Math.abs(hp1Hours), Math.abs(hp2Hours))) * 500))
      : 0;
    const runtimeLeadValue = hasEntity("runtimeLeadHp") ? getSettingsTextStatValue("runtimeLeadHp", "") : "";
    const runtimeLead = ["HP1", "HP2"].includes(runtimeLeadValue) ? runtimeLeadValue : "";
    const runtimeLeadMarkup = runtimeLead
      ? `<span class="oq-settings-runtime-lead"><span aria-hidden="true"></span>${escapeHtml(`${runtimeLead} leidend`)}</span>`
      : "";
    const runtimeResetMarkup = runtimeResetKey
      ? `<button class="oq-settings-runtime-reset" type="button" data-oq-action="open-runtime-reset-confirm" aria-label="Draaiurentellers resetten" ${state.busyAction === runtimeResetKey ? "disabled" : ""}>${state.busyAction === runtimeResetKey ? "Resetten…" : "Balans resetten"}</button>`
      : "";
    const runtimeMarkup = hasHp1Runtime || hasHp2Runtime
      ? `
        <div class="oq-settings-runtime-balance${hasHp2Runtime ? "" : " is-single"}">
          <div class="oq-settings-runtime-balance-head">
            <p>Runtimebalans</p>
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
            ` : `<p class="oq-settings-runtime-single-copy">Opgetelde compressorlooptijd.</p>`}
          </div>
        </div>
      `
      : "";

    if (!runtimeMarkup) {
      return "";
    }

    return renderSettingsSection(
      "Onderhoud",
      "Draaiuren",
      "Bekijk de runtimebalans. Begin de interne balans alleen opnieuw na onderhoud.",
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
      "Service",
      "Service & commissioning",
      "Gebruik de service-stand (controlmode CM100) voor testen, afstelling en onderhoudstaken.",
      `
        <div class="oq-settings-service-shell">
          ${renderSettingsControlModeOverridePanel()}
          <div class="oq-settings-service-toolbar">
            <div class="oq-settings-commissioning-teaser-status">
              <span class="oq-settings-commissioning-teaser-status-label">Huidige status</span>
              <strong>${escapeHtml(service.cm100Status)}</strong>
              <p>${escapeHtml(service.serviceStatusCopy)}</p>
            </div>
            <div class="oq-settings-commissioning-hero-actions oq-settings-service-toolbar-actions">
              ${state.entities.commissioningCm100Start ? renderNamedActionButton("commissioningCm100Start", "Service starten", "oq-helper-button oq-helper-button--primary", service.cm100StartDisabled) : ""}
              ${state.entities.commissioningCm100Stop ? renderNamedActionButton("commissioningCm100Stop", "Service stoppen", "oq-helper-button oq-helper-button--ghost", service.cm100StopDisabled) : ""}
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
      label: "ODU EEPROM-export",
      value: "Alleen-lezen diagnose",
      note: "Lees de volledige EEPROM-shadow uit en download deze als JSON voor hardware- en firmwarevergelijking.",
      action: '<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="open-odu-eeprom-dump-modal">Openen</button>',
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
      kicker: "Service",
      title: task.title,
      copy: task.summary,
      className: "oq-helper-modal--wide oq-helper-modal--scrollable oq-helper-modal--service-task",
      sectionAttributes: "data-oq-service-task-scroller",
      closeAction: "close-system-modal",
      closeLabel: `Sluit ${task.title}`,
      body: `<div class="oq-settings-service-task-modal-body">${task.cardMarkup}</div>`,
      actions: `${task.modalActions || ""}<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal">Sluiten</button>`,
    });
  }
