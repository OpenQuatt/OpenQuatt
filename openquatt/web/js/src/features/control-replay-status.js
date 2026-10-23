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
