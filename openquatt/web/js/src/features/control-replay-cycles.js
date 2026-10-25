import { isOilReturnDecisionEvent } from "./control-replay-status.js";

const validSequence = (event) => Number.isInteger(event.seq) && event.seq >= 0 && event.seq <= 0xffffffff;
const validUptime = (event) => Number.isFinite(event.uptime_s) && event.uptime_s >= 0;

function cycleKind(event) {
  // Never absorb a latch, manual reset or an unclassified flag into a normal
  // period. Known oil-return records only carry AUTO_CLEARABLE/RAW_VALUE_VALID.
  const flags = event.flags === undefined ? 0 : event.flags;
  if (!Number.isInteger(flags) || flags < 0 || flags > 0xff || (flags & ~0x22) !== 0) return "";
  if (["incident_start", "incident_clear"].includes(event.event_type) && isOilReturnDecisionEvent(event)) return "oilReturn";
  if (["defrost_seen_start", "defrost_seen_clear"].includes(event.event_type)
    && event.reason === "defrost_hold" && Number(event.cm) !== 5 && Number(event._oq_context_cm) !== 5) return "defrost";
  return "";
}

// Pass the complete event stream, including hidden records, and a timestamp in
// milliseconds. Group only observed boundaries; no end means unknown, not live.
export function groupControlReplayCycles(events, timeOf) {
  const cycles = [];
  const open = new Map();
  const records = events.map((event) => ({ event, time: timeOf(event), group: null }));
  // Sequence is causal across clock corrections and controller reboots. Sorting
  // by wall time could put a later clear before its start and invent an open run.
  if (records.every(({ event }) => validSequence(event))) records.sort((a, b) => (a.event.seq - b.event.seq) | 0);
  let previous = null;
  const invalidate = (reason, subject = null) => {
    for (const [key, cycle] of open) {
      if (subject && subject !== "BOTH" && cycle.subject !== subject) continue;
      cycle.incompleteReason = reason;
      open.delete(key);
    }
  };
  for (const record of records) {
    const { event, time } = record;
    if (previous && validSequence(event) && event.seq === previous.event.seq) {
      if (time === previous.time && JSON.stringify(event) === JSON.stringify(previous.event)) {
        if (previous.group) previous.group.events.push(event);
        continue;
      }
      invalidate("sequence_conflict");
      if (previous.group) {
        previous.group.complete = false;
        previous.group.durationS = null;
        previous.group.incompleteReason = "sequence_conflict";
      }
      continue;
    }
    const gap = previous && (!validSequence(event) || !validSequence(previous.event)
      || event.seq !== ((previous.event.seq + 1) >>> 0));
    const reset = previous && validUptime(event) && validUptime(previous.event) && event.uptime_s < previous.event.uptime_s;
    const missingClock = !Number.isFinite(time) || (previous && !Number.isFinite(previous.time));
    const clockChange = previous && !missingClock && (time < previous.time
      || (validUptime(event) && validUptime(previous.event)
        && Math.abs(time - previous.time - (event.uptime_s - previous.event.uptime_s) * 1000) > 1000));
    if (gap || reset || missingClock || clockChange || event.event_type === "boot_marker") {
      invalidate(event.event_type === "boot_marker" || reset ? "reboot" : gap ? "sequence_gap" : missingClock ? "missing_clock" : "clock_change");
    }
    previous = record;
    const subject = String(event.subject || "").toUpperCase();
    const kind = ["HP1", "HP2"].includes(subject) ? cycleKind(event) : "";
    if (kind) {
      const key = `${subject}:${kind}`;
      const started = event.event_type.endsWith("_start");
      if (started) {
        const preceding = open.get(key);
        if (preceding) preceding.incompleteReason = "new_cycle";
        const cycle = {
          kind, subject, startEvent: event, endEvent: null, events: [event],
          start: Number.isFinite(time) ? time : null, end: null, durationS: null,
          complete: false, incompleteReason: Number.isFinite(time) ? "missing_end" : "missing_clock",
        };
        cycles.push(cycle);
        record.group = cycle;
        if (Number.isFinite(time)) open.set(key, cycle);
        else open.delete(key);
      } else {
        const cycle = open.get(key);
        if (cycle && Number.isFinite(time)) {
          cycle.endEvent = event;
          cycle.events.push(event);
          cycle.end = time;
          cycle.durationS = (time - cycle.start) / 1000;
          cycle.complete = true;
          delete cycle.incompleteReason;
          record.group = cycle;
          open.delete(key);
        } else {
          const orphan = {
            kind, subject, startEvent: null, endEvent: event, events: [event],
            start: null, end: Number.isFinite(time) ? time : null, durationS: null,
            complete: false, incompleteReason: "missing_start",
          };
          cycles.push(orphan);
          record.group = orphan;
        }
      }
    } else if (["HP1", "HP2", "BOTH"].includes(subject)) {
      if (["source_start", "hp_start_confirmed", "hp_stop_confirmed"].includes(event.event_type)) {
        invalidate("new_run", subject);
      } else if (["defrost_seen_start", "defrost_seen_clear"].includes(event.event_type)) {
        invalidate("cooling_context", subject);
      } else if (["incident_start", "incident_clear", "incident_acknowledged"].includes(event.event_type)
        || (event.event_type === "hp_availability_change" && ["offline", "faulted", "suspect", "blocked", "preheat"].includes(event.to))) {
        // Real incidents and required actions stay standalone and also prevent
        // this helper from presenting an interrupted cycle as normally finished.
        invalidate("incident", subject);
      }
    }
  }
  return cycles;
}
