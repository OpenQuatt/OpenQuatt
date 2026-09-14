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
const {
  getBoilerTestStatusCopy,
  getCommissioningProgressModel,
  getSettingsServiceModel,
  isBoilerTestResultReady,
  isCommissioningTaskStatusTerminal,
} = await import("../js/src/settings/service.js");

function setBoilerEntities(heatPower = "—", result = "—", confidence = "—", quality = "not available") {
  state.entities = {
    boilerHeatPower: { value: heatPower, state: heatPower },
    boilerPowerTestResult: { value: result, state: result },
    boilerPowerTestConfidence: { value: confidence, state: confidence },
    boilerPowerTestResultQuality: { value: quality, state: quality },
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

test("commissioning polling refreshes the active boiler-test flow target", () => {
  assert(SETTINGS_GROUP_KEY_MAP.service.includes("flowSetpoint"));
});

test("getBoilerTestStatusCopy BOILER_SETTLING shows flow", () => {
  const copy = getBoilerTestStatusCopy("BOILER_SETTLING", 805, 800);
  assert.match(copy, /Warmtevraag verstuurd/);
  assert.match(copy, /maximaal 150 sec/);
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
  setBoilerEntities("—", "2571 W", "92%", "OpenTherm measurement, ID15 capacity available");
  const copy = getBoilerTestStatusCopy("DONE: 2571W (conf 92%)", 800, 800);
  assert.equal(copy, "Klaar - 2571 W (92%). Ketel automatisch uit.");
  assert.doesNotMatch(copy, /ID15|empirisch/i);
});

test("getBoilerTestStatusCopy DONE remains compatible when quality entity is absent", () => {
  setBoilerEntities("—", "2571 W", "92%", "—");
  const copy = getBoilerTestStatusCopy("DONE: 2571W (conf 92%)", 800, 800);
  assert.equal(copy, "Klaar - 2571 W (92%). Ketel automatisch uit.");
});

test("empirical result requires an explicit second Apply within 30 seconds", () => {
  setBoilerEntities("—", "8442 W", "96%", "empirical, ID15 unavailable");
  const status = "CONFIRM_REQUIRED: confirm applying empirical result within 30s";
  assert(isCommissioningTaskStatusTerminal(status));
  assert(isBoilerTestResultReady(status));
  const copy = getBoilerTestStatusCopy(status, 800, 800);
  assert.equal(copy, "Bevestig binnen 30 seconden nogmaals dat je dit resultaat wilt toepassen.");
  assert.doesNotMatch(copy, /ID15|empirisch/i);
});

test("boiler-test card keeps result provenance out of the user interface", () => {
  state.loadingEntities = false;
  state.entities = {
    auxHeatSourcePresent: { value: true, state: "ON" },
    cm100Active: { value: true, state: "ON" },
    commissioningStatus: { value: "CM100 READY", state: "CM100 READY" },
    boilerPowerTestStatus: { value: "DONE: 5096W (conf 85%)", state: "DONE: 5096W (conf 85%)" },
    boilerHeatPower: { value: 3206.3, uom: "W" },
    boilerPowerTestResult: { value: 5096, uom: "W" },
    boilerPowerTestConfidence: { value: 85, uom: "%" },
    boilerPowerTestResultQuality: { value: "empirical, ID15 unavailable", state: "empirical, ID15 unavailable" },
    boilerPowerTestApply: { value: false, state: "OFF" },
  };

  const task = getSettingsServiceModel().tasks.find(({ key }) => key === "boiler");
  assert.equal(task.summary, "Meet het vermogen dat de cv-ketel afgeeft.");
  const markup = task.renderCard();
  assert.match(markup, /De test stabiliseert eerst de flow/);
  assert.doesNotMatch(markup, /Herkomst en kwaliteit|ID15|empirisch/i);
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

test("getBoilerTestStatusCopy explains an unstable power timeout", () => {
  const copy = getBoilerTestStatusCopy("FAILED: boiler power did not stabilise", 800, 800);
  assert.equal(copy, "Mislukt: het ketelvermogen werd niet stabiel binnen de testtijd.");
});

test("getBoilerTestStatusCopy REFUSED shows start geweigerd", () => {
  const copy = getBoilerTestStatusCopy("REFUSED: boiler/CV assist disabled", 800, 800);
  assert.equal(copy, "Start geweigerd: boiler/CV assist disabled");
  const copy2 = getBoilerTestStatusCopy("REFUSED", 800, 800);
  assert.equal(copy2, "Start geweigerd: REFUSED");
});

test("getBoilerTestStatusCopy explains a warm or unknown boiler refusal", () => {
  assert.equal(
    getBoilerTestStatusCopy("REFUSED: boiler temperature too high for test", 800, 800),
    "Start geweigerd: boiler temperature too high for test",
  );
  assert.equal(
    getBoilerTestStatusCopy("REFUSED: boiler temperature unavailable or stale", 800, 800),
    "Start geweigerd: boiler temperature unavailable or stale",
  );
});
