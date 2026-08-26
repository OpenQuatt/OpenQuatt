import { getOduRuntimeFrequencyButtonHp, getOduRuntimeFrequencyHpKeys, INSTALLATION_MONITORING_STATE_KEYS, ODU_RUNTIME_FREQUENCY_BUTTON_KEYS } from "./config.js";
import { hasEntity } from "./entity-store.js";
import { triggerIncidentAction, triggerNamedButton, triggerNamedButtonGroup } from "./entity-write-actions.js";
import { ODU_CUSTOMER_MODEL_CODE_KEYS, ODU_GENERATION_DETECT_KEYS, ODU_GENERATION_KEYS, ODU_GENERATION_VARIANT_KEYS } from "./odu-generation.js";
import { state } from "./state.js";

const commissioningRefreshGroups = [
  {
    actions: ["commissioningCm100Start", "commissioningCm100Stop"],
    keys: [
      "commissioningStatus", "cm100Active", "boilerPowerTestStatus", "boilerPowerTestActive",
      "flowAutotuneStatus", "airPurgeStatus", "airPurgeActive", "manualFlowStatus",
      "manualFlowActive", "manualHpStatus", "manualHpGuardStatus", "manualHpActive",
      "hpWaterCalibrationStatus", "hpWaterCalibrationActive",
    ],
  },
  {
    actions: ["boilerPowerTestStart", "boilerPowerTestAbort", "boilerPowerTestApply"],
    keys: [
      "commissioningStatus", "boilerPowerTestStatus", "boilerPowerTestActive", "boilerHeatPower",
      "boilerPowerTestResult", "boilerRatedHeatPower", "flowSetpoint",
    ],
  },
  {
    actions: ["flowAutotuneStart", "flowAutotuneAbort", "flowAutotuneApply"],
    keys: ["commissioningStatus", "flowAutotuneStatus", "flowKpSuggested", "flowKiSuggested", "flowKp", "flowKi"],
  },
  {
    actions: ["airPurgeStart", "airPurgeAbort"],
    keys: [
      "commissioningStatus", "airPurgeStatus", "airPurgeActive", "airPurgeRemaining",
      "airPurgePhase", "airPurgeTargetIpwm", "flowMode",
    ],
  },
  {
    actions: ["hpWaterCalibrationStart", "hpWaterCalibrationAbort", "hpWaterCalibrationApply"],
    keys: [
      "commissioningStatus", "hpWaterCalibrationStatus", "hpWaterCalibrationActive",
      "hpWaterCalibrationRemaining", "hpWaterCalibrationPhase", "hpWaterCalibrationSpread",
      "hpWaterCalibrationSupplyDelta", "hpWaterCalibrationStableProgress", "hpWaterCalibrationStableRequired",
      "hpWaterCalibrationResultReference", "hpWaterCalibrationResultSpreadBefore",
      "hpWaterCalibrationResultExpectedSpread", "hpWaterCalibrationResultHp1InRawAvg",
      "hpWaterCalibrationResultHp1OutRawAvg", "hpWaterCalibrationResultHp2InRawAvg",
      "hpWaterCalibrationResultHp2OutRawAvg", "hpWaterCalibrationResultSupplyRawAvg",
      "hpWaterCalibrationResultSupplyOffset", "hpWaterCalibrationResultSupplySource",
      "hp1WaterInRaw", "hp1WaterOutRaw", "hp2WaterInRaw",
      "hp2WaterOutRaw", "hp1WaterIn", "hp1WaterOut", "hp2WaterIn", "hp2WaterOut",
      "hp1WaterInOffset", "hp1WaterOutOffset", "hp2WaterInOffset", "hp2WaterOutOffset",
      "hp1WaterInOffsetSuggested", "hp1WaterOutOffsetSuggested", "hp2WaterInOffsetSuggested",
      "hp2WaterOutOffsetSuggested", "waterSupplyCalibrationOffset",
      "waterSupplyCalibrationOffsetSuggested", "waterSupplyCalibrationRequired",
      "waterSupplyCalibrationStatus", "supplyTemp", "flowMode",
    ],
  },
  {
    actions: ["manualFlowStart", "manualFlowAbort", "manualFlowApplyHeating", "manualFlowApplyCooling"],
    keys: [
      "commissioningStatus", "manualFlowStatus", "manualFlowActive", "manualFlowSetpoint",
      "manualFlowTargetIpwm", "flowSelected", "flowMode", "flowSetpoint", "coolingFlowSetpoint",
    ],
  },
  {
    actions: ["manualHpStart", "manualHpAbort"],
    keys: [
      "commissioningStatus", "manualHpStatus", "manualHpGuardStatus", "manualHpActive",
      "manualHp1Mode", "manualHp2Mode", "manualHp1Level", "manualHp2Level", "flowSelected",
      "hp1Compressor", "hp1Freq", "hp1Failures", "hp2Compressor", "hp2Freq", "hp2Failures",
      "hp1Mode", "hp2Mode",
    ],
  },
];

function prepareCommissioningState(buttonKey) {
  if (buttonKey === "commissioningCm100Start") {
    state.pendingCommissioningCm100Start = true;
    state.commissioningTaskLock = "cm100";
    state.commissioningBoilerHeatPowerDisplay = "";
  } else if (buttonKey === "commissioningCm100Stop") {
    state.pendingCommissioningCm100Start = false;
    state.pendingBoilerPowerTestStart = false;
    state.pendingFlowAutotuneStart = false;
    state.pendingAirPurgeStart = false;
    state.pendingManualFlowStart = false;
    state.pendingManualHpStart = false;
    state.pendingHpWaterCalibrationStart = false;
    state.commissioningTaskLock = "";
    state.commissioningBoilerHeatPowerDisplay = "";
  } else if (buttonKey === "boilerPowerTestStart") {
    state.pendingBoilerPowerTestStart = true;
    state.pendingFlowAutotuneStart = false;
    state.pendingAirPurgeStart = false;
    state.pendingManualFlowStart = false;
    state.pendingManualHpStart = false;
    state.pendingHpWaterCalibrationStart = false;
    state.commissioningTaskLock = "boiler";
    state.commissioningBoilerHeatPowerDisplay = "";
  } else if (buttonKey === "boilerPowerTestAbort" || buttonKey === "boilerPowerTestApply") {
    state.commissioningTaskLock = "boiler";
  } else if (buttonKey === "flowAutotuneStart") {
    state.pendingFlowAutotuneStart = true;
    state.pendingBoilerPowerTestStart = false;
    state.pendingAirPurgeStart = false;
    state.pendingManualFlowStart = false;
    state.pendingManualHpStart = false;
    state.pendingHpWaterCalibrationStart = false;
    state.commissioningTaskLock = "autotune";
  } else if (buttonKey === "flowAutotuneAbort" || buttonKey === "flowAutotuneApply") {
    state.commissioningTaskLock = "autotune";
  } else if (buttonKey === "airPurgeStart") {
    state.pendingAirPurgeStart = true;
    state.pendingBoilerPowerTestStart = false;
    state.pendingFlowAutotuneStart = false;
    state.pendingManualFlowStart = false;
    state.pendingManualHpStart = false;
    state.pendingHpWaterCalibrationStart = false;
    state.commissioningTaskLock = "purge";
  } else if (buttonKey === "airPurgeAbort") {
    state.commissioningTaskLock = "purge";
  } else if (buttonKey === "manualFlowStart") {
    state.pendingManualFlowStart = true;
    state.pendingBoilerPowerTestStart = false;
    state.pendingFlowAutotuneStart = false;
    state.pendingAirPurgeStart = false;
    state.pendingManualHpStart = false;
    state.pendingHpWaterCalibrationStart = false;
    state.commissioningTaskLock = "manual-flow";
  } else if (buttonKey === "manualFlowAbort") {
    state.commissioningTaskLock = "manual-flow";
  } else if (buttonKey === "manualHpStart") {
    state.pendingManualHpStart = true;
    state.pendingBoilerPowerTestStart = false;
    state.pendingFlowAutotuneStart = false;
    state.pendingAirPurgeStart = false;
    state.pendingManualFlowStart = false;
    state.pendingHpWaterCalibrationStart = false;
    state.commissioningTaskLock = "manual-hp";
  } else if (buttonKey === "manualHpAbort") {
    state.commissioningTaskLock = "manual-hp";
  } else if (buttonKey === "hpWaterCalibrationStart") {
    state.pendingHpWaterCalibrationStart = true;
    state.pendingBoilerPowerTestStart = false;
    state.pendingFlowAutotuneStart = false;
    state.pendingAirPurgeStart = false;
    state.pendingManualFlowStart = false;
    state.pendingManualHpStart = false;
    state.commissioningTaskLock = "hp-water-calibration";
  } else if (buttonKey === "hpWaterCalibrationAbort" || buttonKey === "hpWaterCalibrationApply") {
    state.commissioningTaskLock = "hp-water-calibration";
  }
}

function getRefreshOptions(buttonKey) {
  if (buttonKey === "acknowledgeCompressorCyclingAlert") {
    return { refreshKeys: [...INSTALLATION_MONITORING_STATE_KEYS] };
  }
  if (buttonKey === "acknowledgeHpIncidents") {
    return { refreshIncidentMonitoring: true };
  }

  const generationDetectIndex = ODU_GENERATION_DETECT_KEYS.indexOf(buttonKey);
  if (generationDetectIndex !== -1) {
    const hpIndex = generationDetectIndex + 1;
    return {
      refreshKeys: [ODU_GENERATION_KEYS[generationDetectIndex]],
      refreshDelayMs: 1800,
      successNotice: `HP${hpIndex} ODU-detectie opnieuw aangevraagd.`,
      errorPrefix: `ODU-detectie mislukt voor HP${hpIndex}`,
    };
  }

  const group = commissioningRefreshGroups.find(({ actions }) => actions.includes(buttonKey));
  if (group) {
    return { refreshKeys: [...group.keys] };
  }

  if (ODU_RUNTIME_FREQUENCY_BUTTON_KEYS.has(buttonKey)) {
    const hpIndex = getOduRuntimeFrequencyButtonHp(buttonKey);
    if (hpIndex) {
      const isLoad = buttonKey.endsWith("Load");
      return {
        refreshKeys: getOduRuntimeFrequencyHpKeys(hpIndex),
        refreshDelayMs: isLoad ? 1200 : 3200,
        successNotice: isLoad
          ? `HP${hpIndex} ODU runtime tabel lezen aangevraagd.`
          : `HP${hpIndex} ODU runtime write aangevraagd; controleer status/readback.`,
        errorPrefix: `ODU runtime actie mislukt voor HP${hpIndex}`,
      };
    }
  }

  return {};
}

  export function handleNamedButtonAction(action, button) {
  if (action === "retry-hp-start" || action === "confirm-hp-power-cycle") {
    const hpIndex = Number(button.dataset.oqHpIndex || 0);
    if (hpIndex !== 1 && hpIndex !== 2) return true;
    const kind = action === "retry-hp-start"
      ? "start_failure_retry"
      : "confirm_odu_power_cycle";
    if (kind === "confirm_odu_power_cycle") {
      const confirmed = window.confirm(
        `HP${hpIndex} ODU-powercycle bevestigen?\n\nBevestig alleen als deze buitenunit werkelijk spanningsloos is geweest. Hiermee geef je uitsluitend de herstelde safety-latch van HP${hpIndex} vrij; een actieve fout blijft staan.`,
      );
      if (!confirmed) return true;
    }
    void triggerIncidentAction(hpIndex, kind);
    return true;
  }
  if (action === "press-odu-generation-detect-all") {
    const detectKeys = ODU_GENERATION_DETECT_KEYS.filter((key) => hasEntity(key));
    if (detectKeys.length === 0) return true;
    const detectIndexes = detectKeys.map((key) => ODU_GENERATION_DETECT_KEYS.indexOf(key));
    const refreshKeys = detectIndexes.flatMap((index) => [
      ODU_GENERATION_KEYS[index],
      ODU_GENERATION_VARIANT_KEYS[index],
      ODU_CUSTOMER_MODEL_CODE_KEYS[index],
    ]);
    void triggerNamedButtonGroup(detectKeys, {
      busyAction: "odu-generation-detect-all",
      refreshKeys,
      refreshDelayMs: 3200,
      successNotice: "ODU-detectie opnieuw aangevraagd.",
      errorPrefix: "ODU-detectie niet volledig uitgevoerd",
    });
    return true;
  }
  if (action !== "press-named-button") {
    return false;
  }

  const buttonKey = String(button.dataset.oqButtonKey || button.dataset.buttonKey || button.getAttribute("data-oq-button-key") || "").trim();
  if (buttonKey) {
    prepareCommissioningState(buttonKey);
    void triggerNamedButton(buttonKey, getRefreshOptions(buttonKey));
  }
  return true;
}
