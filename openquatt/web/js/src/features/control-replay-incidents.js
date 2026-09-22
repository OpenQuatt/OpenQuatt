import { getIncidentDisplayLabel } from "../core/incident-monitoring.js";
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
    return result(
      `${label} ${t("controlReplay.clearRecovered")}`,
      latched
        ? t("controlReplay.clearLatched", { subject: resolvedSubject })
        : t("controlReplay.clearStable", { subject: resolvedSubject }),
      { ...meta, label: t("controlReplay.clearLabel") },
    );
  }
  if (type === "incident_acknowledged") {
    return result(
      `${label} ${t("controlReplay.ackConfirmed")}`,
      t("controlReplay.ackSummary", { subject: resolvedSubject }),
      { ...meta, label: t("controlReplay.ackLabel") },
    );
  }
  if (type === "hp_availability_change") {
    const item = resolveAvailability(AVAILABILITY[event.to] || [t("controlReplay.availChanged"), t("controlReplay.availChangedCopy")]);
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
