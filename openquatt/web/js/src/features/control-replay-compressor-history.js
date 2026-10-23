import { getControlReplayOperatingStatus } from "./control-replay-status.js";

const sources = ["HP1", "HP2"];
const confirmationTypes = new Set(["hp_start_confirmed", "hp_stop_confirmed"]);

// timeOf and the window use the same clock (epoch or uptime-derived milliseconds).
// Pass every record, including hidden context records, to detect archive gaps.
export function getConfirmedCompressorHistory(events, { timeOf, start, end }) {
  const stateAtStart = { HP1: null, HP2: null };
  const intervals = [];
  const hasConfirmations = events.some((event) => confirmationTypes.has(event.event_type));
  const result = { intervals, stateAtStart, hasConfirmations };
  if (!Number.isFinite(start) || !Number.isFinite(end) || end < start) return result;
  const records = events.map((event) => ({ event, time: timeOf(event) }));
  // Event sequence is causal and survives reboot. A wall-clock correction must
  // not reorder a later stop before its start and invent an open running cycle.
  const sequenced = records.filter(({ event }) => Number.isInteger(event.seq))
    .sort((a, b) => (a.event.seq - b.event.seq) | 0);
  if (sequenced.some((record, index) => index > 0 && record.time < sequenced[index - 1].time)) return result;
  // A confirmation or restart without a usable clock cannot be positioned safely.
  if (records.some(({ event, time }) => !Number.isFinite(time)
    && (confirmationTypes.has(event.event_type) || event.event_type === "boot_marker"))) return result;
  const ordered = records.filter(({ time }) => Number.isFinite(time) && time <= end)
    .sort((left, right) => left.time - right.time || (Number(left.event.seq) || 0) - (Number(right.event.seq) || 0));
  const open = { HP1: null, HP2: null };
  let previous = null;
  const close = (source, time, endEvent, endUncertain = false) => {
    const active = open[source];
    if (!active) return;
    const clippedStart = Math.max(start, active.time);
    const clippedEnd = Math.min(end, time);
    if (clippedEnd > clippedStart) intervals.push({
      source, start: clippedStart, end: clippedEnd, startEvent: active.event, endEvent,
      mode: active.event.reason === "cooling_request" ? 5 : active.event.reason === "heating_request" ? 2
        : Number(active.event._oq_context_cm ?? active.event.cm),
      isOpen: !endEvent && !endUncertain, endUncertain,
    });
    open[source] = null;
  };
  const invalidate = (time) => {
    sources.forEach((source) => {
      close(source, previous?.time ?? time, null, true);
      if ((previous?.time ?? time) <= start) stateAtStart[source] = null;
    });
  };
  ordered.forEach(({ event, time }) => {
    const sequenceGap = previous && Number.isInteger(previous.event.seq) && Number.isInteger(event.seq)
      && event.seq !== previous.event.seq && event.seq !== ((previous.event.seq + 1) >>> 0);
    const uptimeReset = previous && Number.isFinite(previous.event.uptime_s) && Number.isFinite(event.uptime_s)
      && event.uptime_s < previous.event.uptime_s;
    if (sequenceGap || uptimeReset || event.event_type === "boot_marker") invalidate(time);
    const targets = event.subject === "BOTH" ? sources : sources.filter((source) => source === event.subject);
    const operatingStatus = getControlReplayOperatingStatus(event);
    // Informational statuses and capacity limits do not stop a compressor.
    // Preheat blocks a start; an inconsistent legacy fault reason stays conservative.
    const keepsRunning = operatingStatus && operatingStatus !== "preheat";
    const uncertain = (event.event_type === "incident_start" && !keepsRunning
      && ["hp_link_loss", "hp_stop_unconfirmed", "hp_start_failed", "hp_fault", "hp_protection"].includes(event.reason))
      || (event.event_type === "hp_availability_change" && ["offline", "suspect", "faulted"].includes(event.to));
    if (uncertain) {
      targets.forEach(source => {
        close(source, time, event, true);
        if (time <= start) stateAtStart[source] = null;
      });
    } else if (confirmationTypes.has(event.event_type)) {
      targets.forEach((source) => {
        const running = event.event_type === "hp_start_confirmed";
        if (running) {
          if (!open[source]) open[source] = { event, time };
        } else close(source, time, event);
        if (time <= start) stateAtStart[source] = running;
      });
    }
    previous = { event, time };
  });
  sources.forEach((source) => close(source, end, null));
  return result;
}
