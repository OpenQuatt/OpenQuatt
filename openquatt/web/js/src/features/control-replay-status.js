// Stable IDs from oq_hp_incident_catalog.h. Only explicitly classified statuses
// and protections belong here; unknown bits and synthetic faults stay faults.
const OPERATING_STATUSES = {
  4: "oilReturn",
  6: "highPressureLimit",
  7: "preheat",
  12: "ambientLimit",
  14: "lowPressureLimit",
  46: "pumpDiagnostic",
};

export function getControlReplayOperatingStatus(event) {
  if (!["hp_fault", "hp_protection", "hp_preheat"].includes(event?.reason)
    || !["incident_start", "incident_clear", "incident_acknowledged"].includes(event?.event_type)) return "";
  return OPERATING_STATUSES[Number(event.value_a)] || "";
}

export function isOilReturnDecisionEvent(event) {
  return getControlReplayOperatingStatus(event) === "oilReturn";
}

// The manager emits this pair in one update when the stop starts its restart
// guard. Do not guess from a stop command, a different pump or an archive gap.
export function isControlReplayRestAfterStop(event, previous) {
  return event?.event_type === "hp_availability_change" && event.from === "available" && event.to === "blocked"
    && event.reason === "keep_current" && event.severity === "normal" && event.value_a === 0 && event.value_b === 0
    && ["HP1", "HP2"].includes(event.subject) && previous?.event_type === "hp_stop_confirmed"
    && previous.subject === event.subject && Number.isInteger(event.seq) && Number.isInteger(previous.seq)
    && event.seq === ((previous.seq + 1) >>> 0)
    && Number.isFinite(event.epoch_s) && event.epoch_s > 0 && event.epoch_s === previous.epoch_s
    && Number.isFinite(event.uptime_s) && event.uptime_s >= 0 && event.uptime_s === previous.uptime_s;
}

export function isControlReplayPowerCycleClear(event) {
  // Only the catalogued ODU latch (R2120.b4) requires a power cycle. The wire
  // STOP_UNCONFIRMED flag (0x04) is not MANUAL_RESET_REQUIRED (0x40).
  return event?.event_type === "incident_clear" && event.reason === "hp_fault" && Number(event.value_a) === 21
    && event.to === "blocked" && (Number(event.flags) & 0x41) === 0x41;
}
