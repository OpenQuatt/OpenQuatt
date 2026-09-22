import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };

const { state } = await import("../js/src/core/state.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const { getControlWorkingBlockingReasons } = await loadReplayHarness(`${source}\nexport { getControlWorkingBlockingReasons };`);

function valueEntity(value, uom = "") {
  return { value, state: value == null ? "nan" : String(value), uom };
}

function setEntities(entities) {
  state.drafts = {};
  state.inputDrafts = {};
  state.entities = entities;
}

const powerHouseEntities = {
  strategyActiveCode: valueEntity(3),
  lowLoadLatch: { value: "OFF", state: "OFF" },
  lowLoadOnW: valueEntity(2200, "W"),
  lowLoadOffW: valueEntity(1600, "W"),
};

test("soft_guard toont de waterlimiet, het flowdoel en de laaglastband", () => {
  setEntities({
    ...powerHouseEntities,
    maxWater: valueEntity(60, "°C"),
    supplyTemp: valueEntity(24.3, "°C"),
    flowSetpoint: valueEntity(900, "L/h"),
    flowSelected: valueEntity(0, "L/h"),
  });

  const reasons = getControlWorkingBlockingReasons({ primaryReason: "soft_guard" });
  assert.deepEqual(reasons, [
    "Veilige marge bewaakt: maximaal water 60 °C (aanvoer nu 24.3 °C); flowdoel 900 L/h (actueel 0 L/h)",
    "Laaglastband: uit onder 1600 W, terugstart vanaf 2200 W",
  ]);
});

test("soft_guard zonder limietdata valt terug op de algemene tekst en toont geen dubbele laaglastmelding", () => {
  setEntities(powerHouseEntities);

  const reasons = getControlWorkingBlockingReasons({ primaryReason: "soft_guard" });
  assert.equal(reasons[0], "Veilige marge bewaakt: systeem begrenst zichzelf binnen temperatuur- en flowgrenzen");
  assert.match(reasons[1], /^Laaglastband: uit onder 1600 W, terugstart vanaf 2200 W$/);

  setEntities({ ...powerHouseEntities, lowLoadLatch: { value: "ON", state: "ON" } });
  const whileLatchActive = getControlWorkingBlockingReasons({ primaryReason: "soft_guard" });
  assert.equal(whileLatchActive.length, 2);
  assert.doesNotMatch(whileLatchActive.join(", "), /Laaglastband/);
  assert.match(whileLatchActive[1], /^Low-load beveiliging actief/);
});

test("geen warmtevraag in Power House toont kamer, buiten en de terugstartdrempel", () => {
  setEntities({
    ...powerHouseEntities,
    roomTemp: valueEntity(19.49, "°C"),
    roomSetpoint: valueEntity(19.5, "°C"),
    outsideTempSelected: valueEntity(13.58, "°C"),
    phouseHouse: valueEntity(1108, "W"),
  });

  const [reason] = getControlWorkingBlockingReasons({});
  assert.equal(
    reason,
    "Geen warmtevraag: kamer 19.5 °C (setpoint 19.5 °C), buiten 13.6 °C (huismodel vraagt ~1108 W). " +
      "Het systeem start pas boven de 2200 W stookgrens (uit onder 1600 W), of als de kamer duidelijk onder het setpoint zakt."
  );
});

test("geen warmtevraag valt terug op de generieke tekst buiten Power House of zonder sensordata", () => {
  setEntities({ strategyActiveCode: valueEntity(2) });
  assert.deepEqual(getControlWorkingBlockingReasons({}), ["Geen warmtevraag: het systeem wacht op nieuwe vraag"]);

  setEntities({ strategyActiveCode: valueEntity(3) });
  assert.deepEqual(getControlWorkingBlockingReasons({}), ["Geen warmtevraag: het systeem wacht op nieuwe vraag"]);
});