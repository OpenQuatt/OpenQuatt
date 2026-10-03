import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { getSettingsServiceModel, getCommissioningProgressModel, getAirPurgeTerminalStatus } = await import("../js/src/settings/service.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const initial = structuredClone(state);
const tasks = [
  ["boiler", "boilerPowerTest"], ["autotune", "flowAutotune"], ["purge", "airPurge"],
  ["manual-flow", "manualFlow"], ["manual-hp", "manualHp"], ["hp-water-calibration", "hpWaterCalibration"],
];

function reset() {
  setLocale("nl", { persist: false, applyDocument: false, notify: false });
  Object.assign(state, structuredClone(initial));
  state.loadingEntities = false;
  const entity = (key, value) => { state.entities[key] = { value, state: value }; };
  entity("cm100Active", true);
  entity("commissioningStatus", "CM100 READY");
  entity("auxHeatSourcePresent", true);
  for (const [, prefix] of tasks) {
    entity(prefix + "Status", "IDLE");
    for (const suffix of ["Start", "Abort", "Apply"]) entity(prefix + suffix, false);
  }
}
test.beforeEach(reset);

function startButton(task, prefix) {
  const button = task.renderCard().match(new RegExp(`<button\\b[^>]*data-oq-button-key="${prefix}Start"[^>]*>`));
  assert.ok(button, `${task.key} start button exists`);
  return button[0];
}

test("service task list skips hidden modal controls and only the selected card builds them", () => {
  let reads = 0;
  Object.defineProperty(state.entities, "manualFlowTargetIpwm", {
    get() { reads++; return { value: 42 }; }, configurable: true,
  });
  const model = getSettingsServiceModel();
  assert.equal(model.tasks.length, 6);
  assert.equal(reads, 0);
  model.tasks.find(task => task.key === "boiler").renderCard();
  assert.equal(reads, 0);
  model.tasks.find(task => task.key === "manual-flow").renderCard();
  assert.ok(reads > 0);
});

test("each service task blocks other starts while pending, locked or running", () => {
  for (const [activeKey, activePrefix] of tasks) {
    for (const phase of ["pending", "locked", "running"]) {
      reset();
      if (phase === "locked") state.commissioningTaskLock = activeKey;
      if (phase === "pending") state[`pending${activePrefix[0].toUpperCase()}${activePrefix.slice(1)}Start`] = true;
      if (phase === "running") state.entities[activePrefix + "Status"] = { state: "RUNNING", value: "RUNNING" };
      const model = getSettingsServiceModel();
      for (const [key, prefix] of tasks) {
        if (key !== activeKey) assert.match(startButton(model.tasks.find(task => task.key === key), prefix), /\bdisabled\b/, `${activeKey}/${phase} blocks ${key}`);
      }
    }
  }
});

test("service start controls remain disabled until CM100 is ready", () => {
  state.entities.cm100Active = { value: false };
  state.entities.commissioningStatus = { state: "IDLE", value: "IDLE" };
  const model = getSettingsServiceModel();
  for (const [key, prefix] of tasks) assert.match(startButton(model.tasks.find(task => task.key === key), prefix), /\bdisabled\b/);
});

test("completed task clears pending and lock state without rendering any modal", () => {
  for (const [key, prefix] of tasks) {
    reset();
    const pendingKey = `pending${prefix[0].toUpperCase()}${prefix.slice(1)}Start`;
    state[pendingKey] = true;
    state.commissioningTaskLock = key;
    state.entities[prefix + "Status"] = { value: "DONE", state: "DONE" };
    getSettingsServiceModel();
    assert.equal(state[pendingKey], false, key);
    assert.equal(state.commissioningTaskLock, "", key);
  }
});

test("purge failure remains visible after CM100 ends and overrides a stale phase", () => {
  state.entities.cm100Active = { value: false };
  state.entities.commissioningStatus = { state: "CM100 STOPPED" };
  state.entities.airPurgeStatus = { state: "FAILED: no flow detected" };
  state.entities.airPurgePhase = { value: 2 };
  const task = getSettingsServiceModel().tasks.find(task => task.key === "purge");
  assert.equal(task.status, "Mislukt: geen flow gedetecteerd");
  const card = task.renderCard();
  assert.match(card, /Mislukt/);
  assert.doesNotMatch(card, /Pomp-pulsen/);
  assert.match(startButton(task, "airPurge"), /\bdisabled\b/);
});

test("purge terminal outcomes are distinct and translated when the locale changes", () => {
  assert.equal(getCommissioningProgressModel("FAILED: no flow detected", "purge").phase, "Mislukt");
  assert.equal(getAirPurgeTerminalStatus("ABORTED"), "Afgebroken");
  assert.equal(getAirPurgeTerminalStatus("ABORT: not CM100"), "Afgebroken: CM100 niet actief");
  assert.equal(getAirPurgeTerminalStatus("REFUSED: boiler active"), "Niet gestart: ketel actief");
  setLocale("en", { persist: false, applyDocument: false, notify: false });
  assert.equal(getAirPurgeTerminalStatus("FAILED: no flow detected"), "Failed: no flow detected");
  assert.equal(getAirPurgeTerminalStatus("ABORTED"), "Aborted");
  assert.equal(getAirPurgeTerminalStatus("ABORT: not CM100"), "Aborted: CM100 not active");
  assert.equal(getAirPurgeTerminalStatus("FAILED: new firmware reason"), "Failed: new firmware reason");
});

test("purge renders requested iPWM only when supplied by the firmware and escapes unknown reasons", () => {
  state.entities.airPurgeStatus = { state: "FAILED: <unsafe>" };
  let card = getSettingsServiceModel().tasks.find(task => task.key === "purge").renderCard();
  assert.match(card, /&lt;unsafe&gt;/);
  assert.doesNotMatch(card, /Gevraagde iPWM/);
  state.entities.airPurgeTargetIpwm = { value: 800, state: "800" };
  card = getSettingsServiceModel().tasks.find(task => task.key === "purge").renderCard();
  assert.match(card, /Gevraagde iPWM/);
  assert.match(card, /800/);
});

test("a new purge start hides the previous failure while its request is in flight", () => {
  state.entities.airPurgeStatus = { state: "FAILED: no flow detected" };
  state.busyAction = "airPurgeStart";
  const task = getSettingsServiceModel().tasks.find(task => task.key === "purge");
  assert.equal(task.status, getCommissioningProgressModel("REQUESTED", "purge").phase);
  assert.doesNotMatch(task.renderCard(), /Mislukt/);
});
