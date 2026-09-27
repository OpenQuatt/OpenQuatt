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
  assert.equal(model.deltaText, "−2,0 °C");
  assert.equal(model.statusTitle, "Nog onder doel");
});

test("overzicht valt terug op het stooklijntarget zonder strategietarget", () => {
  setCurveState({ curve: 33.0, strategy: undefined, supply: 40.0, activeSource: "curve" });

  const model = getCurveOverviewModel();
  assert.equal(model.targetText, "33 °C");
  assert.equal(model.deltaText, "+7,0 °C");
  assert.equal(model.statusTitle, "Boven doel");
});

test("overzicht meldt een actief extern doel bij ontbrekende aanvoer", () => {
  setCurveState({ curve: 33.0, strategy: 42.0, supply: null, activeSource: "external" });

  const model = getCurveOverviewModel();
  assert.equal(model.targetText, "42 °C");
  assert.equal(model.deltaText, "—");
  assert.equal(model.statusTitle, "Extern doel actief");
});

const { setLocale, optionLabel } = await import("../js/src/i18n/index.js");

test("extern doel blijft actief zonder buitenmeting en vermeldt de gekozen externe bron", () => {
  setCurveState({ curve: 30, strategy: 25, supply: null, activeSource: "external", outside: null });
  state.entities.heatingSupplyTargetSource = valueEntity("HA input");
  const model = getCurveOverviewModel();
  assert.equal(model.statusTitle, "Extern doel actief");
  assert.match(model.targetCopy, /extern aanvoerdoel.*HA-invoer/);
  assert.match(model.targetCopy, /30 °C/);
  assert.doesNotMatch(model.targetCopy, /fallback-aanvoertemperatuur/);
});

test("lokale fallback en maximum worden uitgelegd zonder een actieve begrenzing te claimen", () => {
  setCurveState({ curve: 28, strategy: 28, supply: 27, activeSource: "curve", outside: null });
  state.entities.maxWater = valueEntity(55, "°C");
  state.entities.heatingSupplyTargetSource = valueEntity("MQTT");
  const model = getCurveOverviewModel();
  assert.match(model.targetCopy, /fallback-aanvoertemperatuur/);
  assert.match(model.targetCopy, /Ingesteld maximum: 55 °C/);
  assert.doesNotMatch(model.targetCopy, /MQTT/);
});

test("oude firmware krijgt lokale uitleg; ontbrekende en onbevestigde data krijgen geen verzonnen bron", () => {
  setCurveState({ curve: 30, strategy: undefined, supply: 29 });
  assert.match(getCurveOverviewModel().targetCopy, /gefilterde buitentemperatuur/);
  setCurveState({ curve: null, strategy: undefined, supply: null });
  assert.match(getCurveOverviewModel().targetCopy, /nog niet bevestigd/);
  setCurveState({ curve: 30, strategy: undefined, supply: null, activeSource: "external" });
  assert.match(getCurveOverviewModel().targetCopy, /nog niet bevestigd/);
  setCurveState({ curve: 30, strategy: 25, supply: 24, activeSource: "" });
  assert.match(getCurveOverviewModel().targetCopy, /nog niet bevestigd/);
});

test("doeluitleg en strategienaam wisselen mee naar Engels", () => {
  setCurveState({ curve: 30, strategy: 25, supply: 24, activeSource: "external" });
  state.entities.heatingSupplyTargetSource = valueEntity("<unknown>");
  setLocale("en");
  try {
    assert.match(getCurveOverviewModel().targetCopy, /external controller/);
    assert.equal(optionLabel("Water Temperature Control (heating curve)"), "Heating curve");
  } finally {
    setLocale("nl");
  }
  assert.equal(optionLabel("Water Temperature Control (heating curve)"), "Stooklijnregeling");
});

test("afwezige curve- en aanvoerentities worden niet als nul behandeld", () => {
  state.entities = {};
  assert.match(getCurveOverviewModel().targetCopy, /nog niet bevestigd/);
  assert.equal(getCurveOverviewModel().deltaText, "—");
});
