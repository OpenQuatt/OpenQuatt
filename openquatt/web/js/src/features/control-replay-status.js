// Catalog ID 4 (R2119.b3) is a normal operating status, although older logs
// serialize all ODU entries with the broad hp_fault reason.
export function isOilReturnDecisionEvent(event) {
  return event?.reason === "hp_fault" && Number(event.value_a) === 4
    && ["incident_start", "incident_clear"].includes(event.event_type);
}
