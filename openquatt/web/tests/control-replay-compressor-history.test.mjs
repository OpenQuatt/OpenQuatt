import assert from "node:assert/strict";
import test from "node:test";
import { getConfirmedCompressorHistory } from "../js/src/features/control-replay-compressor-history.js";

const event = (seq, time, event_type, subject = "HP1", extra = {}) => ({
  seq, epoch_s: time, uptime_s: time, event_type, subject, cm: 2, ...extra,
});
const history = (events, start = 0, end = 100) => getConfirmedCompressorHistory(events, {
  start, end, timeOf: (record) => record.epoch_s,
});
const ranges = (result) => result.intervals.map(({ source, start, end }) => [source, start, end]);

test("compressor intervals follow confirmation, leaving command and topology delays visible", () => {
  const records = [
    event(1, 3, "source_start", "HP2"), event(2, 4, "topology_change", "BOTH", { to: "duo" }),
    event(3, 14, "hp_start_confirmed", "HP2"), event(4, 58, "topology_change", "BOTH", { to: "idle" }),
    event(5, 62, "source_stop", "HP2"), event(6, 84, "hp_stop_confirmed", "HP2"),
  ];
  const result = history(records);
  assert.deepEqual(ranges(result), [["HP2", 14, 84]]);
  assert.equal(result.intervals[0].startEvent, records[2]);
  assert.equal(result.intervals[0].endEvent, records[5]);
  assert.equal(result.intervals[0].isOpen, false);
  assert.equal(result.intervals[0].endUncertain, false);
});

test("legacy commands and a lone stop never manufacture a running interval", () => {
  const records = [event(1, 10, "source_start"), event(2, 20, "topology_change", "BOTH", { to: "duo" }), event(3, 30, "source_stop")];
  assert.deepEqual(history(records), { intervals: [], stateAtStart: { HP1: null, HP2: null }, hasConfirmations: false });
  const stopped = history([event(1, 30, "hp_stop_confirmed")], 40);
  assert.equal(stopped.hasConfirmations, true);
  assert.deepEqual(stopped.intervals, []);
  assert.deepEqual(stopped.stateAtStart, { HP1: false, HP2: null });
});

test("a known running cycle crosses the window boundary and remains open without a stop confirmation", () => {
  const records = [event(1, 10, "hp_start_confirmed", "HP2", { cm: 5 }), event(2, 20, "source_stop", "HP2")];
  const result = history(records, 30, 80);
  assert.deepEqual(ranges(result), [["HP2", 30, 80]]);
  assert.deepEqual(result.stateAtStart, { HP1: null, HP2: true });
  assert.equal(result.intervals[0].isOpen, true);
  assert.equal(result.intervals[0].mode, 5);
  assert.equal(result.intervals[0].endEvent, null);
});

test("Duo compressors start and stop independently, regardless of topology changes", () => {
  const result = history([
    event(1, 10, "hp_start_confirmed"), event(2, 20, "hp_start_confirmed", "HP2"),
    event(3, 25, "topology_change", "HP2", { to: "single" }), event(4, 30, "hp_stop_confirmed"),
    event(5, 50, "hp_stop_confirmed", "HP2"),
  ]);
  assert.deepEqual(ranges(result), [["HP1", 10, 30], ["HP2", 20, 50]]);
});

test("same-second confirmations are ordered by sequence, even with reversed input", () => {
  const records = [event(3, 10, "hp_stop_confirmed"), event(1, 10, "hp_start_confirmed"), event(2, 10, "source_stop")];
  assert.deepEqual(ranges(history(records, 10)), []);
  assert.equal(history(records, 10).stateAtStart.HP1, false);
  const reopened = [event(2, 10, "hp_start_confirmed"), event(1, 10, "hp_stop_confirmed")];
  assert.deepEqual(ranges(history(reopened, 10)), [["HP1", 10, 100]]);
  assert.equal(history(reopened, 10).stateAtStart.HP1, true);
});

test("a reboot or uptime reset cuts certainty at the last record, and needs a new confirmation", () => {
  for (const boundary of [event(3, 30, "boot_marker", "SYSTEM", { uptime_s: 0 }), event(3, 30, "control_mode_change", "SYSTEM", { uptime_s: 0 })]) {
    const records = [event(1, 10, "hp_start_confirmed"), event(2, 20, "decision_hold"), boundary,
      event(4, 40, "hp_start_confirmed", "HP2", { uptime_s: 10 })];
    const result = history(records, 0, 50);
    assert.deepEqual(ranges(result), [["HP1", 10, 20], ["HP2", 40, 50]]);
    assert.deepEqual(history([...records].reverse(), 0, 50), result);
    assert.equal(result.intervals[0].endUncertain, true);
    assert.equal(result.intervals[0].isOpen, false);
    assert.deepEqual(history(records, 25, 50).stateAtStart, { HP1: null, HP2: null });
    assert.deepEqual(history(records, 35, 50).stateAtStart, { HP1: null, HP2: null });
  }
});

test("archive gaps do not bridge an unobserved stop, including a gap across the window boundary", () => {
  const records = [event(1, 10, "hp_start_confirmed"), event(2, 20, "decision_hold"), event(4, 40, "hp_stop_confirmed")];
  const result = history(records);
  assert.deepEqual(ranges(result), [["HP1", 10, 20]]);
  assert.equal(result.intervals[0].endUncertain, true);
  assert.deepEqual(history(records, 30).stateAtStart, { HP1: null, HP2: null });
  assert.deepEqual(history(records, 45).stateAtStart, { HP1: false, HP2: null });
});

test("hidden context records keep a contiguous archive and duplicate starts do not reset runtime", () => {
  const records = [event(1, 10, "hp_start_confirmed"), event(2, 15, "heating_stop_context", "HP1", { _oq_hidden: true }),
    event(3, 20, "hp_start_confirmed"), event(4, 30, "hp_stop_confirmed")];
  const result = history(records);
  assert.deepEqual(ranges(result), [["HP1", 10, 30]]);
  assert.equal(result.intervals[0].startEvent, records[0]);
});

test("unpositionable confirmations and restarts fail closed; an explicit uptime clock works", () => {
  for (const type of ["hp_start_confirmed", "hp_stop_confirmed", "boot_marker"]) {
    const records = [event(1, 10, "hp_start_confirmed"), event(2, Number.NaN, type)];
    assert.deepEqual(history(records).intervals, []);
  }
  const records = [event(1, 10, "hp_start_confirmed", "BOTH", { _oq_context_cm: 5 }), event(2, 20, "hp_stop_confirmed", "BOTH")];
  const result = getConfirmedCompressorHistory(records, { start: 0, end: 100000, timeOf: (record) => record.uptime_s * 1000 });
  assert.deepEqual(ranges(result), [["HP1", 10000, 20000], ["HP2", 10000, 20000]]);
  assert.ok(result.intervals.every((interval) => interval.mode === 5));
  assert.deepEqual(history(records, Number.NaN).intervals, []);
});


test("snapshot bounds stop an open run before browser time and reject invalid windows", () => {
  const records = Object.freeze([Object.freeze(event(1, 10, "hp_start_confirmed")), Object.freeze(event(2, 60, "hp_stop_confirmed"))]);
  const snapshot = history(records, 0, 40);
  assert.deepEqual(ranges(snapshot), [["HP1", 10, 40]]);
  assert.equal(snapshot.intervals[0].isOpen, true);
  assert.deepEqual(history(records, 70, 90).intervals, []);
  assert.deepEqual(history(records, 50, 40).intervals, []);
  assert.deepEqual(history(records, 0, Number.NaN).intervals, []);
});

test("a backward wall-clock correction never turns a later stop into an open run", () => {
  const records = [event(1, 100, "hp_start_confirmed", "HP1", { uptime_s: 10 }),
    event(2, 90, "hp_stop_confirmed", "HP1", { uptime_s: 20 })];
  assert.deepEqual(history(records, 0, 200).intervals, []);
  assert.deepEqual(history([...records].reverse(), 0, 200).intervals, []);
});

test("lost feedback and unconfirmed stops end certainty without pretending to confirm a stop", () => {
  for (const boundary of [
    event(3, 30, "incident_start", "HP1", { reason: "hp_link_loss" }),
    event(3, 30, "incident_start", "HP1", { reason: "hp_stop_unconfirmed" }),
    event(3, 30, "hp_availability_change", "HP1", { to: "offline" }),
  ]) {
    const events = [event(1, 10, "hp_start_confirmed"), event(2, 20, "source_stop"), boundary];
    const result = history(events);
    assert.deepEqual(ranges(result), [["HP1", 10, 30]]);
    assert.equal(result.intervals[0].endUncertain, true);
    assert.equal(result.intervals[0].isOpen, false);
    assert.equal(history(events, 35).stateAtStart.HP1, null);
    events.push(event(4, 40, "incident_clear", "HP1"), event(5, 50, "hp_start_confirmed"));
    assert.deepEqual(ranges(history(events)), [["HP1", 10, 30], ["HP1", 50, 100]]);
  }
});
