import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.localStorage = { getItem: () => null };
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };

const { state } = await import("../js/src/core/state.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const { OVERVIEW_ENERGY_COLUMN_CONFIGS } = await import("../js/src/core/config.js");
const { getOverviewLikeHydrationKeys } = await import("../js/src/core/entity-sync.js");
const {
  getEnergyHistoryBucketTooltip,
  getEnergyHistoryPanelModel,
  getEnergyHistoryRenderSignature,
  renderEnergyHistoryLegend,
  renderEnergyHistoryPanel,
  renderOverviewEnergyColumn,
  shouldShowEnergyHistoryBoiler,
} = await import("../js/src/views/energy.js");
const replaySource = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const { getControlWorkingCurrent, renderControlWorkingNowCard, renderControlWorkingStatusTab } =
  await loadReplayHarness(replaySource + "\nexport { getControlWorkingCurrent, renderControlWorkingNowCard, renderControlWorkingStatusTab };");

const value = (raw) => ({ value: raw, state: String(raw) });
function setup(values = {}) {
  setLocale("nl", { persist: false, notify: false });
  state.drafts = {};
  state.inputDrafts = {};
  state.decisionLog = null;
  state.energyHistoryRaw = "";
  state.entities = Object.fromEntries(Object.entries(values).map(([key, raw]) => [key, value(raw)]));
}
const energyValues = {
  heatingPowerInput: 1081,
  totalHeat: 4244,
  boilerHeatPower: 0,
  systemHeatPower: 4079,
  heatpumpThermalEnergyDaily: 3.9,
  boilerThermalEnergyDaily: 0,
  systemThermalEnergyDaily: 3.9,
  heatpumpThermalEnergyCumulative: 51.4,
  boilerThermalEnergyCumulative: 0,
  systemThermalEnergyCumulative: 51.3,
};
const renderEnergyColumns = () => OVERVIEW_ENERGY_COLUMN_CONFIGS.map(renderOverviewEnergyColumn).join("");

test("energy flows hide the CV group when no auxiliary heat source is connected", () => {
  setup({ ...energyValues, auxHeatSourcePresent: false });
  const markup = renderEnergyColumns();
  assert.doesNotMatch(markup, /CV-ketel/);
  assert.match(markup, /Warmtepomp/);
  assert.match(markup, /Systeem/);

  setup({ ...energyValues, auxHeatSourcePresent: true });
  assert.equal(renderEnergyColumns().match(/CV-ketel/g)?.length, 3);
});

test("energy flows fall back to the legacy assist switch on older firmware", () => {
  setup({ ...energyValues, boilerCvAssistEnabled: false });
  assert.doesNotMatch(renderEnergyColumns(), /CV-ketel/);
  setup({ ...energyValues, boilerCvAssistEnabled: true });
  assert.match(renderEnergyColumns(), /CV-ketel/);
});

test("recorded CV heat stays visible after the source is disconnected", () => {
  setup({ ...energyValues, auxHeatSourcePresent: false, boilerThermalEnergyCumulative: 12.4 });
  const markup = renderEnergyColumns();
  assert.equal(markup.match(/CV-ketel/g)?.length, 1);
  assert.match(markup, /12\.4/);
});

test("energy history drops CV stat, balance, legend and tooltip without a source", () => {
  setup({ auxHeatSourcePresent: false });
  const model = getEnergyHistoryPanelModel();
  assert.equal(model.showBoiler, false);
  const markup = renderEnergyHistoryPanel(model);
  assert.doesNotMatch(markup, /Cv-ketel|cv-ketel|Ketel/);
  assert.match(markup, /oq-energy-history-stats oq-energy-history-stats--three/);
  assert.doesNotMatch(renderEnergyHistoryLegend({ boilerOutputWh: 0 }, false), /Ketel/);
  assert.doesNotMatch(getEnergyHistoryBucketTooltip({ label: "Ma", boilerHeatOutputWh: 0 }, false), /Cv-ketel/);

  setup({ auxHeatSourcePresent: true });
  const connected = getEnergyHistoryPanelModel();
  assert.equal(connected.showBoiler, true);
  assert.notEqual(getEnergyHistoryRenderSignature(connected), getEnergyHistoryRenderSignature(model));
  const connectedMarkup = renderEnergyHistoryPanel(connected);
  assert.match(connectedMarkup, /Cv-ketel/);
  assert.match(connectedMarkup, /Ketel/);
  assert.doesNotMatch(connectedMarkup, /oq-energy-history-stats--three/);
});

test("energy history keeps CV for periods with recorded CV heat", () => {
  setup({ auxHeatSourcePresent: false });
  assert.equal(shouldShowEnergyHistoryBoiler({ boilerOutputWh: 0 }), false);
  assert.equal(shouldShowEnergyHistoryBoiler({ boilerOutputWh: 850 }), true);
});

test("energy and results views hydrate the auxiliary heat source setting", () => {
  setup();
  for (const view of ["energy", "results"]) {
    const keys = getOverviewLikeHydrationKeys(view);
    assert.ok(keys.includes("auxHeatSourcePresent"), view);
    assert.ok(keys.includes("boilerCvAssistEnabled"), view);
  }
});

test("decision log hides the CV source without a connected source", () => {
  setup({ auxHeatSourcePresent: false });
  const current = getControlWorkingCurrent([]);
  assert.equal(current.cvAvailable, false);
  assert.doesNotMatch(renderControlWorkingNowCard(current), /CV ·/);
  const status = renderControlWorkingStatusTab(current);
  assert.doesNotMatch(status, /<span>CV<\/span>/);
  assert.equal(status.match(/class="oq-working-source-card/g)?.length, 1);
  assert.match(status, /oq-working-source-grid oq-working-source-grid--single/);

  setup({ auxHeatSourcePresent: true });
  const connected = getControlWorkingCurrent([]);
  assert.equal(connected.cvAvailable, true);
  assert.match(renderControlWorkingNowCard(connected), /CV · Uit/);
  const connectedStatus = renderControlWorkingStatusTab(connected);
  assert.match(connectedStatus, /<span>CV<\/span>/);
  assert.match(connectedStatus, /oq-working-source-grid oq-working-source-grid--two/);
});

test("decision log still shows an active CV even if the source setting is off", () => {
  setup({ auxHeatSourcePresent: false, boilerActive: true });
  const current = getControlWorkingCurrent([]);
  assert.equal(current.cvAvailable, true);
  assert.match(renderControlWorkingNowCard(current), /CV ·/);
});
