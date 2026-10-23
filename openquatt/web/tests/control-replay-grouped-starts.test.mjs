import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { groupControlReplayStarts } from "../js/src/features/control-replay-starts.js";
import { setLocale } from "../js/src/i18n/index.js";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const replay = await loadReplayHarness(source + "\nexport { buildControlWorkingStartItems, mapDecisionEventToControlWorkingItem, renderControlWorkingDetails, renderControlWorkingTimelineTab, getControlWorkingSelectedItem, getControlWorkingDecisionLogChartLanes, getControlWorkingActiveGraphContextForMinute, buildControlWorkingDerivedItems };\n");
const now = new Date(2026, 9, 23, 1, 20).getTime();
const realNow = Date.now;
test.after(() => { Date.now = realNow; });
const base = now / 1000 - 780;
const event = (seq, seconds, type, subject = "SYSTEM", extra = {}) => ({
  seq, epoch_s: base + seconds, uptime_s: 1000 + seconds, event_type: type,
  subject, reason: "runtime_lead", severity: "normal", cm: 2, ...extra,
});
const sequence = () => [
  event(1, 3, "control_mode_change", "SYSTEM", { value_a: 0, value_b: 1 }),
  event(2, 3, "flow_hold_start", "SYSTEM", { reason: "flow_preflow" }),
  event(3, 33, "control_mode_change", "SYSTEM", { value_a: 1, value_b: 2 }),
  event(4, 33, "flow_hold_clear", "SYSTEM", { reason: "flow_preflow", duration_s: 30 }),
  event(5, 41, "topology_change", "HP2", { to: "single" }),
  event(6, 41, "source_start", "HP2"),
  event(7, 54, "hp_start_confirmed", "HP2"),
];
const group = events => groupControlReplayStarts(events, e => e.epoch_s * 1000);
test.beforeEach(() => {
  Date.now = () => now;
  setLocale("nl");
  state.decisionLog = { ok: true, meta: {}, events: [] };
  state.entities = { installationTopology: { value: "duo" } };
  state.controlReplayWindow = "last24";
  state.controlReplayTimelineLimit = 80;
  state.decisionLogLastFetchAt = now;
});

test("seven start records become one sequence with actual stages and technical details, NL/EN", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const events = sequence();
    const rawItems = events.map(e => replay.mapDecisionEventToControlWorkingItem(e, "last24", now));
    const items = replay.buildControlWorkingStartItems(rawItems);
    assert.equal(items.length, 1);
    assert.equal(items[0].startSteps.length, 7);
    const details = replay.renderControlWorkingDetails(items[0]);
    assert.match(details, /01:07:03/);
    assert.match(details, /01:07:41/);
    assert.match(details, /01:07:54/);
    assert.match(details, /HP2/);
    assert.equal((details.match(/<li>/g) || []).length, 4);
    assert.match(details, /CM0 → CM1/);
    assert.match(details, /30s/);
    state.controlReplaySelectedEpisode = rawItems[5].id;
    assert.equal(replay.getControlWorkingSelectedItem(items).id, items[0].id);
  }
});

test("interleaved Duo feedback belongs to the corresponding pump", () => {
  const result = group([event(1, 0, "source_start", "HP1"), event(2, 1, "source_start", "HP2"),
    event(3, 10, "hp_start_confirmed", "HP2"), event(4, 12, "hp_start_confirmed", "HP1")]);
  assert.deepEqual(result.map(a => a.events.map(e => e.subject)), [["HP1", "HP1"], ["HP2", "HP2"]]);
});

test("feedback outside the selected day never becomes an invisible successful or failed stage", () => {
  state.controlReplayWindow = "yesterday";
  for (const type of ["hp_start_confirmed", "incident_start"]) {
    const command = event(1, 0, "source_start", "HP2", { epoch_s: new Date(2026, 9, 22, 23, 59, 50).getTime() / 1000 });
    const outcome = event(2, 15, type, "HP2", { epoch_s: new Date(2026, 9, 23, 0, 0, 5).getTime() / 1000, reason: "hp_start_failed", severity: "fault" });
    state.decisionLog.events = [command, outcome];
    const items = state.decisionLog.events.map(e => replay.mapDecisionEventToControlWorkingItem(e, "yesterday", now)).filter(Boolean);
    const grouped = replay.buildControlWorkingStartItems(items);
    assert.equal(grouped[0].startSteps.length, 1);
    assert.match(grouped[0].summary, /geen compressorbevestiging/);
    assert.equal(grouped[0].severity, "normal");
  }
});

test("stop, reboot, timeout and retry never borrow another attempt's confirmation", () => {
  for (const barrier of [event(2, 2, "source_stop", "HP2"), event(2, 2, "boot_marker")]) {
    assert.equal(group([event(1, 0, "source_start", "HP2"), barrier, event(3, 10, "hp_start_confirmed", "HP2")])[0].confirmed, false);
  }
  assert.equal(group([event(1, 0, "source_start", "HP2"), event(2, 181, "hp_start_confirmed", "HP2")])[0].confirmed, false);
  const retries = group([event(1, 0, "source_start", "HP2"), event(2, 1, "source_start", "HP2"), event(3, 12, "hp_start_confirmed", "HP2")]);
  assert.deepEqual(retries.map(a => a.confirmed), [false, true]);
  assert.equal(group([event(1, 0, "source_start", "HP2"), event(3, 12, "hp_start_confirmed", "HP2")])[0].confirmed, false);
});

test("graph bars and cursor context use feedback boundaries, with no minimum-width runtime inflation", () => {
  const events = [...sequence(), event(8, 362, "source_stop", "HP2"), event(9, 384, "hp_stop_confirmed", "HP2")];
  state.decisionLog.events = events;
  const items = events.map(e => replay.mapDecisionEventToControlWorkingItem(e, "last24", now));
  const lane = replay.getControlWorkingDecisionLogChartLanes(items).find(lane => lane.label === "HP2");
  const percentage = seconds => (base + seconds - (now / 1000 - 86400)) / 86400 * 100;
  assert.ok(Math.abs(lane.segments[0].start - percentage(54)) < 1e-8);
  assert.ok(Math.abs(lane.segments[0].width - (384 - 54) / 86400 * 100) < 1e-8);
  assert.equal(replay.getControlWorkingActiveGraphContextForMinute(items, percentage(45) * 14.4), null);
  assert.ok(replay.getControlWorkingActiveGraphContextForMinute(items, percentage(370) * 14.4));
});

test("legacy commands and unclocked archived confirmations do not become measured HP bars", () => {
  for (const events of [
    sequence().filter(e => e.event_type !== "hp_start_confirmed"),
    sequence().map(e => ({ ...e, epoch_s: 0 })),
  ]) {
    state.decisionLog.events = events;
    state.decisionLog.meta = { boot_epoch_s: base - 1000, uptime_s: 1780 };
    const items = events.map(e => replay.mapDecisionEventToControlWorkingItem(e, "last24", now)).filter(Boolean);
    assert.ok(!replay.getControlWorkingDecisionLogChartLanes(items).some(lane => ["HP1", "HP2"].includes(lane.label)));
  }
});

test("confirmed compressors before a quiet window remain visible without in-window events", () => {
  state.controlReplayWindow = "last1";
  state.decisionLog.events = [event(1, -7200, "hp_start_confirmed", "HP1", { reason: "heating_request" })];
  let lanes = replay.getControlWorkingDecisionLogChartLanes([]);
  assert.ok(lanes.some(lane => lane.label === "HP1" && lane.segments[0].width === 100));
  state.decisionLog.events.push(event(2, -7100, "hp_start_confirmed", "HP2", { reason: "cooling_request", cm: 5 }));
  lanes = replay.getControlWorkingDecisionLogChartLanes([]);
  assert.ok(lanes.some(lane => lane.label === "HP2"));
  assert.ok(lanes.some(lane => lane.label === "Koeling"));
});

test("a stale log snapshot caps open confirmed periods and cooling identity survives CM change", () => {
  const events = [event(1, 0, "source_start", "HP2", { cm: 5 }), event(2, 10, "hp_start_confirmed", "HP2", { cm: 1, reason: "cooling_request" })];
  state.decisionLog.events = events;
  state.decisionLog.meta = { boot_epoch_s: base - 1000, uptime_s: 1300 };
  const items = events.map(e => replay.mapDecisionEventToControlWorkingItem(e, "last24", now));
  const lanes = replay.getControlWorkingDecisionLogChartLanes(items);
  assert.ok(lanes.some(lane => lane.label === "Koeling"));
  const hp = lanes.find(lane => lane.label === "HP2");
  assert.ok(Math.abs(hp.segments[0].width - 290 / 86400 * 100) < 1e-8);
  const derived = replay.buildControlWorkingDerivedItems(events, "last24", now);
  assert.ok(derived.some(item => item.modeLabel === "CM5"));
});

test("failed and incomplete starts remain explicit without invented preparation", () => {
  const events = [event(1, 0, "source_start", "HP2"), event(2, 12, "incident_start", "HP2", { reason: "hp_start_failed", severity: "fault" })];
  assert.equal(group(events)[0].failed, true);
  const items = replay.buildControlWorkingStartItems(events.map(e => replay.mapDecisionEventToControlWorkingItem(e, "last24", now)));
  assert.equal(items[0].severity, "fault");
  assert.match(replay.renderControlWorkingDetails(items[0]), /niet gelogd/);
  assert.equal(group([event(1, 1, "hp_start_confirmed", "HP2")]).length, 0);
});

test("timeline exposes its limit and can render all 81 ungrouped entries", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const items = Array.from({ length: 81 }, (_, i) => replay.mapDecisionEventToControlWorkingItem(event(i + 1, i, "hp_stop_confirmed", "HP2"), "last24", now));
    const limited = replay.renderControlWorkingTimelineTab(items, null);
    assert.equal((limited.match(/data-replay-episode=/g) || []).length, 80);
    assert.match(limited, /80.*81/);
    assert.match(limited, /more-control-replay-events/);
    state.controlReplayTimelineLimit = 160;
    const all = replay.renderControlWorkingTimelineTab(items, null);
    assert.equal((all.match(/data-replay-episode=/g) || []).length, 81);
    assert.doesNotMatch(all, /more-control-replay-events/);
    state.controlReplayTimelineLimit = 80;
  }
});
