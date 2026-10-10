import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { requestAnimationFrame: (callback) => callback(), localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { getOverviewStrategySectionModel } = await import("../js/src/views/overview.js");
const { setLocale } = await import("../js/src/i18n/index.js");

function setup(strategy, capacity) {
  state.entities = { strategy: { value: strategy, state: strategy } };
  if (capacity !== undefined) state.entities.hpCapacity = { value: capacity, state: String(capacity), uom: "W" };
}

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
