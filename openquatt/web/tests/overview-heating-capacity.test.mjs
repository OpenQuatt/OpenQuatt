import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { requestAnimationFrame: (callback) => callback(), localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { getOverviewStatusModel, getOverviewStrategySectionModel, renderOverviewStatusPanel } = await import("../js/src/views/overview.js");
const { setLocale } = await import("../js/src/i18n/index.js");

function setup(strategy, capacity) {
  state.entities = { strategy: { value: strategy, state: strategy } };
  if (capacity !== undefined) state.entities.hpCapacity = { value: capacity, state: String(capacity), uom: "W" };
}

test("Power House vergelijkt de warmtevraag met het geschatte maximum of de actuele levering", () => {
  setup("Power House", 8349.439);
  const controlMode = "CM2 - Heating - Heat Pump Only";
  Object.assign(state.entities, {
    openquattEnabled: { value: true },
    controlModeLabel: { value: controlMode },
    phouseReq: { value: 9085.5 },
    totalHeat: { value: 8233 },
  });

  try {
    setLocale("nl");
    const capacity = getOverviewStatusModel("Power House", controlMode);
    assert.equal(capacity.title, "Verwarmen");
    assert.equal(capacity.detail, "Warmtevraag boven geschat maximum");
    assert.equal(capacity.comparison, "9,09\u00a0kW gevraagd · 8,35\u00a0kW geschat maximum");
    assert.ok(renderOverviewStatusPanel("Power House", controlMode).includes(`<small>${capacity.comparison}</small>`));

    state.entities.phouseReq.value = 8000;
    state.entities.totalHeat.value = 7500;
    const output = getOverviewStatusModel("Power House", controlMode);
    assert.equal(output.detail, "Minder warmte geleverd dan gevraagd");
    assert.equal(output.comparison, "8,00\u00a0kW gevraagd · 7,50\u00a0kW geleverd");

    setLocale("en");
    assert.equal(getOverviewStatusModel("Power House", controlMode).comparison, "8.00\u00a0kW requested · 7.50\u00a0kW delivered");

    delete state.entities.hpCapacity;
    assert.equal(getOverviewStatusModel("Power House", controlMode).detail, "Heat output below demand");

    delete state.entities.phouseReq;
    state.entities.strategyRequestedPower = { value: 8000 };
    state.entities.totalHeat.value = 0;
    assert.equal(getOverviewStatusModel("Power House", controlMode).comparison, "8.00\u00a0kW requested · 0.00\u00a0kW delivered");

    delete state.entities.totalHeat;
    assert.equal(getOverviewStatusModel("Power House", controlMode).comparison, "");
  } finally {
    setLocale("nl");
  }
});

for (const strategy of ["Power House", "Water Temperature Control (heating curve)"]) {
  test(`${strategy}: geschatte capaciteit en onbekende waarden`, () => {
    setLocale("nl");
    for (const [value, expected] of [[6000, "6000 W"], [0, "0 W"], [undefined, "—"], [null, "—"], ["", "—"], ["nan", "—"], ["unavailable", "—"], [Infinity, "—"], [-1, "—"]]) {
      setup(strategy, value);
      const metric = getOverviewStrategySectionModel().metrics[2];
      assert.equal(metric.value, expected);
      assert.equal(metric.label, "Geschatte beschikbare warmtecapaciteit");
    }
  });
}

test("capaciteitsuitleg wisselt mee naar Engels", () => {
  setup("Water Temperature Control (heating curve)", 6000);
  setLocale("en");
  try {
    const metric = getOverviewStrategySectionModel().metrics[2];
    assert.equal(metric.label, "Estimated available heating capacity");
    assert.match(metric.note, /temperatures.*frequency.*current limits/);
  } finally {
    setLocale("nl");
  }
});

test("vermogensbijsturing omvat ook langer doorverwarmen en wisselt mee van taal", () => {
  setup("Power House", 6726);
  state.entities.phouseHouse = { value: 999 };
  state.entities.phouseReq = { value: 4001 };
  try {
    for (const [locale, label, note, value] of [
      ["nl", "Vermogensbijsturing", /langer doorverwarmen/, "+3.002 W"],
      ["en", "Demand adjustment", /extended heating/, "+3,002 W"],
    ]) {
      setLocale(locale);
      const metric = getOverviewStrategySectionModel().metrics[1];
      assert.equal(metric.label, label);
      assert.equal(metric.value, value);
      assert.match(metric.note, note);
    }
  } finally {
    setLocale("nl");
  }
});
