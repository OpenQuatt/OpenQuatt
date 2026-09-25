import { hasEntity, isEntityActive } from "./app-shared.js";
import { getEntityValue, parseLooseNumber } from "./entity-store.js";
import { escapeHtml } from "./html.js";
import { formatNumber, optionLabel, t } from "../i18n/index.js";

// Centrale diagnoseconstanten voor de low-flow blokkade (issue #745).
//
// Bronnen in firmware (niet dupliceren als magic numbers in UI-logica):
// - LOWFLOW_MIN_FLOW_LPH spiegelt `oq_cm_min_flow_lph` in
//   `openquatt/oq_substitutions_common.yaml` (minimumflow voor compressorbedrijf).
// - FLOW_IPWM_MIN / FLOW_IPWM_MAX spiegelen `clamp_ipwm()` in
//   `openquatt/includes/control/oq_flow_control_logic.h` en de regelaarschaal in
//   `openquatt/oq_flow_control.yaml` (lagere iPWM = harder pompen).
export const LOWFLOW_MIN_FLOW_LPH = 250;
export const FLOW_IPWM_MIN = 50;
export const FLOW_IPWM_MAX = 850;
// Band waarbinnen de regelaar duidelijk om meer flow vraagt. Bewust geen
// `output == FLOW_IPWM_MIN`-vergelijking: zo blijft de diagnose werken als de
// regelaar net boven de ondergrens zit.
export const FLOW_IPWM_REQUEST_MARGIN = 100;
// Grens voor "vrijwel geen flow" bij aangestuurde pomp.
export const LOWFLOW_NEAR_ZERO_LPH = 10;

export function isLowFlowFaultActive() {
  return hasEntity("lowflowFaultActive") && isEntityActive("lowflowFaultActive");
}

function getNumericEntityValue(key) {
  if (!hasEntity(key)) {
    return NaN;
  }
  return parseLooseNumber(getEntityValue(key));
}

function getPumpState() {
  const relays = [
    { key: "hp1PumpRelay", label: "HP1" },
    { key: "hp2PumpRelay", label: "HP2" },
  ].filter(({ key }) => hasEntity(key));
  if (!relays.length) {
    return { available: false, running: false, relays: [] };
  }
  const states = relays.map(({ key, label }) => ({ label, running: isEntityActive(key) }));
  return {
    available: true,
    running: states.some((state) => state.running),
    relays: states,
  };
}

function getSetpointLph() {
  const heating = getNumericEntityValue("flowSetpoint");
  const cooling = getNumericEntityValue("coolingFlowSetpoint");
  const modeLabel = String(getEntityValue("controlModeLabel") || "").trim().toLowerCase();
  const coolingMode = /cm5|cooling|koeling/.test(modeLabel)
    || isEntityActive("coolingRequestActive");
  const setpoints = [];
  if (Number.isFinite(heating)) {
    setpoints.push({ kind: "heating", value: heating });
  }
  if (Number.isFinite(cooling)) {
    setpoints.push({ kind: "cooling", value: cooling });
  }
  const ordered = coolingMode
    ? [...setpoints].sort((a, b) => Number(a.kind !== "cooling") - Number(b.kind !== "cooling"))
    : setpoints;
  return {
    value: ordered[0]?.value ?? NaN,
    available: ordered.length > 0,
    setpoints: ordered,
  };
}

function getEffectiveFlowSource() {
  const source = String(getEntityValue("flowSource") || "").trim();
  if (source !== "Outdoor unit" || !hasEntity("qFlowSource")) {
    return source;
  }
  const qSource = String(getEntityValue("qFlowSource") || "").trim();
  const hpGeneration = String(getEntityValue("hpGeneration") || "").trim();
  const topology = String(getEntityValue("installationTopology") || "").trim().toLowerCase();
  if (qSource === "Local" || (qSource === "Auto" && hpGeneration === "V1" && topology !== "duo")) {
    return "Local";
  }
  if (qSource === "Auto") {
    return "Outdoor unit";
  }
  return qSource;
}

export function getLowFlowDiagnosis() {
  const active = isLowFlowFaultActive();
  const flowValue = getNumericEntityValue("flowSelected");
  const flowAvailable = Number.isFinite(flowValue);
  const setpoint = getSetpointLph();
  const outputValue = getNumericEntityValue("flowOutputIpwm");
  const outputAvailable = Number.isFinite(outputValue);
  const flowControlMode = String(getEntityValue("flowControlMode") || "").trim();
  const requestingMore = outputAvailable
    && flowControlMode === "Flow Setpoint"
    && outputValue >= FLOW_IPWM_MIN
    && outputValue <= FLOW_IPWM_MAX
    && outputValue <= FLOW_IPWM_MIN + FLOW_IPWM_REQUEST_MARGIN;
  const pump = getPumpState();
  const flowSource = getEffectiveFlowSource();
  const flowSourceAvailable = flowSource.length > 0;

  let scenario = "inactive";
  if (active) {
    if (!flowAvailable) {
      scenario = "no-measurement";
    } else if (flowValue >= LOWFLOW_MIN_FLOW_LPH) {
      scenario = "recovering";
    } else if (!pump.available) {
      scenario = "pump-unknown";
    } else if (!pump.running) {
      scenario = "pump-off";
    } else if (flowValue <= LOWFLOW_NEAR_ZERO_LPH) {
      scenario = requestingMore ? "no-flow" : "no-flow-unconfirmed";
    } else {
      scenario = "low-flow";
    }
  }

  return {
    active,
    scenario,
    flowLph: flowValue,
    flowAvailable,
    setpointLph: setpoint.value,
    setpointAvailable: setpoint.available,
    setpoints: setpoint.setpoints,
    outputIpwm: outputValue,
    outputAvailable,
    requestingMore,
    pumpAvailable: pump.available,
    pumpRunning: pump.running,
    pumpRelays: pump.relays,
    flowSource,
    flowSourceAvailable,
    minFlowLph: LOWFLOW_MIN_FLOW_LPH,
  };
}

export function renderLowFlowDiagnosis() {
  const diagnosis = getLowFlowDiagnosis();
  if (!diagnosis.active) {
    return "";
  }
  const scenarioCopy = diagnosis.scenario === "pump-off"
    ? t("settingsInstallation.lowflowDiagPumpOff")
    : diagnosis.scenario === "pump-unknown"
      ? t("settingsInstallation.lowflowDiagPumpUnknown")
      : diagnosis.scenario === "no-flow"
        ? t("settingsInstallation.lowflowDiagNoFlow")
        : diagnosis.scenario === "no-flow-unconfirmed"
          ? t("settingsInstallation.lowflowDiagNoFlowUnconfirmed")
          : diagnosis.scenario === "recovering"
            ? t("settingsInstallation.lowflowDiagRecovering")
            : diagnosis.scenario === "no-measurement"
              ? t("settingsInstallation.lowflowDiagNoMeasurement")
              : t("settingsInstallation.lowflowDiagLowFlow");
  const pumpValue = !diagnosis.pumpAvailable
    ? t("settingsInstallation.lowflowDiagPumpStatusUnknown")
    : diagnosis.pumpRelays.map(({ label, running }) => `${label} ${running ? t("settingsInstallation.lowflowDiagPumpOn") : t("settingsInstallation.lowflowDiagPumpOffValue")}`).join(" · ");
  const formatFlow = (value) => `${formatNumber(Math.round(value), { maximumFractionDigits: 0 })} L/h`;
  const rows = [
    [t("settingsInstallation.lowflowDiagFlow"), diagnosis.flowAvailable ? formatFlow(diagnosis.flowLph) : "—"],
    [t("settingsInstallation.lowflowDiagMinimum"), formatFlow(diagnosis.minFlowLph)],
    ...diagnosis.setpoints.map(({ kind, value }) => [
      t(kind === "cooling" ? "settingsInstallation.lowflowDiagCoolingSetpoint" : "settingsInstallation.lowflowDiagHeatingSetpoint"),
      formatFlow(value),
    ]),
    diagnosis.outputAvailable
      ? [t("settingsInstallation.lowflowDiagPumpOutput"), `${formatNumber(Math.round(diagnosis.outputIpwm), { maximumFractionDigits: 0 })} iPWM${diagnosis.requestingMore ? ` · ${t("settingsInstallation.lowflowDiagRequestingMore")}` : ""}`]
      : null,
    [t("settingsInstallation.lowflowDiagPump"), pumpValue],
    [t("settingsInstallation.lowflowDiagSource"), diagnosis.flowSourceAvailable ? optionLabel(diagnosis.flowSource) : t("settingsInstallation.lowflowDiagSourceUnknown")],
  ].filter(Boolean);
  return `
      <div class="oq-settings-monitoring-incident is-active">
        <div class="oq-settings-monitoring-incident-head">
          <div>
            <p>${escapeHtml(t("settingsInstallation.flowLabel"))}</p>
            <strong>${escapeHtml(t("settingsInstallation.lowflowDiagTitle"))}</strong>
          </div>
          <span class="oq-settings-monitoring-badge is-warning">${escapeHtml(t("settingsInstallation.flowFault"))}</span>
        </div>
        <span>${escapeHtml(scenarioCopy)}</span>
        <dl>${rows.map(([label, value]) => (
          `<div><dt>${escapeHtml(label)}</dt><dd>${escapeHtml(value)}</dd></div>`
        )).join("")}</dl>
        <span>${escapeHtml(t("settingsInstallation.lowflowDiagSafety"))}</span>
        <div class="oq-settings-monitoring-incident-action">
          <button
            class="oq-helper-button oq-helper-button--warning"
            type="button"
            data-oq-action="open-service-task-modal"
            data-service-task="manual-flow"
          >${escapeHtml(t("settingsInstallation.lowflowDiagTestAction"))}</button>
          <span>${escapeHtml(t("settingsInstallation.lowflowDiagTestNote"))}</span>
        </div>
        <div class="oq-settings-monitoring-incident-action">
          <button
            class="oq-helper-button oq-helper-button--ghost"
            type="button"
            data-oq-action="select-settings-group"
            data-group-id="installation"
          >${escapeHtml(t("settingsInstallation.lowflowDiagSettingsAction"))}</button>
        </div>
      </div>
    `;
}
