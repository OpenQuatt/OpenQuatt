import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
  localStorage: { getItem: () => null },
};

const { state } = await import("../js/src/core/state.js");
const { SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");
const { getBoilerTestStatusCopy, getCommissioningProgressModel } = await import("../js/src/settings/service.js");

function setBoilerEntities(heatPower = "—", result = "—", confidence = "—") {
  state.entities = {
    boilerHeatPower: { value: heatPower, state: heatPower },
    boilerPowerTestResult: { value: result, state: result },
    boilerPowerTestConfidence: { value: confidence, state: confidence },
  };
}

test("boiler progress model uses user-friendly phases", () => {
  assert.equal(getCommissioningProgressModel("FLOW_SETTLING", "boiler").phase, "Flow stabiliseren");
  assert.equal(getCommissioningProgressModel("BOILER_SETTLING", "boiler").phase, "Ketel starten");
  assert.equal(getCommissioningProgressModel("MEASURING", "boiler").phase, "Vermogen meten");
  assert.equal(getCommissioningProgressModel("COOLDOWN", "boiler").phase, "Test afronden");
  assert.equal(getCommissioningProgressModel("DONE: 2571W (conf 92%)", "boiler").phase, "Klaar");
  assert.equal(getCommissioningProgressModel("ABORTED", "boiler").phase, "Afgebroken");
});

test("getBoilerTestStatusCopy FLOW_SETTLING shows target and 2 min hint", () => {
  setBoilerEntities();
  const copy = getBoilerTestStatusCopy("FLOW_SETTLING", 812, 800);
  assert.match(copy, /Flow naar 800 L\/h/);
  assert.match(copy, /Min\. 2 min/);
  assert.match(copy, /Nu 812 L\/h/);
});

test("commissioning polling refreshes the temporary boiler-test flow target", () => {
  assert(SETTINGS_GROUP_KEY_MAP.service.includes("flowSetpoint"));
});

test("getBoilerTestStatusCopy BOILER_SETTLING shows flow", () => {
  const copy = getBoilerTestStatusCopy("BOILER_SETTLING", 805, 800);
  assert.match(copy, /Warmtevraag verstuurd/);
  assert.match(copy, /Flow 805 L\/h/);
});

test("getBoilerTestStatusCopy MEASURING shows heat", () => {
  setBoilerEntities("2057 W", "—", "—");
  const copy = getBoilerTestStatusCopy("MEASURING", 807, 800);
  assert.match(copy, /Ketel actief; meten/);
  assert.match(copy, /Nu 2057 W/);
});

test("getBoilerTestStatusCopy COOLDOWN shows result", () => {
  setBoilerEntities("—", "2571 W", "92%");
  const copy = getBoilerTestStatusCopy("COOLDOWN", 808, 800);
  assert.match(copy, /Metingen klaar; ketel uit/);
});

test("getBoilerTestStatusCopy DONE shows result and confidence", () => {
  setBoilerEntities("—", "2571 W", "92%");
  const copy = getBoilerTestStatusCopy("DONE: 2571W (conf 92%)", 800, 800);
  assert.match(copy, /Klaar - 2571 W/);
  assert.match(copy, /92%/);
});

test("getBoilerTestStatusCopy exact ABORTED is handmatig", () => {
  const copy = getBoilerTestStatusCopy("ABORTED", 800, 800);
  assert.equal(copy, "Handmatig gestopt. Flow en ketel zijn hersteld naar vorige instelling.");
});

test("getBoilerTestStatusCopy ABORTED: CM100 exited is afgebroken with reason", () => {
  const copy = getBoilerTestStatusCopy("ABORTED: CM100 exited", 800, 800);
  assert.equal(copy, "Afgebroken: CM100 exited");
});

test("getBoilerTestStatusCopy FAILED shows mislukt with reason", () => {
  const copy = getBoilerTestStatusCopy("FAILED: boiler active state not confirmed", 800, 800);
  assert.equal(copy, "Mislukt: boiler active state not confirmed");
});

test("getBoilerTestStatusCopy REFUSED shows start geweigerd", () => {
  const copy = getBoilerTestStatusCopy("REFUSED: boiler/CV assist disabled", 800, 800);
  assert.equal(copy, "Start geweigerd: boiler/CV assist disabled");
  const copy2 = getBoilerTestStatusCopy("REFUSED", 800, 800);
  assert.equal(copy2, "Start geweigerd: REFUSED");
});
