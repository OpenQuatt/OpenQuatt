import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: { getItem: () => null },
  matchMedia: () => ({ matches: false }),
};

const { state } = await import("../js/src/core/state.js");
const { formatOverviewStatValue, getDerivedEfficiencyValue, getLiveHeatingCopValue } = await import("../js/src/core/app-shared.js");
const { getOverviewTrendSeriesCurrentValue } = await import("../js/src/views/overview.js");
const { getOverviewLikeHydrationKeys } = await import("../js/src/core/entity-sync.js");

function setup(overrides = {}) {
  state.drafts = {};
  state.lastKnownInstallationTopology = "";
  state.entities = {
    installationTopology: { value: "single" },
    hp1Mode: { value: "Heating" },
    hp1Freq: { value: 30 },
    hp1Defrost: { value: false },
    hp1Cop: { value: 5 },
    totalCop: { value: 5 },
    totalHeat: { value: 3000 },
    totalPower: { value: 600 },
    heatingPowerInput: { value: 600 },
    ...overrides,
  };
}

test("CM1 suppresses live COP despite positive retained heating samples", () => {
  for (const label of ["CM1", "CM1 - Preflow/Postflow", "CM1 - Voorloop/naloop"]) {
    setup({ controlModeLabel: { value: label } });
    assert.equal(formatOverviewStatValue("totalCop"), "—");
    assert.ok(Number.isNaN(getLiveHeatingCopValue("hp1Cop")));
    assert.ok(Number.isNaN(getOverviewTrendSeriesCurrentValue({ currentKey: "totalCop" }, { cop: 6 })));
    delete state.entities.totalCop;
    assert.ok(Number.isNaN(getDerivedEfficiencyValue("totalCop")));
  }
  for (const label of ["CM2 - Heating", "CM3 - Heating", "CM100 - Commissioning"]) {
    setup({ controlModeLabel: { value: label } });
    assert.equal(formatOverviewStatValue("totalCop"), "5.0");
  }
});

test("start with pump consumption and negative heat never displays a negative COP", () => {
  for (const value of [-6.5, 0, NaN, "nan", "", null, Infinity]) {
    setup({ hp1Freq: { value: 0 }, totalHeat: { value: -233 }, totalPower: { value: 36 }, heatingPowerInput: { value: 36 }, totalCop: { value } });
    assert.equal(formatOverviewStatValue("totalCop"), "—");
    assert.ok(Number.isNaN(getDerivedEfficiencyValue("totalCop")));
  }
});

test("firmware suppression remains authoritative after the compressor starts", () => {
  for (const value of [NaN, "nan", "", null, 0, -6.5, Infinity]) {
    setup({ totalCop: { value } });
    assert.equal(formatOverviewStatValue("totalCop"), "—");
  }
  setup();
  assert.equal(formatOverviewStatValue("totalCop"), "5.0");
  assert.equal(getLiveHeatingCopValue("hp1Cop"), 5);
});

test("legacy fallback requires measured heating, positive output and available input", () => {
  setup();
  delete state.entities.totalCop;
  assert.equal(getDerivedEfficiencyValue("totalCop"), 5);
  for (const heat of [-233, 0, NaN, Infinity]) {
    state.entities.totalHeat = { value: heat };
    assert.ok(Number.isNaN(getDerivedEfficiencyValue("totalCop")));
  }
  state.entities.totalHeat = { value: 3000 };
  for (const input of [0, 9.999, -36, NaN, Infinity]) {
    state.entities.heatingPowerInput = { value: input };
    assert.ok(Number.isNaN(getDerivedEfficiencyValue("totalCop")));
  }
  delete state.entities.heatingPowerInput;
  assert.equal(getDerivedEfficiencyValue("totalCop"), 5);
  state.entities.hp1Freq = { value: 0 };
  assert.ok(Number.isNaN(getDerivedEfficiencyValue("totalCop")));
});

test("unknown operating data and defrost suppress all live COP consumers", () => {
  for (const overrides of [
    { hp1Freq: { value: "" } }, { hp1Freq: { value: NaN } },
    { hp1Mode: { value: "Unknown (9)" } }, { hp1Mode: { value: "Standby" } },
    { hp1Mode: { value: "Defrost" } }, { hp1Defrost: { value: true } },
    { hp1Defrost: { state: "unknown" } },
  ]) {
    setup(overrides);
    assert.equal(formatOverviewStatValue("totalCop"), "—");
    assert.ok(Number.isNaN(getLiveHeatingCopValue("hp1Cop")));
    assert.ok(Number.isNaN(getOverviewTrendSeriesCurrentValue({ currentKey: "totalCop", sampleKey: "cop" }, { cop: 6 })));
  }
  setup();
  delete state.entities.hp1Defrost;
  assert.equal(formatOverviewStatValue("totalCop"), "—");
});

test("Duo permits one heating compressor but suppresses partial telemetry or defrost", () => {
  setup({ installationTopology: { value: "duo" }, hp1Freq: { value: 0 }, hp2Mode: { value: "Heating" }, hp2Freq: { value: 30 }, hp2Defrost: { value: false }, hp2Cop: { value: 5 } });
  assert.equal(formatOverviewStatValue("totalCop"), "5.0");
  assert.ok(Number.isNaN(getLiveHeatingCopValue("hp1Cop")));
  assert.equal(getLiveHeatingCopValue("hp2Cop"), 5);
  state.entities.hp1Mode = { value: "Defrost" };
  assert.equal(formatOverviewStatValue("totalCop"), "—");
  state.entities.hp1Mode = { value: "Standby" };
  delete state.entities.hp1Freq;
  assert.equal(formatOverviewStatValue("totalCop"), "—");
});

test("direct energy/results navigation keeps live COP operating telemetry hydrated", () => {
  for (const view of ["energy", "results"]) {
    const keys = getOverviewLikeHydrationKeys(view);
    for (const key of ["controlModeLabel", "hp1Mode", "hp1Freq", "hp1Defrost", "hp2Mode", "hp2Freq", "hp2Defrost"]) {
      assert.ok(keys.includes(key), `${view} must hydrate ${key}`);
    }
  }
});

test("COP guards leave signed heat and other efficiencies unchanged", () => {
  setup({ totalHeat: { value: -233 }, totalCop: { value: -6.5 }, totalEer: { value: 4 }, totalCoolingPower: { value: 2400 }, coolingPowerInput: { value: 600 } });
  assert.equal(formatOverviewStatValue("totalHeat"), "-233 W");
  assert.equal(getDerivedEfficiencyValue("totalEer"), 4);
  assert.equal(formatOverviewStatValue("totalCop"), "—");
});
