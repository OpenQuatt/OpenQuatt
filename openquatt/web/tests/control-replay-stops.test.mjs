import assert from "node:assert/strict";
import test from "node:test";
import { groupControlReplayStops } from "../js/src/features/control-replay-stops.js";

const base = 1792710000;
const event = (seq, seconds, event_type, subject = "HP2", extra = {}) => ({
  seq, epoch_s: base + seconds, uptime_s: 1000 + seconds, event_type, subject,
  reason: "keep_current", severity: "normal", ...extra,
});
const group = events => groupControlReplayStops(events, e => e.epoch_s > 0 ? e.epoch_s * 1000 : Number.NaN);
const postflow = (seq, seconds, type, extra = {}) => event(seq, seconds, type, "SYSTEM", { reason: "flow_postflow", ...extra });
const rest = (seq, seconds, subject = "HP2", extra = {}) => event(seq, seconds, "hp_availability_change", subject, {
  from: "available", to: "blocked", value_a: 0, value_b: 0, ...extra,
});
const available = (seq, seconds, subject = "HP2", extra = {}) => event(seq, seconds, "hp_availability_change", subject, {
  from: "blocked", to: "available", value_a: 0, value_b: 0, ...extra,
});
const sequence = () => [
  postflow(1, 0, "control_mode_change", { value_a: 2, value_b: 1 }),
  postflow(2, 0, "flow_hold_start"),
  event(3, 30, "control_mode_change", "SYSTEM", { value_a: 1, value_b: 0 }),
  postflow(4, 30, "flow_hold_clear", { duration_s: 30 }),
  event(5, 302, "source_stop", "HP2", { reason: "ph_comfort_stop" }),
  event(6, 324, "hp_stop_confirmed"), rest(7, 324), available(8, 500),
];

test("postflow may finish before the actual stop command and confirmation; references and order survive", () => {
  const events = Object.freeze(sequence().map(Object.freeze));
  const attempts = group(events);
  assert.equal(attempts.length, 1);
  assert.equal(attempts[0].subject, "HP2");
  assert.equal(attempts[0].command, events[4]);
  assert.equal(attempts[0].confirmed, true);
  assert.equal(attempts[0].failed, false);
  assert.equal(attempts[0].restartPause, events[6]);
  attempts[0].events.forEach((record, index) => assert.equal(record, events[index]));
  assert.deepEqual(group([...events].reverse()), attempts);
});

test("postflow after a stop command also preserves its order before later stop feedback", () => {
  const events = [event(1, 0, "source_stop"), postflow(2, 2, "control_mode_change", { value_a: 5, value_b: 1 }),
    postflow(3, 2, "flow_hold_start"), event(4, 32, "control_mode_change", "SYSTEM", { value_a: 1, value_b: 0 }),
    postflow(5, 32, "flow_hold_clear"), event(6, 40, "hp_stop_confirmed"), rest(7, 40), available(8, 220)];
  assert.deepEqual(group(events)[0].events, events);
});

test("interleaved Duo confirmations stay per pump and ambiguous system postflow stays outside both", () => {
  const events = [event(1, 0, "source_stop", "HP1"), event(2, 1, "source_stop"), postflow(3, 2, "flow_hold_start"),
    event(4, 10, "hp_stop_confirmed"), rest(5, 10), event(6, 12, "hp_stop_confirmed", "HP1"),
    rest(7, 12, "HP1"), postflow(8, 32, "flow_hold_clear")];
  const attempts = group(events);
  assert.deepEqual(attempts.map(a => a.confirmed), [true, true]);
  assert.deepEqual(attempts.map(a => a.events.map(e => e.seq)), [[1, 6, 7], [2, 4, 5]]);
  assert.ok(attempts.every(a => a.events.every(e => e.subject === a.subject)));
});

test("retries are distinct and only the latest command owns later feedback", () => {
  const events = [postflow(1, 0, "flow_hold_start"), event(2, 1, "source_stop"), event(3, 2, "source_stop"), event(4, 12, "hp_stop_confirmed")];
  const attempts = group(events);
  assert.deepEqual(attempts.map(a => a.events.map(e => e.seq)), [[2], [3, 4]]);
  assert.deepEqual(attempts.map(a => a.confirmed), [false, true]);
});

test("failed stops and every incident remain independent severity-bearing records", () => {
  const incident = event(2, 12, "incident_start", "HP2", { reason: "hp_stop_unconfirmed", severity: "fault" });
  const events = [event(1, 0, "source_stop"), incident, event(3, 20, "hp_stop_confirmed"), event(4, 22, "incident_clear", "HP2", { reason: "hp_stop_unconfirmed" })];
  const [attempt] = group(events);
  assert.equal(attempt.failed, true);
  assert.equal(attempt.failure, incident);
  assert.equal(attempt.failure.severity, "fault");
  assert.equal(attempt.confirmed, false);
  assert.deepEqual(attempt.events, [events[0]]);
  for (const type of ["incident_start", "incident_acknowledged", "incident_clear"]) {
    const records = [event(1, 0, "source_stop"), event(2, 2, type, "HP2", { reason: "hp_fault", severity: "fault" }), event(3, 10, "hp_stop_confirmed")];
    assert.deepEqual(group(records)[0].events, [records[0]]);
  }
});

test("unattributed system incidents cannot invent a specific pump's failed stop", () => {
  const records = [event(1, 0, "source_stop", "HP1"), event(2, 1, "source_stop"),
    event(3, 12, "incident_start", "SYSTEM", { reason: "hp_stop_unconfirmed", severity: "fault" }),
    event(4, 20, "hp_stop_confirmed")];
  assert.deepEqual(group(records).map(a => [a.failed, a.confirmed]), [[false, false], [false, false]]);
  assert.deepEqual(group(records).map(a => a.events.map(e => e.seq)), [[1], [2]]);
});

test("a fault on the other pump stays visible without stealing this pump's valid stop feedback", () => {
  const events = [event(1, 0, "source_stop", "HP1"), event(2, 2, "incident_start", "HP2", { reason: "hp_fault", severity: "fault" }), event(3, 10, "hp_stop_confirmed", "HP1")];
  assert.deepEqual(group(events)[0].events, [events[0], events[2]]);
});

test("reboot, gaps, uptime resets and wall-clock resets never bridge stop attempts", () => {
  for (const boundary of [event(2, 2, "boot_marker", "SYSTEM"), event(2, 2, "decision_hold", "SYSTEM", { uptime_s: 0 }),
    event(2, -2, "decision_hold", "SYSTEM"), event(2, 2, "decision_hold", "SYSTEM", { epoch_s: 0 })]) {
    const records = [event(1, 0, "source_stop"), boundary, event(3, 10, "hp_stop_confirmed")];
    assert.equal(group(records)[0].confirmed, false);
    assert.deepEqual(group(records)[0].events, [records[0]]);
  }
  assert.equal(group([event(1, 0, "source_stop"), event(3, 10, "hp_stop_confirmed")])[0].confirmed, false);
});

test("new starts, stale confirmations, and missing commands do not borrow a stop", () => {
  for (const type of ["source_start", "hp_start_confirmed"]) {
    const events = [event(1, 0, "source_stop"), event(2, 2, type), event(3, 10, "hp_stop_confirmed")];
    assert.deepEqual(group(events)[0].events, [events[0]]);
  }
  assert.equal(group([event(1, 0, "source_stop"), event(2, 181, "hp_stop_confirmed")])[0].confirmed, false);
  assert.deepEqual(group([event(1, 0, "hp_stop_confirmed"), rest(2, 0)]), []);
  assert.equal(group([event(1, 0, "source_stop", "BOTH")]).length, 0);
});

test("minimum off-time is only bundled with its exact confirmed-stop pair", () => {
  for (const changed of [{ seq: 4 }, { uptime_s: 0 }, { epoch_s: base + 11 }, { value_a: 22 }, { value_b: 1 },
    { severity: "fault" }, { reason: "hp_fault" }, { from: "recovering" }, { subject: "HP1" }]) {
    const records = [event(1, 0, "source_stop"), event(2, 10, "hp_stop_confirmed"), rest(3, 10, "HP2", changed)];
    assert.ok(!group(records)[0].events.includes(records[2]));
    assert.equal(group(records)[0].restartPause, undefined);
  }
});

test("available is only bundled after a proven normal restart pause, never after a fault or replacement availability", () => {
  const prefix = [event(1, 0, "source_stop"), event(2, 10, "hp_stop_confirmed"), rest(3, 10)];
  for (const barrier of [event(4, 20, "incident_start", "HP2", { reason: "hp_fault", severity: "fault" }),
    available(4, 20, "HP2", { to: "preheat" }), event(4, 20, "source_start"), event(4, 20, "source_stop")]) {
    const final = available(5, 190);
    assert.ok(!group([...prefix, barrier, final])[0].events.includes(final));
  }
  const late = available(4, 3611);
  assert.ok(!group([...prefix, late])[0].events.includes(late));
  const noRest = [prefix[0], prefix[1], available(3, 190)];
  assert.deepEqual(group(noRest)[0].events, noRest.slice(0, 2));
});

test("unpaired postflow ends, non-stop modes and exceptional flow remain separate", () => {
  const records = [event(1, 0, "source_stop"), event(2, 1, "control_mode_change", "SYSTEM", { value_a: 1, value_b: 2 }),
    postflow(3, 1, "flow_hold_clear"), postflow(4, 2, "flow_hold_start", { severity: "limited" }),
    postflow(5, 30, "flow_hold_clear"), event(6, 40, "hp_stop_confirmed")];
  assert.deepEqual(group(records)[0].events, [records[0], records[5]]);
  const unusualMode = [event(1, 0, "source_stop"), postflow(2, 1, "control_mode_change", { value_a: 4, value_b: 1 }),
    postflow(3, 1, "flow_hold_start"), event(4, 10, "hp_stop_confirmed")];
  assert.deepEqual(group(unusualMode)[0].events, [unusualMode[0], unusualMode[2], unusualMode[3]]);
});

test("new flow cycles and thermal starts cannot borrow another cycle's postflow", () => {
  for (const barrier of [event(2, 1, "source_start", "HP1"), event(2, 1, "boiler_assist_start", "BOILER"),
    event(2, 1, "frost_protection_start", "SYSTEM"), event(2, 1, "flow_hold_start", "SYSTEM", { reason: "flow_preflow" }),
    event(2, 1, "incident_start", "HP1", { reason: "hp_fault" })]) {
    const events = [postflow(1, 0, "flow_hold_start"), barrier, event(3, 2, "source_stop")];
    assert.deepEqual(group(events)[0].events, [events[2]]);
  }
  const repeated = [postflow(1, 0, "flow_hold_start"), postflow(2, 30, "flow_hold_clear"), event(3, 31, "source_stop"),
    postflow(4, 32, "flow_hold_start"), postflow(5, 62, "flow_hold_clear")];
  assert.deepEqual(group(repeated)[0].events, [repeated[2]]);
});

test("period-truncated histories expose only supplied stages, and incomplete flow does not invent completion", () => {
  const events = sequence();
  assert.deepEqual(group(events.slice(0, 5))[0].events, events.slice(0, 5));
  assert.equal(group(events.slice(0, 5))[0].confirmed, false);
  assert.deepEqual(group(events.slice(5)), []);
  const incomplete = [postflow(1, 0, "flow_hold_start"), event(2, 1, "source_stop")];
  assert.deepEqual(group(incomplete)[0].events, incomplete);
  assert.deepEqual(group([postflow(1, 0, "flow_hold_start"), event(2, 901, "source_stop")])[0].events.map(e => e.seq), [2]);
});

test("same-second feedback uses sequence order and reset boundaries cannot borrow technical CM", () => {
  const records = [event(3, 0, "hp_stop_confirmed"), event(1, 0, "source_stop"), event(2, 0, "decision_hold", "SYSTEM")];
  assert.deepEqual(group(records)[0].events.map(e => e.seq), [1, 3]);
  const reset = [postflow(1, 0, "control_mode_change", { value_a: 2, value_b: 1 }),
    postflow(2, 0, "flow_hold_start", { uptime_s: 0 }), event(3, 1, "source_stop", "HP2", { uptime_s: 1 })];
  assert.deepEqual(group(reset)[0].events.map(e => e.seq), [2, 3]);
});

test("idle topology belongs to the unique same-tick stop across only that pump's heating snapshot", () => {
  const records = [event(1, 302, "topology_change", "SYSTEM", { from: "single", to: "idle" }),
    ...[2, 3, 4].map((seq, offset) => event(seq, 302, "heating_stop_context", "HP2", { reason: "ph_comfort_stop", cm: 1, flags: offset + 1 })),
    event(5, 302, "source_stop", "HP2", { reason: "ph_comfort_stop", cm: 1 }), event(6, 324, "hp_stop_confirmed")];
  assert.deepEqual(group(records)[0].events, [records[0], records[4], records[5]]);
  assert.equal(group(records)[0].command, records[4]);
  assert.deepEqual(group([...records].reverse())[0].events, [records[0], records[4], records[5]]);
  const direct = [records[0], event(2, 302, "source_stop")];
  assert.deepEqual(group(direct)[0].events, direct);
});

test("idle topology stays separate for ambiguous Duo, unknown clocks, gaps and mismatched context", () => {
  const original = [event(1, 302, "topology_change", "SYSTEM", { to: "idle" }),
    event(2, 302, "heating_stop_context", "HP2"), event(3, 302, "source_stop")];
  const variants = [
    [...original, event(4, 302, "source_stop", "HP1")],
    original.map(e => ({ ...e, epoch_s: 0 })),
    [original[0], { ...original[1], seq: 3 }, { ...original[2], seq: 4 }],
    [original[0], { ...original[1], subject: "HP1" }, original[2]],
    [original[0], { ...original[1], reason: "hp_fault" }, original[2]],
    [original[0], { ...original[1], event_type: "boot_marker" }, original[2]],
    [original[0], { ...original[1], uptime_s: 0 }, original[2]],
    [original[0], original[1], { ...original[2], epoch_s: base + 303 }],
    [{ ...original[0], subject: "BOTH" }, original[1], original[2]],
  ];
  for (const records of variants) assert.ok(group(records).every(attempt => !attempt.events.includes(records[0])));
});

test("conflicting sequences and clock corrections cannot prove a stop sequence", () => {
  const conflict = [event(1, 0, "source_start"), event(1, 0, "source_stop"), event(2, 10, "hp_stop_confirmed")];
  assert.deepEqual(group(conflict), []);
  const clock = [event(1, 0, "source_stop"), event(2, 120, "hp_stop_confirmed", "HP2", { uptime_s: 1010 })];
  assert.equal(group(clock)[0].confirmed, false);
  const wrap = [event(0xffffffff, 0, "source_stop"), event(0, 10, "hp_stop_confirmed")];
  assert.equal(group(wrap)[0].confirmed, true);
});

test("a later conflicting duplicate retracts already observed compressor feedback", () => {
  const records = [event(1, 0, "source_stop"), event(2, 10, "hp_stop_confirmed"), event(2, 10, "source_start")];
  assert.equal(group(records)[0].confirmed, false);
  assert.deepEqual(group(records)[0].events, [records[0]]);
});

test("quarantined idle topology cannot return through the technical-context pass", () => {
  const records = [event(1, 0, "incident_start", "SYSTEM", { reason: "hp_fault", severity: "fault" }),
    event(1, 0, "topology_change", "SYSTEM", { to: "idle" }), event(2, 0, "source_stop"), event(3, 0, "hp_stop_confirmed")];
  assert.deepEqual(group(records)[0].events, records.slice(2));
});
