import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { loadReplayHarness, decisionCopyDigests } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
globalThis.document = { activeElement: null };
const { state } = await import("../js/src/core/state.js");
const { replaceOuterHtmlIfSignatureChanged } = await import("../js/src/views/view-utils.js");
const energy = await import("../js/src/views/energy.js");
const replay = await loadReplayHarness();
const initial = structuredClone(state);
const originalNow = Date.now;

test.beforeEach(() => {
  Object.assign(state, structuredClone(initial));
  state.entities = {};
  document.activeElement = null;
  Date.now = () => 1_783_944_000_000;
  for (const key of Object.keys(replay.renderCalls)) replay.renderCalls[key] = 0;
});
test.afterEach(() => { Date.now = originalNow; });

function fakePanel(signature = "") {
  let writes = 0;
  const panel = {
    dataset: { renderSignature: signature },
    set outerHTML(markup) {
      writes++;
      // Simulate finding the replacement node on the next patch.
      this.dataset.renderSignature = (markup.match(/data-render-signature="([^"]*)"/)?.[1] || "")
        .replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&");
    },
  };
  return { panel, writes: () => writes };
}

test("signature guard skips both HTML generation and DOM replacement when unchanged or absent", () => {
  let renders = 0;
  const render = () => { renders++; return '<div data-render-signature="new"></div>'; };
  const { panel, writes } = fakePanel("old");
  assert.equal(replaceOuterHtmlIfSignatureChanged(null, "new", render), false);
  assert.equal(replaceOuterHtmlIfSignatureChanged(panel, "old", render), false);
  assert.equal(renders, 0);
  assert.equal(writes(), 0);
  assert.equal(replaceOuterHtmlIfSignatureChanged(panel, "new", render), true);
  assert.equal(renders, 1);
  assert.equal(writes(), 1);
  assert.equal(replaceOuterHtmlIfSignatureChanged(panel, "new", render), false);
  assert.equal(renders, 1);
});

test("failed HTML generation leaves the previous DOM available for a retry", () => {
  const { panel, writes } = fakePanel("old");
  assert.throws(() => replaceOuterHtmlIfSignatureChanged(panel, "new", () => { throw new Error("render failed"); }), /render failed/);
  assert.equal(writes(), 0);
  assert.equal(panel.dataset.renderSignature, "old");
  assert.equal(replaceOuterHtmlIfSignatureChanged(panel, "new", () => '<div data-render-signature="new"></div>'), true);
});

test("decision copy preserves the pre-refactor output for 21,504 combinations", async () => {
  // Golden digests captured from commit 26ea56d: 28 types × 32 reasons × 3 subjects × 8 contexts.
  const expected = JSON.parse(await readFile(new URL("./fixtures/decision-copy-digests.json", import.meta.url), "utf8"));
  assert.deepEqual(decisionCopyDigests(replay.getDecisionEventCopy), expected);
});

test("source-start copy does not evaluate unrelated topology or startup fields", () => {
  const event = { event_type: "source_start", subject: "HP1", reason: "keep_current", cm: 2 };
  for (const field of ["to", "value_a"]) {
    Object.defineProperty(event, field, { get() { throw new Error(`unused field ${field}`); } });
  }
  assert.match(replay.getDecisionEventCopy(event).title, /gestart/);
});

for (const tab of ["status", "timeline", "graphs"]) {
  test(`decision ${tab} computes current state once and only builds required history`, () => {
    state.appView = "control";
    state.controlReplayTab = tab;
    replay.renderControlReplayView();
    assert.equal(replay.renderCalls.getControlWorkingCurrent, 1);
    assert.equal(replay.renderCalls.getControlWorkingDecisionLogItems, tab === "status" ? 0 : 1);
  });
}

test("unchanged decision patch skips history; changed signature builds it exactly once", () => {
  state.appView = "control";
  state.controlReplayTab = "timeline";
  const { panel, writes } = fakePanel();
  const board = { className: "", querySelector: () => panel };
  state.root = { querySelector: () => board };
  assert.equal(replay.patchControlReplayDom(), true);
  assert.equal(replay.renderCalls.getControlWorkingCurrent, 1);
  assert.equal(replay.renderCalls.getControlWorkingDecisionLogItems, 1);
  assert.equal(replay.patchControlReplayDom(), true);
  assert.equal(replay.renderCalls.getControlWorkingDecisionLogItems, 1);
  assert.equal(writes(), 1);
  state.decisionLogSignature = "new events";
  assert.equal(replay.patchControlReplayDom(), true);
  assert.equal(replay.renderCalls.getControlWorkingDecisionLogItems, 2);
  assert.equal(replay.renderCalls.getControlWorkingCurrent, 3);
  assert.equal(writes(), 2);
});

for (const [view, patch, signature] of [
  ["energy", energy.patchEnergyDom, energy.getEnergySectionRenderSignature],
  ["results", energy.patchResultsDom, energy.getEnergyHistoryRenderSignature],
]) {
  test(`${view}: unchanged and changed panels are handled without a full-render fallback`, () => {
    state.appView = view;
    const { panel, writes } = fakePanel(signature());
    state.root = { querySelector: () => ({ className: "", querySelector: () => panel }) };
    assert.equal(patch(), true);
    assert.equal(writes(), 0);
    panel.dataset.renderSignature = "stale";
    assert.equal(patch(), true);
    assert.equal(writes(), 1);
    assert.equal(patch(), true);
    assert.equal(writes(), 1);
    state.root = { querySelector: () => null };
    assert.equal(patch(), false);
  });
}

test("focused history period input defers replacement until focus leaves", () => {
  state.appView = "results";
  const { panel, writes } = fakePanel("stale");
  state.root = { querySelector: () => ({ className: "", querySelector: () => panel }) };
  document.activeElement = { closest: () => ({}) };
  assert.equal(energy.patchResultsDom(), true);
  assert.equal(writes(), 0);
  document.activeElement = null;
  assert.equal(energy.patchResultsDom(), true);
  assert.equal(writes(), 1);
});
