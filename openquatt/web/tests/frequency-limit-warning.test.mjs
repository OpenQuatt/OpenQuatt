import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { getFrequencyLimitModel, getFrequencyLimitWarning, patchFrequencyLimitWarnings } = await import("../js/src/features/frequency-limits.js");
const { renderSilentSettingsGrid } = await import("../js/src/settings/silent.js");
const { renderSystemModal } = await import("../js/src/features/header-status.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const { getDecisionEventCopy, getControlWorkingCurrent } = await loadReplayHarness(`${source}\nexport { getControlWorkingCurrent };`);

function setup(topology = "single") {
  state.entities = Object.fromEntries(Object.entries({
    installationTopology: topology,
    controlModeLabel: "CM2",
    silentActive: true,
    silentMaxHz: 20,
    dayMaxHz: 90,
    hp1MinimumHeatingHz: 30,
    hp1MinimumCoolingHz: 26,
    hp2MinimumHeatingHz: 20,
    hp2MinimumCoolingHz: 30,
  }).map(([key, value]) => [key, { value, state: String(value), min_value: 20, max_value: 120, step: 1, uom: "Hz" }]));
  state.drafts = {};
  state.inputDrafts = {};
  state.decisionLog = { ok: true, events: [] };
  state.loadingEntities = false;
}

test("silent/day sliders warn below the actual minimum and keep equality/defaults valid", () => {
  setup();
  const warning = getFrequencyLimitWarning("silentMaxHz");
  assert.equal(warning.warning, true);
  assert.match(warning.text, /20 Hz.*HP1 bij verwarmen: 30 Hz/);
  assert.match(warning.text, /HP1 bij koelen: 26 Hz/);
  assert.doesNotMatch(warning.text, /HP2/);
  const markup = renderSilentSettingsGrid();
  assert.match(markup, /data-oq-frequency-limit-warning="silentMaxHz" role="status" aria-live="polite"/);
  assert.match(markup, /Deze buitenunits kunnen in die bedrijfsmodi niet starten/);
  state.systemModal = "silent-settings";
  assert.match(renderSystemModal(), /oq-helper-modal--wide oq-helper-modal--scrollable/);
  assert.equal(getFrequencyLimitWarning("dayMaxHz").text, "");
  for (const cap of [30, 67, 90]) {
    state.drafts.silentMaxHz = cap;
    assert.equal(getFrequencyLimitWarning("silentMaxHz").text, "");
  }
});

test("Duo compares each unit and mode independently, including a valid 20 Hz minimum", () => {
  setup("duo");
  const model = getFrequencyLimitModel("silentMaxHz", { mode: "heating" });
  assert.deepEqual(model.blocked.map(({ unit }) => unit), ["hp1"]);
  assert.equal(model.limits[1].blocked, false);
  state.entities.hp1MinimumHeatingHz.value = 20;
  assert.equal(getFrequencyLimitModel("silentMaxHz", { mode: "heating" }).blocked.length, 0);
  assert.equal(getFrequencyLimitModel("silentMaxHz", { mode: "cooling" }).blocked.length, 2);
});

test("frequency sliders show the highest physical level per mode and distinguish Duo tables", () => {
  setup("duo");
  state.entities.silentMaxHz.value = 72;
  state.entities.silentMaxHz.min_value = 0;
  state.entities.hp1AutomaticFrequencies = { value: "30,39,49,55,61,67,72,79,85,90|26,32,38" };
  state.entities.hp2AutomaticFrequencies = { value: "26,32,38,50,62,71,71,80|26,32,38" };
  const markup = renderSilentSettingsGrid();
  assert.match(markup, /type="range" data-oq-field="silentMaxHz" min="0" max="120" step="1" value="72"/);
  assert.match(markup, /<span>Verwarmen · HP1<\/span><strong>F7 · 72 Hz<\/strong>/);
  assert.match(markup, /<span>Verwarmen · HP2<\/span><strong>F7 · 71 Hz<\/strong>/);
  assert.match(markup, /<span>Verwarmen · HP1<\/span><strong>F10 · 90 Hz<\/strong>/);
  assert.equal((markup.match(/<span>Koelen<\/span>/g) || []).length, 2);
});

test("V2 tables show two summaries per cap, including F20 and an in-between value", () => {
  setup();
  state.entities.silentMaxHz.value = 73;
  state.entities.silentMaxHz.min_value = 0;
  state.entities.dayMaxHz.min_value = 0;
  state.entities.hp1AutomaticFrequencies = {
    value: "20,26,30,36,40,45,48,52,55,60,65,67,68,69,72,76,82,85,88,90|26,32,38,44,50,56,62,68,74,80",
  };
  const markup = renderSilentSettingsGrid();
  assert.match(markup, /type="range" data-oq-field="silentMaxHz" min="0" max="120" step="1" value="73"/);
  assert.match(markup, /<span>Verwarmen<\/span><strong>F15 · 72 Hz<\/strong>/);
  assert.match(markup, /<span>Koelen<\/span><strong>F8 · 68 Hz<\/strong>/);
  assert.match(markup, /<span>Verwarmen<\/span><strong>F20 · 90 Hz<\/strong>/);
  assert.equal((markup.match(/class="oq-settings-frequency-caption"/g) || []).length, 4);
  assert.doesNotMatch(markup, /frequency-ticks|--oq-frequency-position/);
});

test("missing or invalid minimum data stays unknown; polling repairs the existing warning node", () => {
  setup();
  for (const value of [undefined, null, "", "NAN", 0, -1, 121]) {
    state.entities.hp1MinimumHeatingHz = { value };
    state.entities.hp1MinimumCoolingHz = { value };
    assert.equal(getFrequencyLimitWarning("silentMaxHz").warning, false);
    assert.match(getFrequencyLimitWarning("silentMaxHz").text, /nog niet beschikbaar/);
  }
  const node = { dataset: { oqFrequencyLimitWarning: "silentMaxHz" }, textContent: "", hidden: false,
    classList: { toggle: (_name, active) => { node.warning = active; } }, closest: () => card };
  const input = { value: "20" };
  const label = { textContent: "20 Hz" };
  const levels = { innerHTML: "" };
  const elements = {
    'input[type="range"]': input,
    ".oq-helper-slider-meta strong": label,
    "[data-oq-frequency-limit-levels]": levels,
  };
  const card = { querySelector: (selector) => elements[selector] };
  const root = { querySelectorAll: () => [node] };
  patchFrequencyLimitWarnings(root);
  assert.match(node.textContent, /nog niet beschikbaar/);
  assert.equal(levels.innerHTML, "");
  state.entities.hp1MinimumHeatingHz = { value: 30 };
  state.entities.hp1MinimumCoolingHz = { value: 30 };
  state.entities.hp1AutomaticFrequencies = { state: "30,39|30,34" };
  patchFrequencyLimitWarnings(root);
  assert.equal(node.warning, true);
  assert.match(levels.innerHTML, /<strong>Onder F1<\/strong>/);
  state.drafts.silentMaxHz = 35;
  patchFrequencyLimitWarnings(root);
  assert.equal(node.hidden, true);
  assert.equal(node.warning, false);
  assert.equal(input.value, "35");
  assert.equal(label.textContent, "35 Hz");
  assert.match(levels.innerHTML, /<span>Verwarmen<\/span><strong>F1 · 30 Hz<\/strong>/);
  assert.match(levels.innerHTML, /<span>Koelen<\/span><strong>F2 · 34 Hz<\/strong>/);
  state.entities.silentMaxHz.value = 20;
  patchFrequencyLimitWarnings(root);
  assert.equal(input.value, "35", "polling must preserve the unsaved draft");
  state.drafts = {};
  state.entities.hp1AutomaticFrequencies.state = "30,,39|";
  patchFrequencyLimitWarnings(root);
  assert.equal(input.value, "20");
  assert.equal(label.textContent, "20 Hz");
  assert.equal(node.warning, true);
  assert.equal(levels.innerHTML, "");
});

test("current control status does not infer a blocked start from a low cap", () => {
  setup();
  const panels = [{ title: "HP1", keys: { mode: "hp1Mode", freq: "hp1Freq", defrost: "hp1Defrost" } }];
  state.drafts.silentMaxHz = 67;
  assert.equal(getFrequencyLimitWarning("silentMaxHz").warning, false);
  assert.notEqual(getControlWorkingCurrent(panels).primaryReason, "frequency_cap_below_minimum");
  assert.equal(getControlWorkingCurrent(panels).hp1Status, "Beschikbaar");
  state.drafts.silentMaxHz = 20;
  assert.equal(getFrequencyLimitWarning("silentMaxHz").warning, true);
  assert.notEqual(getControlWorkingCurrent(panels).primaryReason, "frequency_cap_below_minimum");
});

test("decision log preserves historical cap, minimum, mode and day/silent selection", () => {
  setup();
  state.entities.silentMaxHz.value = 67;
  state.entities.hp1MinimumHeatingHz.value = 26;
  const event = { event_type: "candidate_blocked", reason: "frequency_cap_below_minimum", subject: "HP1", cm: 2, value_a: 20, value_b: 30, flags: 1 };
  const copy = getDecisionEventCopy(event);
  assert.match(copy.summary, /20 Hz.*verwarmen: 30 Hz/);
  assert.match(copy.detail, /tijdens stille uren/);
  const cooling = getDecisionEventCopy({ ...event, subject: "HP2", cm: 5, flags: 0, _oq_context_cm: 2 });
  assert.match(cooling.summary, /HP2 bij koelen: 30 Hz/);
  assert.match(cooling.detail, /overdag/);
});
