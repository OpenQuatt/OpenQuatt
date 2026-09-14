import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  requestAnimationFrame: (callback) => callback(),
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const { getCurveOverviewModel } = await import("../js/src/views/overview.js");

function valueEntity(value, uom = "") {
  return { value, state: value == null ? "nan" : String(value), uom };
}

function setCurveState({ curve, strategy, supply, activeSource, outside = 8.2 }) {
  state.entities = {
    curveSupplyTarget: valueEntity(curve, "°C"),
    supplyTemp: valueEntity(supply, "°C"),
    outsideTempSelected: valueEntity(outside, "°C"),
  };
  if (strategy !== undefined) {
    state.entities.strategySupplyTarget = valueEntity(strategy, "°C");
  }
  if (activeSource !== undefined) {
    state.entities.heatingSupplyTargetActiveSource = { value: activeSource, state: activeSource };
  }
}

test("overzicht gebruikt het effectieve target bij een extern aanvoerdoel", () => {
  // Repro uit de review: extern 42 °C, stooklijn 33 °C, aanvoer 40 °C.
  // Met het stooklijntarget zou dit "Boven doel, +7,0 °C" tonen.
  setCurveState({ curve: 33.0, strategy: 42.0, supply: 40.0, activeSource: "external" });

  const model = getCurveOverviewModel();
  assert.equal(model.targetText, "42 °C");
  assert.equal(model.deltaText, "-2.0 °C");
  assert.equal(model.statusTitle, "Nog onder doel");
});

test("overzicht valt terug op het stooklijntarget zonder strategietarget", () => {
  setCurveState({ curve: 33.0, strategy: undefined, supply: 40.0, activeSource: "curve" });

  const model = getCurveOverviewModel();
  assert.equal(model.targetText, "33 °C");
  assert.equal(model.deltaText, "+7.0 °C");
  assert.equal(model.statusTitle, "Boven doel");
});

test("overzicht meldt een actief extern doel bij ontbrekende aanvoer", () => {
  setCurveState({ curve: 33.0, strategy: 42.0, supply: null, activeSource: "external" });

  const model = getCurveOverviewModel();
  assert.equal(model.targetText, "42 °C");
  assert.equal(model.deltaText, "—");
  assert.equal(model.statusTitle, "Extern doel actief");
});
