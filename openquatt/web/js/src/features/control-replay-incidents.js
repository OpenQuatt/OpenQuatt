import { getIncidentDisplayLabel } from "../core/incident-monitoring.js";
import { getControlReplayOperatingStatus } from "./control-replay-status.js";
import { t } from "../i18n/index.js";

const REASONS = Object.fromEntries([
  ["hp_fault", "controlReplay.reasonHpFault", "controlReplay.reasonHpFaultCopy"],
  ["hp_protection", "controlReplay.reasonHpProtection", "controlReplay.reasonHpProtectionCopy"],
  ["hp_preheat", "controlReplay.reasonHpPreheat", "controlReplay.reasonHpPreheatCopy"],
  ["hp_link_loss", "controlReplay.reasonHpLinkLoss", "controlReplay.reasonHpLinkLossCopy"],
  ["hp_start_failed", "controlReplay.reasonHpStartFailed", "controlReplay.reasonHpStartFailedCopy"],
  ["hp_stop_unconfirmed", "controlReplay.reasonHpStopUnconfirmed", "controlReplay.reasonHpStopUnconfirmedCopy"],
  ["hp_persistence_failure", "controlReplay.reasonHpPersistence", "controlReplay.reasonHpPersistenceCopy"],
  ["hp_recovery_wait", "controlReplay.reasonHpRecoveryWait", "controlReplay.reasonHpRecoveryWaitCopy"],
  ["hp_recovered", "controlReplay.reasonHpRecovered", "controlReplay.reasonHpRecoveredCopy"],
  ["boiler_fallback", "controlReplay.reasonBoilerFallback", "controlReplay.reasonBoilerFallbackCopy"],
  ["fallback_blocked", "controlReplay.reasonFallbackBlocked", "controlReplay.reasonFallbackBlockedCopy"],
  ["heating_request", "controlReplay.reasonHeatingRequest", "controlReplay.reasonHeatingRequestCopy"],
  ["commissioning", "controlReplay.reasonCommissioning", "controlReplay.reasonCommissioningCopy"],
  ["supervisory_override", "controlReplay.reasonOverride", "controlReplay.reasonOverrideCopy"],
].map(([key, labelKey, summaryKey]) => [key, { labelKey, summaryKey, checks: [] }]));
const TYPES = new Set([
  "incident_start", "incident_clear", "incident_acknowledged", "hp_availability_change",
  "control_mode_change", "boiler_fallback_start", "boiler_fallback_stop",
  "hp_start_confirmed", "hp_stop_confirmed",
]);
const SYNTHETIC = {
  hp_link_loss: "hp_link_loss",
  hp_start_failed: "hp_start_failed",
  hp_stop_unconfirmed: "hp_stop_unconfirmed",
  hp_persistence_failure: "hp_manual_reset_persistence_failure",
};
const AVAILABILITY = {
  available: ["controlReplay.availAvailable", "controlReplay.availAvailableCopy"],
  recovering: ["controlReplay.availRecovering", "controlReplay.availRecoveringCopy"],
  faulted: ["controlReplay.availFaulted", "controlReplay.availFaultedCopy"],
  offline: ["controlReplay.availOffline", "controlReplay.availOfflineCopy"],
  preheat: ["controlReplay.availPreheat", "controlReplay.availPreheatCopy"],
  blocked: ["controlReplay.availBlocked", "controlReplay.availBlockedCopy"],
  suspect: ["controlReplay.availSuspect", "controlReplay.availSuspectCopy"],
};
const DEFAULT_META = { labelKey: "controlReplay.defaultLabel", summaryKey: "controlReplay.defaultSummary", checks: [] };
const OPERATING_COPY = {
  oilReturn: ["controlReplay.oilReturnLabel", "controlReplay.oilReturnWhy", "controlReplay.oilReturnNormalCopy"],
  preheat: ["controlReplay.reasonHpPreheat", "controlReplay.preheatWhy", "controlReplay.preheatNormal"],
  highPressureLimit: ["controlReplay.highPressureLimit", "controlReplay.highPressureWhy", "controlReplay.capacityLimitNormal"],
  lowPressureLimit: ["controlReplay.lowPressureLimit", "controlReplay.lowPressureWhy", "controlReplay.capacityLimitNormal"],
  ambientLimit: ["controlReplay.ambientLimit", "controlReplay.ambientLimitWhy", "controlReplay.capacityLimitNormal"],
  pumpDiagnostic: ["controlReplay.pumpDiagnostic", "controlReplay.pumpDiagnosticWhy", "controlReplay.pumpDiagnosticNormal"],
};
const NEXT_KEY = "controlReplay.nextAuto";
const mode = (value) => {
  const number = Number(value);
  return Number.isInteger(number) && number >= 0 && number <= 100 ? number : null;
};
const result = (title, summary, meta, next = null) => ({
  title,
  summary,
  detail: meta.summary,
  next: next ?? t(NEXT_KEY),
  reasonLabel: meta.label,
  checks: meta.checks || [],
});

function resolveMeta(meta) {
  if (!meta) return { label: t("controlReplay.defaultLabel"), summary: t("controlReplay.defaultSummary"), checks: [] };
  return {
    ...meta,
    label: meta.labelKey ? t(meta.labelKey) : meta.label,
    summary: meta.summaryKey ? t(meta.summaryKey) : meta.summary,
  };
}

function resolveAvailability(item) {
  const [labelKey, summaryKey] = item || [];
  return [labelKey ? t(labelKey) : "", summaryKey ? t(summaryKey) : ""];
}

export function getControlReplayIncidentReasonMeta(reasonCode) {
  const entry = REASONS[String(reasonCode || "")] || null;
  return entry ? resolveMeta(entry) : null;
}

export function getControlReplayIncidentEventCopy(event, subject = null) {
  const resolvedSubject = subject ?? t("controlReplay.defaultSubject");
  const type = String(event?.event_type || "");
  if (!TYPES.has(type)) return null;
  const operatingStatus = getControlReplayOperatingStatus(event);
  if (operatingStatus) {
    const started = type === "incident_start";
    const acknowledged = type === "incident_acknowledged";
    const [labelKey, whyKey, normalKey] = OPERATING_COPY[operatingStatus];
    const label = t(labelKey);
    const normal = t(normalKey);
    const oil = operatingStatus === "oilReturn";
    const preheat = operatingStatus === "preheat";
    const diagnostic = operatingStatus === "pumpDiagnostic";
    const title = acknowledged ? t("controlReplay.operatingAcknowledged", { label })
      : oil ? started ? t("controlReplay.oilReturnStarted") : t("controlReplay.oilReturnFinished")
      : preheat ? started ? t("controlReplay.preheatStarted") : t("controlReplay.preheatFinished")
      : diagnostic ? started ? t("controlReplay.pumpDiagnosticStarted") : t("controlReplay.pumpDiagnosticFinished")
      : started ? t("controlReplay.capacityLimitStarted", { label }) : t("controlReplay.capacityLimitFinished", { label });
    let summary = acknowledged ? t("controlReplay.operatingAckCopy", { subject: resolvedSubject })
      : oil ? started ? t("controlReplay.oilReturnStartCopy", { subject: resolvedSubject })
        : t("controlReplay.oilReturnFinishedCopy", { subject: resolvedSubject })
      : preheat ? started ? t("controlReplay.preheatStartCopy", { subject: resolvedSubject })
        : t("controlReplay.preheatFinishedCopy", { subject: resolvedSubject })
      : diagnostic ? started ? t("controlReplay.pumpDiagnosticStartCopy", { subject: resolvedSubject })
        : t("controlReplay.pumpDiagnosticFinishedCopy", { subject: resolvedSubject })
      : started ? t("controlReplay.capacityLimitStartCopy", { subject: resolvedSubject })
        : t("controlReplay.capacityLimitFinishedCopy", { subject: resolvedSubject });
    if (type === "incident_clear" && !oil && !diagnostic && (Number(event.flags) & 1) !== 0) {
      summary += ` ${t("controlReplay.operatingHistoryLatch")}`;
    }
    return {
      ...result(
        title, summary, { label, summary: normal, checks: [] },
        started ? oil ? t("controlReplay.oilReturnNext") : preheat ? t("controlReplay.preheatNext")
          : diagnostic ? t("controlReplay.pumpDiagnosticNext") : t("controlReplay.capacityLimitNext")
          : t("controlReplay.operatingEndedNext"),
      ),
      detail: t(whyKey),
      reasonSummary: normal,
    };
  }
  const meta = getControlReplayIncidentReasonMeta(event.reason) || resolveMeta(DEFAULT_META);
  const label = getIncidentDisplayLabel({ id: event.value_a, key: SYNTHETIC[event.reason] || "" });
  if (type === "incident_start") {
    return result(
      Number(event.value_a) >= 1000 ? label : `${label} ${t("controlReplay.startActiveSuffix")}`,
      t("controlReplay.startSummary", { subject: resolvedSubject }),
      meta,
    );
  }
  if (type === "incident_clear") {
    const latched = (Number(event.flags) & 1) !== 0;
    const resetRequired = latched && (Number(event.flags) & 4) !== 0;
    return result(
      resetRequired ? t("controlReplay.clearResetTitle", { label }) : `${label} ${t("controlReplay.clearRecovered")}`,
      resetRequired ? t("controlReplay.clearResetRequired", { subject: resolvedSubject }) : latched
        ? t("controlReplay.clearLatched", { subject: resolvedSubject })
        : t("controlReplay.clearStable", { subject: resolvedSubject }),
      { ...meta, label: resetRequired ? t("controlReplay.clearResetLabel") : t("controlReplay.clearLabel") },
      resetRequired ? t("controlReplay.clearResetNext") : t("controlReplay.nextAuto"),
    );
  }
  if (type === "incident_acknowledged") {
    return result(
      `${label} ${t("controlReplay.ackConfirmed")}`,
      event.to === "active" ? t("controlReplay.ackActiveSummary", { subject: resolvedSubject })
        : t("controlReplay.ackSummary", { subject: resolvedSubject }),
      { ...meta, label: t("controlReplay.ackLabel") },
    );
  }
  if (type === "hp_availability_change") {
    if (event.to === "available" || event.to === "preheat") {
      const available = event.to === "available";
      return {
        ...result(
          `${resolvedSubject} ${available ? t("controlReplay.availAvailable") : t("controlReplay.availPreheat")}`,
          available ? t("controlReplay.availAvailableCopy") : t("controlReplay.availPreheatCopy"),
          { label: available ? t("controlReplay.availabilityLabel") : t("controlReplay.reasonHpPreheat"), summary: "", checks: [] },
          available ? t("controlReplay.operatingEndedNext") : t("controlReplay.preheatNext"),
        ),
        detail: available ? t("controlReplay.availAvailableWhy") : t("controlReplay.preheatWhy"),
        reasonSummary: available ? t("controlReplay.availAvailableNormal") : t("controlReplay.preheatNormal"),
      };
    }
    const item = resolveAvailability(AVAILABILITY[event.to] || ["controlReplay.availChanged", "controlReplay.availChangedCopy"]);
    return result(`${resolvedSubject} ${item[0]}`, item[1], meta);
  }
  if (type === "control_mode_change") {
    const from = mode(event.value_a);
    const to = mode(event.value_b) ?? mode(event.cm);
    return result(
      from !== null && to !== null ? `CM${from} → CM${to}` : t("controlReplay.modeChanged"),
      to === 4 && from === 3
        ? t("controlReplay.modeRoleFallback")
        : t("controlReplay.modeRoleNew"),
      to === 4 ? { ...meta, label: t("controlReplay.modeRoleLabel") } : meta,
      t("controlReplay.modeOutputNote"),
    );
  }
  const fixed = {
    boiler_fallback_start: [t("controlReplay.fallbackStartTitle"), t("controlReplay.fallbackStartCopy"), t("controlReplay.fallbackStartLabel")],
    boiler_fallback_stop: [t("controlReplay.fallbackStopTitle"), t("controlReplay.fallbackStopCopy"), t("controlReplay.fallbackStopLabel")],
    hp_start_confirmed: [t("controlReplay.hpStartTitle", { subject: resolvedSubject }), t("controlReplay.hpStartCopy"), t("controlReplay.hpStartLabel")],
    hp_stop_confirmed: [t("controlReplay.hpStopTitle", { subject: resolvedSubject }), t("controlReplay.hpStopCopy"), t("controlReplay.hpStopLabel")],
  }[type];
  return result(fixed[0], fixed[1], { ...meta, label: fixed[2] });
}

export function getControlReplayIncidentDisplaySeverity(event) {
  const type = String(event?.event_type || "");
  const operatingStatus = getControlReplayOperatingStatus(event);
  if (operatingStatus) {
    if (["oilReturn", "pumpDiagnostic"].includes(operatingStatus)) return "normal";
    return type === "incident_start" || (type === "incident_acknowledged" && event.to === "active") ? "limited" : "normal";
  }
  if (type === "incident_clear" && (Number(event.flags) & 5) === 5) return "limited";
  if (type === "incident_acknowledged" && event.to === "active") return String(event.severity || "fault");
  if (["incident_clear", "incident_acknowledged", "boiler_fallback_stop", "hp_start_confirmed", "hp_stop_confirmed"].includes(type)) return "normal";
  if (type !== "hp_availability_change") return "";
  if (event.to === "available") return "normal";
  if (["recovering", "preheat", "blocked", "suspect"].includes(event.to)) return "limited";
  return ["faulted", "offline"].includes(event.to) ? "fault" : "";
}

export function getControlReplayIncidentModeTransition(event, previousCm) {
  if (event?.event_type === "control_mode_change") {
    return { from: mode(event.value_a) ?? mode(previousCm), to: mode(event.value_b) ?? mode(event.cm) };
  }
  if (event?.event_type === "boiler_fallback_start") {
    const current = mode(event.cm);
    return {
      from: mode(previousCm) ?? (current === 4 ? null : current),
      to: 4,
    };
  }
  if (event?.event_type === "boiler_fallback_stop") {
    const current = mode(event.cm);
    return { from: 4, to: current === 4 ? null : current };
  }
  return null;
}

export function getControlReplayIncidentModeAfterEvent(event) {
  return getControlReplayIncidentModeTransition(event, null)?.to ?? null;
}
