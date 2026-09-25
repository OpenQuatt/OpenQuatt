import { getEntityStateText, hasEntity, isEntityActive } from "./app-shared.js";
import { getEntityValue, parseLooseNumber } from "./entity-store.js";
import { formatFailures, formatWarningFailures } from "./failure-format.js";
import { formatNumericState } from "./formatting.js";
import { combineInstallationMonitoringModel } from "./incident-monitoring.js";
import { state } from "./state.js";
import { getLocale, t } from "../i18n/index.js";

export function isInstallationMonitoringBinaryActive(key) {
  return hasEntity(key) && isEntityActive(key);
}

export function isInstallationMonitoringIntegrationEnabled(key) {
  return !hasEntity(key) || isEntityActive(key);
}

export function getInstallationMonitoringFailureText(key) {
  if (!hasEntity(key)) {
    return "";
  }
  return formatFailures(getEntityStateText(key, "None"));
}

export function getInstallationMonitoringWarningFailureText(key) {
  if (!hasEntity(key)) {
    return "";
  }
  return formatWarningFailures(getEntityStateText(key, "None"));
}

export function isInstallationMonitoringFailureActive(key) {
  const normalized = getInstallationMonitoringWarningFailureText(key).trim().toLowerCase();
  return Boolean(normalized) && normalized !== t("failures.none").toLowerCase();
}

export function getInstallationMonitoringModel() {
  const problems = [];
  const incidentMonitoringStale = Boolean(state.incidentMonitoringError);
  const structuredIncidentMonitoringAvailable = Boolean(
    state.incidentMonitoringSnapshot?.valid && !incidentMonitoringStale,
  );
  const cyclingActive = isInstallationMonitoringBinaryActive("compressorCyclingWarning2h")
    || isInstallationMonitoringBinaryActive("compressorCyclingWarning72h")
    || isInstallationMonitoringBinaryActive("alternatingCompressorStartsWarning");
  const cyclingAlertLatched = isInstallationMonitoringBinaryActive("compressorCyclingAlertLatched");
  const cicPollingEnabled = isInstallationMonitoringIntegrationEnabled("cicPollingEnabled");
  const otEnabled = isInstallationMonitoringIntegrationEnabled("otEnabled");
  const isOtThermostatSource = (key) => (
    hasEntity(key) && String(getEntityValue(key) || "").trim() === "OT thermostat"
  );
  const heatingEnableFromOt = isOtThermostatSource("heatingEnableSource");
  const coolingEnableFromOt = isOtThermostatSource("coolingEnableSource");
  const otThermostatStatusInvalid = (heatingEnableFromOt || coolingEnableFromOt)
    && hasEntity("otThermostatStatusValid")
    && !isEntityActive("otThermostatStatusValid");
  const addBinaryProblem = (key, label) => {
    if (isInstallationMonitoringBinaryActive(key)) {
      problems.push({ key, label });
    }
  };
  addBinaryProblem("compressorCyclingWarning2h", t("monitoring.cycling2h"));
  addBinaryProblem("compressorCyclingWarning72h", t("monitoring.cycling72h"));
  addBinaryProblem("alternatingCompressorStartsWarning", t("monitoring.alternating"));
  addBinaryProblem("lowflowFaultActive", t("monitoring.lowflow"));
  addBinaryProblem("pt1000ReadProblem", t("monitoring.pt1000"));
  addBinaryProblem("waterSupplyTempFallbackActive", t("monitoring.supplyFallback"));
  addBinaryProblem("flowMismatch", t("monitoring.flowMismatch"));
  const otbState = String(getEntityValue("otbConnectionState") || "");
  if (isInstallationMonitoringBinaryActive("auxHeatSourcePresent")
      && getEntityValue("boilerConnection") === "OpenTherm"
      && (otbState === "ot_no_response" || otbState === "ot_link_lost")) {
    problems.push({
      key: "otbConnectionState",
      label: otbState === "ot_no_response" ? t("monitoring.otbNoResponse") : t("monitoring.otbLost"),
    });
  }
  const otbBoilerSelected = getEntityValue("boilerConnection") === "OpenTherm";
  const otbBoilerLinkAvailable = otbBoilerSelected && isInstallationMonitoringBinaryActive("otbLinkAvailable");
  if (otbBoilerLinkAvailable && isInstallationMonitoringBinaryActive("otbLowWaterPressure")) {
    // De numerieke druk is alleen aanvullende context: lege of niet-ondersteunde
    // waarden ("" wordt via Number() 0) mogen nooit als 0.0 bar worden getoond.
    const chPressure = hasEntity("otbChPressure") ? parseLooseNumber(getEntityValue("otbChPressure")) : NaN;
    problems.push({
      key: "otbLowWaterPressure",
      label: Number.isFinite(chPressure)
        ? t("monitoring.lowWaterPressureWithValue", { pressure: formatNumericState(chPressure, 1, "bar") })
        : t("monitoring.lowWaterPressure"),
      copy: t("monitoring.lowWaterPressureCopy"),
    });
  }
  if (cicPollingEnabled) {
    addBinaryProblem("cicDataStale", t("monitoring.cicStale"));
  }
  if (otEnabled && isInstallationMonitoringBinaryActive("otLinkProblem")) {
    problems.push({
      key: "otLinkProblem",
      label: t("monitoring.otProblem"),
    });
  } else if (otThermostatStatusInvalid) {
    const label = heatingEnableFromOt && coolingEnableFromOt
      ? t("monitoring.otInvalidBoth")
      : heatingEnableFromOt
        ? t("monitoring.otInvalidHeat")
        : t("monitoring.otInvalidCool");
    problems.push({ key: "otThermostatStatusInvalid", label });
  }
  if (!structuredIncidentMonitoringAvailable && isInstallationMonitoringFailureActive("hp1Failures")) {
    problems.push({ key: "hp1Failures", label: t("monitoring.hpFailurePrefix", { label: t("incidents.hp1Label"), text: getInstallationMonitoringWarningFailureText("hp1Failures") }) });
  }
  if (!structuredIncidentMonitoringAvailable && isInstallationMonitoringFailureActive("hp2Failures")) {
    problems.push({ key: "hp2Failures", label: t("monitoring.hpFailurePrefix", { label: t("incidents.hp2Label"), text: getInstallationMonitoringWarningFailureText("hp2Failures") }) });
  }
  const activeProblemCount = problems.length;
  if (cyclingAlertLatched && !cyclingActive) {
    problems.unshift({
      key: "compressorCyclingAlertLatched",
      label: t("monitoring.cycleLatched"),
    });
  }

  const pluralize = (count) => {
    if (getLocale() === "en") return count === 1 ? "" : "s";
    return count === 1 ? "" : "en";
  };
  const baseModel = {
    problems,
    active: problems.length > 0,
    cyclingAlertLatched,
    cyclingAlertActive: cyclingActive,
    cyclingAlertRecovered: cyclingAlertLatched && !cyclingActive,
    title: activeProblemCount > 0
      ? t("monitoring.titleAttention")
      : cyclingAlertLatched ? t("monitoring.titleLatched") : t("monitoring.titleClear"),
    copy: activeProblemCount > 0
      ? t("monitoring.copyActive", { count: problems.length, plural: pluralize(problems.length) })
      : cyclingAlertLatched
        ? t("monitoring.copyLatched")
        : t("monitoring.copyClear"),
  };
  const combined = structuredIncidentMonitoringAvailable
    ? combineInstallationMonitoringModel(baseModel, state.incidentMonitoringSnapshot)
    : baseModel;
  if (!incidentMonitoringStale) {
    return combined;
  }

  const monitoringProblem = {
    key: "incident-monitoring-stale",
    label: t("monitoring.staleProblem"),
    severity: "attention",
    source: "incident_manager",
  };
  const staleProblems = combined.problems.some((problem) => problem.key === monitoringProblem.key)
    ? combined.problems
    : [monitoringProblem, ...combined.problems];
  return {
    ...combined,
    active: true,
    severity: combined.severity === "fault" ? "fault" : "attention",
    problems: staleProblems,
    title: combined.active ? combined.title : t("monitoring.staleTitle"),
    copy: combined.active
      ? t("monitoring.staleCopyWithActive", { copy: combined.copy })
      : t("monitoring.staleCopy"),
    incidentMonitoringStale: true,
  };
}

export function syncInstallationMonitoringDetailsState(monitoring) {
  const problemSignature = monitoring.active
    ? monitoring.problems.map((problem) => problem.key).sort().join("|")
    : "";
  if (!problemSignature) {
    state.installationMonitoringProblemSignature = "";
    return;
  }
  if (problemSignature !== state.installationMonitoringProblemSignature) {
    state.installationMonitoringProblemSignature = problemSignature;
    state.installationMonitoringDetailsOpen = true;
  }
}
