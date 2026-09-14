import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { getSettingsServiceModel } = await import("../js/src/settings/service.js");
const initial = structuredClone(state);
const tasks = [
  ["boiler", "boilerPowerTest"], ["autotune", "flowAutotune"], ["purge", "airPurge"],
  ["manual-flow", "manualFlow"], ["manual-hp", "manualHp"], ["hp-water-calibration", "hpWaterCalibration"],
];

function reset() {
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
