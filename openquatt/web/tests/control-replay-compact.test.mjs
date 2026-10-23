import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { setLocale } from "../js/src/i18n/index.js";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const replay = await loadReplayHarness(source + "\nexport { buildControlWorkingTimelineItems, enrichControlWorkingDecisionLogEvents, mapDecisionEventToControlWorkingItem, renderControlWorkingDetails, renderControlWorkingTimelineItem, getControlWorkingSelectedItem };\n");
const now = new Date(2026, 9, 23, 12).getTime();
const base = new Date(2026, 9, 23, 1).getTime() / 1000;
const event = (seq, second, type, subject = "SYSTEM", extra = {}) => ({
  seq, epoch_s: base + second, uptime_s: 1000 + second, event_type: type, subject,
  reason: "keep_current", cm: 2, severity: "normal", flags: 0, ...extra,
});
const map = events => {
  state.decisionLog = { ok: true, meta: {}, events };
  return replay.enrichControlWorkingDecisionLogEvents(events).map(e => replay.mapDecisionEventToControlWorkingItem(e, "today", now)).filter(Boolean);
};
test.beforeEach(() => {
  setLocale("nl");
  state.entities = {};
  state.controlReplayWindow = "today";
  state.decisionLogLastFetchAt = now;
});
test.afterEach(() => setLocale("nl"));
const stopEvents = () => [
  event(1, 478, "control_mode_change", "SYSTEM", { reason: "flow_postflow", value_a: 2, value_b: 1 }),
  event(2, 478, "flow_hold_start", "SYSTEM", { reason: "flow_postflow" }),
  event(3, 782, "source_stop", "HP2", { reason: "ph_comfort_hold" }),
  event(4, 788, "control_mode_change", "SYSTEM", { value_a: 1, value_b: 0 }),
  event(5, 788, "flow_hold_clear", "SYSTEM", { reason: "flow_postflow", duration_s: 310 }),
  event(6, 804, "hp_stop_confirmed", "HP2"),
  event(7, 804, "hp_availability_change", "HP2", { from: "available", to: "blocked", value_a: 0, value_b: 0 }),
  event(8, 1044, "hp_availability_change", "HP2", { from: "blocked", to: "available", value_a: 0, value_b: 0 }),
];
test("real stop ordering becomes one selectable sequence with CM only in technical details, NL/EN", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const raw = map(stopEvents());
    const items = replay.buildControlWorkingTimelineItems(raw);
    assert.equal(items.length, 1);
    const item = items[0];
    assert.equal(item.periodSteps.length, 8);
    assert.equal(item.periodSteps[0].realEventType, "control_mode_change");
    const row = replay.renderControlWorkingTimelineItem(item, null);
    assert.doesNotMatch(row, /CM\d|Technische status|Technical status/);
    const html = replay.renderControlWorkingDetails(item);
    const [visible, support] = html.split("<details");
    assert.ok(visible.indexOf("01:07:58") < visible.indexOf("01:13:02"));
    assert.ok(visible.indexOf("01:13:08") < visible.indexOf("01:13:24"));
    assert.match(support, /CM2 → CM1/);
    assert.doesNotMatch(visible, /Is dit normaal|Is this normal|Waarom\?|Why\?/);
    state.controlReplaySelectedEpisode = raw[5].id;
    assert.equal(replay.getControlWorkingSelectedItem(items).id, item.id);
  }
});
test("normal cycles show one period, physical explanation and observed times without all-clear", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const items = replay.buildControlWorkingTimelineItems(map([
      event(1, 16595, "incident_start", "HP2", { reason: "hp_fault", value_a: 4 }),
      event(2, 16765, "incident_clear", "HP2", { reason: "hp_fault", value_a: 4, duration_s: 170 }),
    ]));
    assert.equal(items.length, 1);
    assert.equal(items[0].cycle, "oilReturn");
    assert.match(items[0].time, /05:36:35.*05:39:25/);
    assert.equal(items[0].duration, "3 min");
    const html = replay.renderControlWorkingDetails(items[0]);
    assert.match(html, locale === "nl" ? /smeerolie.*compressor/ : /oil.*compressor/);
    assert.doesNotMatch(html.split("<details")[0], /hersteld|recovered|storing|fault|Is dit normaal|Is this normal/);
  }
});
test("a period boundary cannot import a hidden successful end or confirmation", () => {
  for (const events of [stopEvents(), [
    event(1, 100, "incident_start", "HP1", { reason: "hp_fault", value_a: 4 }),
    event(2, 200, "incident_clear", "HP1", { reason: "hp_fault", value_a: 4 }),
  ]]) {
    const raw = map(events);
    const visible = raw.filter(i => !["hp_stop_confirmed", "incident_clear"].includes(i.realEventType));
    const grouped = replay.buildControlWorkingTimelineItems(visible).find(i => i.periodSteps);
    assert.match(grouped.summary, /niet bevestigd|ontbreekt/);
    assert.equal(grouped.duration, "");
  }
});
test("faults, failed stops and reset actions remain separate severity-bearing rows", () => {
  const events = [event(1, 10, "source_stop", "HP2"),
    event(2, 20, "incident_start", "HP2", { reason: "hp_stop_unconfirmed", value_a: 1003, severity: "fault" }),
    event(3, 25, "incident_clear", "HP1", { reason: "hp_fault", value_a: 21, flags: 0x41, to: "blocked" })];
  const items = replay.buildControlWorkingTimelineItems(map(events));
  assert.equal(items.length, 3);
  assert.equal(items.find(i => i.periodSteps).severity, "fault");
  assert.ok(items.some(i => i.realEventType === "incident_start" && i.severity === "fault"));
  const reset = items.find(i => i.realEventType === "incident_clear");
  assert.equal(reset.severity, "limited");
  assert.match(replay.renderControlWorkingDetails(reset), /spanningsonderbreking/);
});
test("explicit restart cause uses recorded remaining seconds despite missing history and current settings", () => {
  state.entities = { minOffTime: { value: 99 } };
  const item = map([event(1, 100, "hp_availability_change", "HP2", {
    reason: "min_rest_active", from: "available", to: "blocked", threshold_a: 240, value_a: 4, value_b: 1,
  })])[0];
  assert.equal(item.severity, "normal");
  assert.match(item.title, /minimale uit-tijd/);
  assert.match(replay.renderControlWorkingDetails(item), /240 s/);
  assert.doesNotMatch(item.summary + item.detail, /zojuist|geen.*incident/);
});
test("unattributed decisions admit missing cause and routine detail does not repeat normality", () => {
  const item = map([event(1, 100, "unknown_record")])[0];
  assert.match(item.summary, /oorzaak.*niet.*opgeslagen/);
  const html = replay.renderControlWorkingDetails(item).split("<details")[0];
  assert.doesNotMatch(html, /Is dit normaal|Wat gebeurt daarna|keuze vastgelegd/);
});

test("unclocked archives cannot become complete current-boot periods", () => {
  const events = [event(1, 10, "incident_start", "HP1", { reason: "hp_fault", value_a: 4, epoch_s: 0 }),
    event(2, 20, "incident_clear", "HP1", { reason: "hp_fault", value_a: 4, epoch_s: 0 })];
  map(events);
  state.decisionLog.meta = { boot_epoch_s: now / 1000 - 2000, uptime_s: 2000 };
  const raw = replay.enrichControlWorkingDecisionLogEvents(events).map(e => replay.mapDecisionEventToControlWorkingItem(e, "today", now)).filter(Boolean);
  const items = replay.buildControlWorkingTimelineItems(raw);
  assert.equal(items.length, 2);
  for (const item of items.filter(i => i.cycle)) {
    assert.match(item.summary, /ontbreekt/);
    assert.equal(item.duration, "");
  }
});

test("grouped stops retain saved restart thresholds, power readings and postflow duration", () => {
  const common = { subject: "HP2", reason: "ph_comfort_stop", flags: 0, cm: 1, duration_s: 60 };
  const events = [event(1, 100, "heating_stop_context", "HP2", { ...common, flags: 1, value_a: 900, value_b: 0, threshold_a: 2500 }),
    event(2, 100, "heating_stop_context", "HP2", { ...common, flags: 2, value_a: 2050, value_b: 2030, threshold_a: 2050 }),
    event(3, 100, "heating_stop_context", "HP2", { ...common, flags: 3, value_a: -32768, value_b: -32768, threshold_a: -32768 }),
    event(4, 100, "source_stop", "HP2", { ...common, flags: 32 }),
    event(5, 101, "flow_hold_start", "SYSTEM", { reason: "flow_postflow" }),
    event(6, 103, "hp_stop_confirmed", "HP2"),
    event(7, 131, "flow_hold_clear", "SYSTEM", { reason: "flow_postflow", duration_s: 30 })];
  const grouped = replay.buildControlWorkingTimelineItems(map(events));
  const html = replay.renderControlWorkingDetails(grouped.find(i => i.periodSteps));
  assert.match(html, /20,30 °C/);
  assert.match(html, /900 W.*0 W.*2\.500 W/s);
  assert.match(html.split("<details")[1], /30s/);
});

test("conflicting raw records keep distinct identities through timeline enrichment", () => {
  const events = [event(1, 0, "source_stop", "HP1"), event(2, 10, "hp_stop_confirmed", "HP1"),
    event(2, 10, "source_start", "HP1")];
  const items = replay.buildControlWorkingTimelineItems(map(events));
  const stop = items.find(i => i.periodSteps);
  assert.match(stop.summary, /niet bevestigd/);
  assert.ok(items.some(i => i.realEventType === "hp_stop_confirmed"));
  const cycles = [event(1, 0, "incident_start", "HP2", { reason: "hp_fault", value_a: 4 }),
    event(2, 10, "incident_clear", "HP2", { reason: "hp_fault", value_a: 4 }),
    event(2, 10, "incident_clear", "HP2", { reason: "hp_fault", value_a: 21, flags: 0x41, to: "blocked" })];
  const period = replay.buildControlWorkingTimelineItems(map(cycles)).find(i => i.cycle);
  assert.match(period.summary, /Tegenstrijdige meldingen/);
  assert.equal(period.duration, "");
});
