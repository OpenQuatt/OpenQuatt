import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { setLocale, t } from "../js/src/i18n/index.js";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const replay = await loadReplayHarness(source + "\nexport { getControlWorkingOptimizerModel, mapDecisionEventToControlWorkingItem, renderControlWorkingDetails };\n");
const now = new Date(2026, 9, 23, 1, 20, 0).getTime();
const epoch = (seconds) => Math.floor(new Date(2026, 9, 23, 1, 7, seconds).getTime() / 1000);
function event(type = "source_start", subject = "HP2", seconds = 41) {
  return { seq: seconds, epoch_s: epoch(seconds), uptime_s: 1000 + seconds, event_type: type,
    subject, reason: "runtime_lead", cm: 2, from: "idle", to: "active", severity: "normal" };
}

test.beforeEach(() => {
  setLocale("nl");
  // Today's lead and topology must not overwrite the pump in a historical event.
  state.entities = { installationTopology: { value: "duo", state: "duo" }, runtimeLeadHp: { value: "HP1", state: "HP1" } };
});

test("start command and compressor confirmation have distinct truthful titles in NL and EN", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const command = replay.getDecisionEventCopy(event());
    const confirmed = replay.getDecisionEventCopy(event("hp_start_confirmed", "HP2", 54));
    assert.match(command.title, locale === "nl" ? /startopdracht/i : /start command/i);
    assert.match(confirmed.title, locale === "nl" ? /bevestigd/i : /confirmed/i);
    assert.notEqual(command.title, confirmed.title);
    assert.match(command.summary, locale === "nl" ? /nog niet bevestigd/ : /not yet confirmed/);
    assert.doesNotMatch(command.summary, locale === "nl" ? /is gestart/ : /has started/);
  }
});

test("selecting a single pump from idle does not claim its compressor is already running", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const selection = replay.getDecisionEventCopy({ ...event("topology_change"), to: "single" });
    assert.match(selection.title, locale === "nl" ? /gekozen/ : /selected/);
    assert.doesNotMatch(selection.summary, locale === "nl" ? /verwarmt|draait/ : /heating|running/);
  }
});

test("historical HP1 and HP2 choices use raw identity, with correct badges and explanations", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const subject of ["HP1", "HP2"]) {
      const item = replay.mapDecisionEventToControlWorkingItem(event("source_start", subject), "last24", now);
      assert.notEqual(item.source, subject); // Includes the translated heating suffix.
      const model = replay.getControlWorkingOptimizerModel(item);
      const chosen = model.rows.filter(({ tone }) => tone === "selected");
      assert.deepEqual(chosen.map(({ option }) => option), [subject]);
      assert.equal(chosen[0].result, t("controlReplay.optChosen"));
      assert.equal(chosen[0].detail, t("controlReplay.optFitsBalance"));
      const other = model.rows.find(({ option }) => option !== subject);
      assert.equal(other.result, t("controlReplay.optNotNow"));
      assert.equal(other.detail, t("controlReplay.optEqualLessGood"));
      assert.match(replay.renderControlWorkingDetails(item), new RegExp(`<span>${subject}</span>\\s*<strong>${t("controlReplay.optChosen")}</strong>`));
    }
  }
});

test("legacy or incomplete historical identity never fabricates a pump choice", () => {
  const item = replay.mapDecisionEventToControlWorkingItem(event("topology_change", "SYSTEM"), "last24", now);
  assert.equal(replay.getControlWorkingOptimizerModel(item), null);
  state.entities.installationTopology = { value: "single", state: "single" };
  const historical = replay.mapDecisionEventToControlWorkingItem(event(), "last24", now);
  assert.ok(replay.getControlWorkingOptimizerModel(historical).rows.some(({ option, tone }) => option === "HP2" && tone === "selected"));
  assert.equal(replay.getControlWorkingOptimizerModel({ reasonCode: "runtime_lead", source: "HP1", hp2Available: false }).rows.length, 1);
});

test("events within one minute display their seconds in every period and both locales", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const period of ["last24", "last48", "last3d", "week", "today"]) {
      const command = replay.mapDecisionEventToControlWorkingItem(event(), period, now);
      const confirmed = replay.mapDecisionEventToControlWorkingItem(event("hp_start_confirmed", "HP2", 54), period, now);
      assert.match(command.time, /01:07:41/);
      assert.match(confirmed.time, /01:07:54/);
      assert.notEqual(command.time, confirmed.time);
    }
  }
});
