import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { setLocale } from "../js/src/i18n/index.js";
import { getControlReplayIncidentEventCopy, getControlReplayIncidentDisplaySeverity } from "../js/src/features/control-replay-incidents.js";
import { isControlReplayRestAfterStop } from "../js/src/features/control-replay-status.js";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const replay = await loadReplayHarness(source + "\nexport { enrichControlWorkingDecisionLogEvents, mapDecisionEventToControlWorkingItem, renderControlWorkingDetails };\n");
const now = new Date(2026, 9, 23, 12).getTime();
const stop = { seq: 1, epoch_s: now / 1000 - 300, uptime_s: 1000, event_type: "hp_stop_confirmed", subject: "HP2", reason: "heating_request_cleared", severity: "normal", cm: 0 };
const blocked = { ...stop, seq: 2, event_type: "hp_availability_change", reason: "keep_current", from: "available", to: "blocked", value_a: 0, value_b: 0 };
const map = events => replay.enrichControlWorkingDecisionLogEvents(events)
  .map(e => replay.mapDecisionEventToControlWorkingItem(e, "today", now)).filter(Boolean);
test.beforeEach(() => { setLocale("nl"); state.entities = {}; state.controlReplayWindow = "today"; });
test.afterEach(() => setLocale("nl"));

test("a confirmed stop and its immediate availability edge explain a normal restart pause, NL/EN", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const events = Object.freeze([Object.freeze({ ...stop }), Object.freeze({ ...blocked })]);
    const item = map(events).find(i => i.realEventType === "hp_availability_change");
    assert.equal(item.title, locale === "nl" ? "HP2 wacht op minimale uit-tijd" : "HP2 waits for minimum off-time");
    assert.equal(item.severity, "normal");
    assert.match(item.detail, locale === "nl" ? /korte cycli.*vraag.*startvoorwaarden/ : /short cycling.*demand.*start conditions/);
    assert.equal(item.next, "");
    assert.match(item.summary, locale === "nl" ? /resterende wachttijd niet opgeslagen/ : /remaining wait not recorded/);
    assert.doesNotMatch(replay.renderControlWorkingDetails(item), /technische voorwaarde|Technische statuswijziging|Storing|technical condition|Technical status change|Fault/);
    assert.equal(events[1]._oq_rest_after_stop, undefined);
  }
});

test("missing history, archive gaps and mismatched feedback never invent minimum off-time", () => {
  const pairs = [
    [undefined, blocked], [{ ...stop, subject: "HP1" }, blocked],
    [{ ...stop, event_type: "source_stop" }, blocked], [stop, { ...blocked, seq: 3 }],
    [stop, { ...blocked, epoch_s: blocked.epoch_s + 1 }], [stop, { ...blocked, uptime_s: 0 }],
    [stop, { ...blocked, epoch_s: 0 }], [stop, { ...blocked, value_a: undefined }],
    [stop, { ...blocked, value_b: undefined }], [stop, { ...blocked, from: "recovering" }],
    [stop, { ...blocked, reason: "hp_fault", value_a: 22, value_b: 1 }],
    [stop, { ...blocked, value_b: 1 }], [stop, { ...blocked, severity: "fault" }],
  ];
  for (const [previous, event] of pairs) {
    assert.equal(isControlReplayRestAfterStop(event, previous), false);
    const item = map([previous, event].filter(Boolean)).find(i => i.realEventType === "hp_availability_change");
    if (event.epoch_s === 0) {
      assert.equal(item, undefined);
      continue;
    }
    assert.equal(item.severity, "limited");
    assert.doesNotMatch(item.title, /minimale uit-tijd/);
  }
});

test("an unspecified block admits missing cause and wait instead of using live settings", () => {
  state.entities = { minOffTime: { value: 4 } };
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const item = map([{ ...blocked, _oq_rest_after_stop: true }])[0];
    assert.equal(item.severity, "limited");
    assert.match(item.summary, locale === "nl" ? /oorzaak.*niet.*opgeslagen/ : /cause.*not stored/);
    assert.match(item.detail, locale === "nl" ? /geen specifieke oorzaak of resterende wachttijd/ : /no specific cause or remaining wait/);
    assert.match(item.reasonSummary, locale === "nl" ? /niet vast te stellen/ : /not possible to tell/);
    assert.doesNotMatch(item.title, /minimale uit-tijd|minimum off-time/);
  }
});

test("both pumps keep their own restart pause; boots and interleaved incidents break inference", () => {
  const duo = map([{ ...stop, subject: "HP1" }, { ...blocked, subject: "HP1" },
    { ...stop, seq: 3 }, { ...blocked, seq: 4 }]);
  assert.deepEqual(duo.filter(i => i.realEventType === "hp_availability_change").map(i => [i.source, i.severity]),
    [["HP1", "normal"], ["HP2", "normal"]]);
  for (const boundary of [{ ...stop, seq: 2, event_type: "boot_marker", subject: "SYSTEM" },
    { ...stop, seq: 2, event_type: "incident_start", reason: "hp_fault", value_a: 22, severity: "fault" }]) {
    const item = map([stop, boundary, { ...blocked, seq: 3 }]).find(i => i.realEventType === "hp_availability_change");
    assert.equal(item.severity, "limited");
    assert.doesNotMatch(item.title, /minimale uit-tijd/);
  }
});

test("a blocked availability with a recorded primary incident names that incident", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const copy = getControlReplayIncidentEventCopy({ ...blocked, reason: "hp_fault", value_a: 22, value_b: 1 }, "HP2");
    assert.match(copy.summary, locale === "nl" ? /Condensordruksensor/ : /Condenser pressure sensor/);
    assert.match(copy.detail, locale === "nl" ? /ODU-storing/ : /ODU fault/);
    assert.doesNotMatch(copy.summary, /oorzaak.*niet|cause.*not/);
  }
});

test("a non-blocking primary status beside a restart guard never masquerades as the blocking fault", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const id of [4, 6, 12, 14, 46]) {
      const item = map([stop, { ...blocked, reason: "hp_fault", value_a: id, value_b: 1 }])
        .find(i => i.realEventType === "hp_availability_change");
      assert.equal(item.severity, "limited");
      assert.doesNotMatch(item.title, /minimale uit-tijd|minimum off-time/);
      assert.doesNotMatch(replay.renderControlWorkingDetails(item), /Warmtepompstoring|bevestigde ODU-storing|Heat pump fault|confirmed ODU fault/);
      assert.match(item.detail, locale === "nl" ? /blokkeert op zichzelf geen start.*niet opgeslagen/ : /does not itself block a start.*not stored/);
      assert.match(item.summary, locale === "nl" ? /Gelogde melding/ : /Logged report/);
    }
  }
});

test("stop-unconfirmed and manual-reset flags follow the firmware wire contract", async () => {
  const header = await readFile(new URL("../../../components/openquatt_decision_log/OpenQuattDecisionLog.h", import.meta.url), "utf8");
  const flags = Object.fromEntries([...header.matchAll(/(FLAG_[A-Z_]+) = (0x[0-9A-Fa-f]+)/g)]
    .map(([, key, value]) => [key, Number(value)]));
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const e = { ...blocked, event_type: "incident_clear", reason: "hp_stop_unconfirmed", value_a: 1003,
      flags: flags.FLAG_LATCHED | flags.FLAG_AUTO_CLEARABLE | flags.FLAG_STOP_UNCONFIRMED, from: "active", to: "idle" };
    const copy = getControlReplayIncidentEventCopy(e, "HP2");
    assert.equal(getControlReplayIncidentDisplaySeverity(e), "normal");
    assert.doesNotMatch(copy.summary + copy.title + copy.next, /spanningsonderbreking|power cycle|reset vereist|reset required/);
    const oduLatch = { ...e, reason: "hp_fault", value_a: 21,
      flags: flags.FLAG_LATCHED | flags.FLAG_MANUAL_RESET_REQUIRED | flags.FLAG_RAW_VALUE_VALID, to: "blocked" };
    assert.equal(getControlReplayIncidentDisplaySeverity(oduLatch), "limited");
    assert.match(getControlReplayIncidentEventCopy(oduLatch).summary, locale === "nl" ? /spanningsonderbreking/ : /power cycle/);
  }
});
