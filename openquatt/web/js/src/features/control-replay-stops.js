import { isControlReplayRestAfterStop } from "./control-replay-status.js";

// Only correlate recorded stages. Shared system postflow stays separate whenever
// two stop attempts could own it; incidents always remain independent log rows.
export function groupControlReplayStops(events, timeOf) {
  const attempts = [];
  const pending = new Map();
  const contexts = new Map();
  const postflows = [];
  let context = 0;
  let postflow = null;
  let previous = null;
  // Quarantine every occurrence before grouping: a later conflicting record
  // must also retract earlier feedback with the same sequence number.
  const seen = new Map();
  const conflicts = new Set();
  for (const event of events) {
    const identity = JSON.stringify(event);
    if (seen.has(event.seq) && seen.get(event.seq) !== identity) conflicts.add(event.seq);
    seen.set(event.seq, identity);
  }
  const validSequence = event => Number.isInteger(event.seq) && event.seq >= 0 && event.seq <= 0xffffffff;
  const numbered = events.every(validSequence);
  const sorted = [...events].sort((a, b) => numbered
    ? (a.seq - b.seq) | 0 : timeOf(a) - timeOf(b) || Number(a.seq) - Number(b.seq));
  const nearby = (a, b, limit = 180000) => Number.isFinite(timeOf(a)) && Number.isFinite(timeOf(b))
    && timeOf(b) >= timeOf(a) && timeOf(b) - timeOf(a) <= limit;
  const normal = event => !event.severity || event.severity === "normal";
  const newContext = () => { context++; postflow = null; };
  const pairedMode = (event, entering) => previous?.event_type === "control_mode_change" && normal(previous)
    && Number.isInteger(previous.seq) && event.seq === ((previous.seq + 1) >>> 0)
    && nearby(previous, event, 1000)
    && Number.isFinite(previous.uptime_s) && Number.isFinite(event.uptime_s) && event.uptime_s >= previous.uptime_s && (entering
      ? [2, 3, 5].includes(Number(previous.value_a)) && Number(previous.value_b) === 1 && previous.reason === "flow_postflow"
      : Number(previous.value_a) === 1 && Number(previous.value_b) === 0);
  for (const event of sorted) {
    const type = String(event.event_type || "");
    const hp = String(event.subject || "").toUpperCase();
    const gap = previous && (!validSequence(previous) || !validSequence(event)
      || (event.seq !== previous.seq && event.seq !== ((previous.seq + 1) >>> 0)));
    const conflict = conflicts.has(event.seq);
    const clockChange = previous && Number.isFinite(previous.uptime_s) && Number.isFinite(event.uptime_s)
      && Math.abs(timeOf(event) - timeOf(previous) - (event.uptime_s - previous.uptime_s) * 1000) > 1000;
    const reset = previous && ((Number.isFinite(previous.uptime_s) && Number.isFinite(event.uptime_s)
      && event.uptime_s < previous.uptime_s) || timeOf(event) < timeOf(previous));
    if (gap || conflict || clockChange || reset || !Number.isFinite(timeOf(event)) || (previous && !Number.isFinite(timeOf(previous))) || type === "boot_marker") {
      pending.clear();
      newContext();
    }
    if (conflict) { previous = event; continue; }
    if (type.startsWith("incident_")) {
      const affected = ["HP1", "HP2"].includes(hp) ? [hp] : [...pending.keys()];
      affected.forEach(source => {
        const attempt = pending.get(source);
        if (attempt && source === hp && type === "incident_start" && event.reason === "hp_stop_unconfirmed" && nearby(attempt.command, event)) {
          attempt.failed = true;
          attempt.failure = event;
        }
        pending.delete(source);
      });
      newContext();
    } else if (type === "source_start" || type === "hp_start_confirmed") {
      if (hp === "BOTH") pending.clear();
      else pending.delete(hp);
      newContext();
    } else if (type.startsWith("boiler_") || type.startsWith("frost_protection_")
      || (type === "flow_hold_start" && event.reason !== "flow_postflow")) {
      newContext();
    } else if (type === "source_stop" && ["HP1", "HP2"].includes(hp)) {
      const attempt = { subject: hp, command: event, events: [event], confirmed: false, failed: false };
      attempts.push(attempt);
      contexts.set(attempt, context);
      pending.set(hp, attempt);
    } else if (type === "hp_stop_confirmed") {
      const attempt = pending.get(hp);
      if (attempt && !attempt.confirmed && nearby(attempt.command, event)) {
        attempt.events.push(event);
        attempt.confirmed = true;
      } else pending.delete(hp);
    } else if (type === "hp_availability_change") {
      const attempt = pending.get(hp);
      if (attempt?.confirmed && isControlReplayRestAfterStop(event, previous)) {
        attempt.events.push(event);
        attempt.restartPause = event;
      } else if (attempt?.restartPause && event.from === "blocked" && event.to === "available"
        && event.reason === "keep_current" && normal(event) && event.value_a === 0 && event.value_b === 0
        && nearby(attempt.restartPause, event, 3600000)) {
        attempt.events.push(event);
        pending.delete(hp);
      } else pending.delete(hp);
    } else if (event.reason === "flow_postflow" && normal(event)) {
      if (type === "flow_hold_start") {
        postflow = { start: event, context, events: pairedMode(event, true) ? [previous, event] : [event] };
        postflows.push(postflow);
      } else if (type === "flow_hold_clear" && postflow && nearby(postflow.start, event, 900000)) {
        if (pairedMode(event, false)) postflow.events.push(previous);
        postflow.events.push(event);
        postflow = null;
      }
    }
    previous = event;
  }
  const claims = new Map();
  postflows.forEach(flow => {
    const candidates = attempts.filter(attempt => contexts.get(attempt) === flow.context
      && (nearby(flow.start, attempt.command, 900000) || nearby(attempt.command, flow.start, 900000)));
    if (candidates.length === 1) {
      const attempt = candidates[0];
      claims.set(attempt, [...(claims.get(attempt) || []), flow]);
    }
  });
  claims.forEach((flows, attempt) => {
    if (flows.length === 1) attempt.events.push(...flows[0].events);
  });
  // The actuator writes idle topology immediately before one stop command,
  // optionally separated by that pump's heating snapshot. Do not assign a
  // shared Duo shutdown or a partial/restarted archive to a single pump.
  sorted.forEach((event, index) => {
    if (event.event_type !== "topology_change" || event.subject !== "SYSTEM" || event.to !== "idle"
      || !normal(event) || !Number.isFinite(timeOf(event))) return;
    const candidates = attempts.filter(attempt => timeOf(attempt.command) === timeOf(event));
    if (candidates.length !== 1) return;
    const attempt = candidates[0];
    const commandIndex = sorted.indexOf(attempt.command);
    if (commandIndex <= index) return;
    const chain = sorted.slice(index, commandIndex + 1);
    if (!chain.every((item, offset) => validSequence(item) && !conflicts.has(item.seq) && Number.isFinite(item.uptime_s)
      && item.uptime_s === event.uptime_s && timeOf(item) === timeOf(event)
      && (!offset || item.seq === ((chain[offset - 1].seq + 1) >>> 0)))) return;
    if (chain.slice(1, -1).some(item => item.event_type !== "heating_stop_context"
      || item.subject !== attempt.subject || item.reason !== attempt.command.reason || item.cm !== attempt.command.cm)) return;
    attempt.events.push(event);
  });
  attempts.forEach(attempt => attempt.events.sort((a, b) => sorted.indexOf(a) - sorted.indexOf(b)));
  return attempts;
}
