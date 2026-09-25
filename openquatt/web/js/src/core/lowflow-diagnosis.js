import { getEntityNumericValue, hasEntity, isEntityActive } from "./app-shared.js";
import { getEntityValue } from "./entity-store.js";
import { escapeHtml } from "./html.js";
import { formatNumber, t } from "../i18n/index.js";

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

function getPumpState() {
  const relayKeys = ["hp1PumpRelay", "hp2PumpRelay"].filter((key) => hasEntity(key));
  if (!relayKeys.length) {
    return { available: false, running: false };
  }
  return {
    available: true,
    running: relayKeys.some((key) => isEntityActive(key)),
  };
}

function getSetpointLph() {
  // Verwarmings- en koelsetpoint delen één diagnoseveld; toon de beschikbare
  // configuratie zonder te gokken welke modus actief is.
  for (const key of ["flowSetpoint", "coolingFlowSetpoint"]) {
    if (!hasEntity(key)) continue;
    const value = getEntityNumericValue(key);
    if (Number.isFinite(value)) {
      return { value, available: true };
    }
  }
  return { value: NaN, available: false };
}

export function getLowFlowDiagnosis() {
  const active = isLowFlowFaultActive();
  const flowValue = hasEntity("flowSelected") ? getEntityNumericValue("flowSelected") : NaN;
  const flowAvailable = Number.isFinite(flowValue);
  const setpoint = getSetpointLph();
  const outputValue = hasEntity("flowOutputIpwm") ? getEntityNumericValue("flowOutputIpwm") : NaN;
  const outputAvailable = Number.isFinite(outputValue);
  const requestingMore = outputAvailable
    && outputValue <= FLOW_IPWM_MIN + FLOW_IPWM_REQUEST_MARGIN;
  const pump = getPumpState();
  const flowSourceRaw = hasEntity("flowSource") ? String(getEntityValue("flowSource") || "").trim() : "";
  const flowSourceAvailable = flowSourceRaw.length > 0;

  let scenario = "inactive";
  if (active) {
    if (!flowAvailable) {
      scenario = "no-measurement";
    } else if (pump.available && !pump.running) {
      scenario = "pump-off";
    } else if (flowValue <= LOWFLOW_NEAR_ZERO_LPH) {
      scenario = "no-flow";
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
    outputIpwm: outputValue,
    outputAvailable,
    requestingMore,
    pumpAvailable: pump.available,
    pumpRunning: pump.running,
    flowSource: flowSourceRaw,
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
    : diagnosis.scenario === "no-flow"
      ? t("settingsInstallation.lowflowDiagNoFlow")
      : diagnosis.scenario === "no-measurement"
        ? t("settingsInstallation.lowflowDiagNoMeasurement")
        : t("settingsInstallation.lowflowDiagLowFlow");
  const formatFlow = (value) => `${formatNumber(Math.round(value), { maximumFractionDigits: 0 })} L/h`;
  const rows = [
    [t("settingsInstallation.lowflowDiagFlow"), diagnosis.flowAvailable ? formatFlow(diagnosis.flowLph) : "—"],
    [t("settingsInstallation.lowflowDiagMinimum"), formatFlow(diagnosis.minFlowLph)],
    diagnosis.setpointAvailable
      ? [t("settingsInstallation.lowflowDiagSetpoint"), formatFlow(diagnosis.setpointLph)]
      : null,
    diagnosis.outputAvailable
      ? [t("settingsInstallation.lowflowDiagPumpOutput"), `${formatNumber(Math.round(diagnosis.outputIpwm), { maximumFractionDigits: 0 })} iPWM${diagnosis.requestingMore ? ` · ${t("settingsInstallation.lowflowDiagRequestingMore")}` : ""}`]
      : null,
    [t("settingsInstallation.lowflowDiagPump"), !diagnosis.pumpAvailable
      ? t("settingsInstallation.lowflowDiagPumpUnknown")
      : diagnosis.pumpRunning
        ? t("settingsInstallation.lowflowDiagPumpOn")
        : t("settingsInstallation.lowflowDiagPumpOffValue")],
    [t("settingsInstallation.lowflowDiagSource"), diagnosis.flowSourceAvailable ? diagnosis.flowSource : t("settingsInstallation.lowflowDiagSourceUnknown")],
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
            data-group-id="heating"
          >${escapeHtml(t("settingsInstallation.lowflowDiagSettingsAction"))}</button>
        </div>
      </div>
    `;
}
