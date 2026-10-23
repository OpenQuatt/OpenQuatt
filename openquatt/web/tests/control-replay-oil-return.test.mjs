import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { setLocale } from "../js/src/i18n/index.js";
import { getControlReplayIncidentDisplaySeverity } from "../js/src/features/control-replay-incidents.js";
import { getConfirmedCompressorHistory } from "../js/src/features/control-replay-compressor-history.js";
import { isOilReturnDecisionEvent } from "../js/src/features/control-replay-status.js";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const replay = await loadReplayHarness(source + "\nexport { mapDecisionEventToControlWorkingItem, renderControlWorkingTimelineItem, renderControlWorkingDetails };\n");
const now = new Date(2026, 9, 23, 6).getTime();
const oil = (type, flags = 34) => ({
  seq: type === "incident_start" ? 1 : 2, epoch_s: now / 1000 - 3600, uptime_s: 1000,
  event_type: type, subject: "HP2", reason: "hp_fault", severity: "normal", cm: 2,
  value_a: 4, value_b: type === "incident_start" ? 8 : 0, threshold_a: 1,
  flags, duration_s: type === "incident_clear" ? 170 : 0,
});

test("oil-return start and completion are normal operation throughout the NL/EN replay", () => {
  state.entities = {};
  state.controlReplayWindow = "today";
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const flags of [0, 1, 34, 35]) {
      for (const type of ["incident_start", "incident_clear"]) {
        const event = oil(type, flags);
        const item = replay.mapDecisionEventToControlWorkingItem(event, "today", now);
        assert.equal(item.title, locale === "nl"
          ? type === "incident_start" ? "Olie-retour gestart" : "Olie-retour afgerond"
          : type === "incident_start" ? "Oil return started" : "Oil return completed");
        assert.equal(item.severity, "normal");
        assert.equal(item.reasonLabel, locale === "nl" ? "Olie-retour" : "Oil return");
        assert.notEqual(item.detail, item.reasonSummary);
        assert.match(item.detail, locale === "nl" ? /smeerolie.*koudemiddel.*terug naar de compressor/ : /lubricating oil.*refrigerant.*back to the compressor/);
        const html = replay.renderControlWorkingTimelineItem(item, null) + replay.renderControlWorkingDetails(item);
        assert.doesNotMatch(html, locale === "nl" ? /storing|hersteld|oorzaak|actief actief/i : /\bfault\b|recovered|cause|active active/i);
        assert.match(html, locale === "nl" ? /normaal warmtepompbedrijf/ : /normal heat pump operation/);
        if (type === "incident_clear") assert.ok(item.duration);
      }
    }
  }
  setLocale("nl");
});

test("oil return does not truncate confirmed compressor operation or conceal another pump's real fault", () => {
  const records = [
    { seq: 1, epoch_s: 10, event_type: "hp_start_confirmed", subject: "BOTH", cm: 2 },
    { ...oil("incident_start"), seq: 2, epoch_s: 20 },
    { seq: 3, epoch_s: 30, event_type: "incident_start", subject: "HP1", reason: "hp_fault", value_a: 22 },
    { ...oil("incident_clear"), seq: 4, epoch_s: 40 },
    { seq: 5, epoch_s: 50, event_type: "hp_stop_confirmed", subject: "HP2" },
  ];
  const result = getConfirmedCompressorHistory(records, { start: 0, end: 100, timeOf: event => event.epoch_s });
  assert.deepEqual(result.intervals.map(({ source, start, end, endUncertain }) => [source, start, end, endUncertain]),
    [["HP1", 10, 30, true], ["HP2", 10, 50, false]]);
});

test("the status exception never swallows other IDs, synthetic faults or non-incident records", () => {
  for (const event of [
    { ...oil("incident_start"), value_a: 22, severity: "fault" },
    { ...oil("incident_start"), reason: "hp_link_loss", severity: "fault" },
    { ...oil("incident_start"), value_a: undefined, severity: "fault" },
    { ...oil("incident_start"), event_type: "source_start", severity: "fault" },
  ]) {
    assert.equal(isOilReturnDecisionEvent(event), false);
    assert.notEqual(getControlReplayIncidentDisplaySeverity(event), "normal");
  }
});
