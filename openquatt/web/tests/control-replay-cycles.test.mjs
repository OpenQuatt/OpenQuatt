import assert from "node:assert/strict";
import test from "node:test";
import { groupControlReplayCycles } from "../js/src/features/control-replay-cycles.js";

const base = 1792700000000;
const event = (seq, seconds, event_type, subject = "HP1", extra = {}) => ({
  seq, time: base + seconds * 1000, uptime_s: 1000 + seconds,
  event_type, subject, reason: "defrost_hold", severity: "normal", cm: 2, ...extra,
});
const oil = (seq, seconds, edge, subject = "HP1", extra = {}) => event(seq, seconds, `incident_${edge}`, subject,
  { reason: "hp_fault", value_a: 4, flags: 0x22, ...extra });
const defrost = (seq, seconds, edge, subject = "HP1", extra = {}) => event(seq, seconds, `defrost_seen_${edge}`, subject, extra);
const group = (events) => groupControlReplayCycles(events, (item) => item.time);
const assertUncertain = (cycles) => {
  assert.ok(cycles.length > 0);
  for (const cycle of cycles) {
    assert.equal(cycle.complete, false);
    assert.equal(cycle.durationS, null);
    assert.notEqual(cycle.isOpen, true);
    assert.ok(cycle.incompleteReason);
  }
};

test("observed oil-return and defrost boundaries become periods with original event references", () => {
  for (const make of [oil, defrost]) {
    const start = make(1, 0, "start");
    const end = make(2, 45, "clear", "HP1", { duration_s: 999 });
    const input = [start, end];
    const before = structuredClone(input);
    const [cycle] = group(input);
    assert.equal(cycle.kind, make === oil ? "oilReturn" : "defrost");
    assert.equal(cycle.subject, "HP1");
    assert.equal(cycle.complete, true);
    assert.equal(cycle.startEvent, start);
    assert.equal(cycle.endEvent, end);
    assert.equal(cycle.events[0], start);
    assert.equal(cycle.events[1], end);
    assert.equal(cycle.start, base);
    assert.equal(cycle.end, base + 45000);
    assert.equal(cycle.durationS, 45); // Actual boundaries, not an unverified wire duration.
    assert.deepEqual(input, before);
  }
});

test("Duo interleavings and different cycle kinds retain their own boundaries", () => {
  const events = [oil(1, 0, "start"), defrost(2, 2, "start", "HP2"), defrost(3, 4, "start"),
    oil(4, 6, "clear"), defrost(5, 8, "clear", "HP2"), defrost(6, 10, "clear")];
  const cycles = group(events);
  assert.deepEqual(cycles.map(({ kind, subject, durationS }) => [kind, subject, durationS]),
    [["oilReturn", "HP1", 6], ["defrost", "HP2", 6], ["defrost", "HP1", 6]]);
  assert.ok(cycles.every(({ complete }) => complete));
});

test("normal unrelated records preserve continuity without being consumed", () => {
  const between = event(2, 4, "topology_change", "SYSTEM");
  const events = [oil(1, 0, "start"), between, oil(3, 7, "clear")];
  const [cycle] = group(events);
  assert.equal(cycle.complete, true);
  assert.deepEqual(cycle.events, [events[0], events[2]]);
  assert.ok(!cycle.events.includes(between));
});

test("missing boundaries remain unknown even with a duration on the clear record", () => {
  for (const make of [oil, defrost]) {
    const [startOnly] = group([make(1, 0, "start")]);
    assertUncertain([startOnly]);
    assert.equal(startOnly.end, null);
    assert.equal(startOnly.endEvent, null);
    assert.equal(startOnly.incompleteReason, "missing_end");
    const [endOnly] = group([make(1, 70, "clear", "HP1", { duration_s: 70 })]);
    assertUncertain([endOnly]);
    assert.equal(endOnly.start, null);
    assert.equal(endOnly.startEvent, null);
    assert.equal(endOnly.incompleteReason, "missing_start");
  }
});

test("archive gaps, including missing hidden context records, never bridge a cycle", () => {
  for (const make of [oil, defrost]) {
    const cycles = group([make(1, 0, "start"), event(3, 4, "heating_stop_context"), make(4, 8, "clear")]);
    assertUncertain(cycles);
    assert.equal(cycles[0].incompleteReason, "sequence_gap");
    assert.equal(cycles[1].start, null);
    assertUncertain(group([make(1, 0, "start"), make(undefined, 8, "clear")]));
  }
});

test("boot markers and uptime resets close correlation without fabricating an end", () => {
  for (const make of [oil, defrost]) {
    const reboot = [make(1, 0, "start"), event(2, 3, "boot_marker", "SYSTEM", { uptime_s: 0 }),
      make(3, 10, "clear", "HP1", { uptime_s: 7 })];
    assertUncertain(group(reboot));
    assert.equal(group(reboot)[0].incompleteReason, "reboot");
    assertUncertain(group([make(1, 0, "start"), make(2, 10, "clear", "HP1", { uptime_s: 2 })]));
  }
});

test("sequence, rather than wall time, prevents clock corrections from inventing periods", () => {
  for (const make of [oil, defrost]) {
    for (const time of [base - 10000, base + 3600000]) {
      const start = make(1, 0, "start");
      const end = make(2, 10, "clear", "HP1", { time });
      assertUncertain(group([end, start]));
      assert.equal(group([end, start])[0].incompleteReason, "clock_change");
    }
  }
});

test("an unpositioned boundary or intermediate record prevents a complete period", () => {
  for (const time of [NaN, undefined, null, "1792700000000"]) {
    assertUncertain(group([oil(1, 0, "start", "HP1", { time }), oil(2, 10, "clear")]));
    assertUncertain(group([oil(1, 0, "start"), oil(2, 10, "clear", "HP1", { time })]));
    assertUncertain(group([oil(1, 0, "start"), event(2, 5, "topology_change", "SYSTEM", { time }), oil(3, 10, "clear")]));
  }
});

test("identical duplicate records are retained as evidence without creating more periods", () => {
  const start = oil(1, 0, "start");
  const startCopy = structuredClone(start);
  const end = oil(2, 10, "clear");
  const endCopy = structuredClone(end);
  const cycles = group([end, start, startCopy, endCopy]);
  assert.equal(cycles.length, 1);
  assert.equal(cycles[0].complete, true);
  assert.deepEqual(cycles[0].events, [start, startCopy, end, endCopy]);
  assert.equal(cycles[0].events[1], startCopy);
  assert.equal(cycles[0].events[3], endCopy);
});

test("conflicting records with the same sequence number cannot prove an end", () => {
  const cycles = group([oil(1, 0, "start"), oil(1, 1, "start"), oil(2, 10, "clear")]);
  assertUncertain(cycles);
  assert.equal(cycles[0].incompleteReason, "sequence_conflict");
  const unsafeClear = oil(2, 10, "clear", "HP1", { flags: 0x41 });
  const conflictingEnd = group([oil(1, 0, "start"), oil(2, 10, "clear"), unsafeClear]);
  assertUncertain(conflictingEnd);
  assert.ok(conflictingEnd.every(({ events }) => !events.includes(unsafeClear)));
});

test("a new cycle start leaves the earlier attempt incomplete", () => {
  const cycles = group([oil(1, 0, "start"), oil(2, 5, "start"), oil(3, 10, "clear")]);
  assert.equal(cycles.length, 2);
  assert.equal(cycles[0].complete, false);
  assert.equal(cycles[0].incompleteReason, "new_cycle");
  assert.equal(cycles[1].complete, true);
  assert.equal(cycles[1].durationS, 5);
});

test("new compressor runs and confirmed stops prevent stale cycle claims, per pump", () => {
  for (const type of ["source_start", "hp_start_confirmed", "hp_stop_confirmed"]) {
    assertUncertain(group([oil(1, 0, "start"), event(2, 4, type), oil(3, 10, "clear")]));
    assertUncertain(group([oil(1, 0, "start"), event(2, 4, type, "BOTH"), oil(3, 10, "clear")]));
    const [otherPump] = group([oil(1, 0, "start"), event(2, 4, type, "HP2"), oil(3, 10, "clear")]);
    assert.equal(otherPump.complete, true);
  }
});

test("faults, capacity protections, acknowledgements and manual-reset records stay standalone", () => {
  for (const barrier of [
    event(2, 4, "incident_start", "HP1", { reason: "hp_fault", value_a: 5 }),
    oil(2, 4, "acknowledged"),
    event(2, 4, "incident_start", "HP1", { reason: "hp_protection", value_a: 6 }),
    event(2, 4, "incident_clear", "HP1", { reason: "hp_fault", value_a: 21, flags: 0x41 }),
    event(2, 4, "hp_availability_change", "HP1", { to: "offline" }),
  ]) {
    const cycles = group([oil(1, 0, "start"), barrier, oil(3, 10, "clear")]);
    assertUncertain(cycles);
    assert.ok(cycles.every(({ events }) => !events.includes(barrier)));
    assert.deepEqual(group([barrier]), []);
  }
});

test("oil-return latch, action and unknown flags can never disappear into a normal cycle", () => {
  for (const flags of [1, 0x40, 0x41, 0x80, 4, 8, 16, 256, 0x100000000, null, "0", -1]) {
    const start = oil(1, 0, "start", "HP1", { flags });
    const end = oil(2, 5, "clear", "HP1", { flags });
    assert.deepEqual(group([start, end]), []);
    const withUnsafeClear = group([oil(1, 0, "start"), end]);
    assertUncertain(withUnsafeClear);
    assert.ok(withUnsafeClear.every(({ events }) => !events.includes(end)));
  }
});

test("defrost records in cooling mode or unknown subjects/reasons are never promoted", () => {
  for (const extra of [{ cm: 5 }, { _oq_context_cm: 5 }, { reason: "unknown" }]) {
    assert.deepEqual(group([defrost(1, 0, "start", "HP1", extra), defrost(2, 10, "clear", "HP1", extra)]), []);
    assertUncertain(group([defrost(1, 0, "start"), defrost(2, 5, "clear", "HP1", extra), defrost(3, 10, "clear")]));
  }
  for (const subject of ["BOTH", "SYSTEM", "unknown"]) {
    assert.deepEqual(group([oil(1, 0, "start", subject), oil(2, 5, "clear", subject)]), []);
  }
  assert.equal(group([oil(1, 0, "start", "HP2", { cm: 5 }), oil(2, 10, "clear", "HP2", { cm: 5 })])[0].complete, true);
});

test("uint32 sequence wrap and observed same-second boundaries remain valid", () => {
  const [cycle] = group([oil(0xffffffff, 0, "start"), oil(0, 0, "clear")]);
  assert.equal(cycle.complete, true);
  assert.equal(cycle.durationS, 0);
  assert.deepEqual(group([]), []);
});
