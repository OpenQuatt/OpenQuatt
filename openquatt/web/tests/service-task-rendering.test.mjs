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

test("autotune preserves failure and advice after CM100 ends and clears the task lock", () => {
  state.entities.cm100Active = { value: false };
  state.entities.commissioningStatus = { state: "CM100 STOPPED" };
  state.entities.flowAutotuneStatus = { state: "ABORT: NO_STEADY_STATE" };
  state.pendingFlowAutotuneStart = true;
  state.commissioningTaskLock = "autotune";
  const task = getSettingsServiceModel().tasks.find(task => task.key === "autotune");
  assert.equal(task.status, "Afgebroken: flow stabiliseert niet");
  assert.match(task.renderCard(), /Houd kleppen en pompen/);
  assert.doesNotMatch(task.renderCard(), /CM100 staat klaar/);
  assert.equal(state.pendingFlowAutotuneStart, false);
  assert.equal(state.commissioningTaskLock, "");
  assert.match(startButton(task, "flowAutotune"), /\bdisabled\b/);
});

test("autotune terminal reasons and advice translate without losing failure or enabling apply", () => {
  for (const [wire, expected] of [
    ["ABORT: FLOW_INVALID", "Afgebroken: flowmeting ongeldig"],
    ["FAILED: INVALID_GAIN", "Mislukt: geen bruikbare pomprespons"],
    ["REFUSED: BUSY", "Niet gestart: andere servicetaak actief"],
    ["ABORTED", "Afgebroken"],
    ["FAILED: <unsafe>", "Mislukt: <unsafe>"],
  ]) {
    state.entities.flowAutotuneStatus = { state: wire };
    const task = getSettingsServiceModel().tasks.find(task => task.key === "autotune");
    assert.equal(task.status, expected);
    assert.match(task.renderCard().match(/<button\b[^>]*data-oq-button-key="flowAutotuneApply"[^>]*>/)[0], /\bdisabled\b/);
    assert.doesNotMatch(task.renderCard(), /<unsafe>/);
  }
  state.entities.flowAutotuneStatus = { state: "ABORT: NO_STEADY_STATE" };
  setLocale("en", { persist: false, applyDocument: false, notify: false });
  let task = getSettingsServiceModel().tasks.find(task => task.key === "autotune");
  assert.equal(task.status, "Aborted: flow did not stabilise");
  assert.match(task.renderCard(), /Keep valves and pumps/);
  setLocale("nl", { persist: false, applyDocument: false, notify: false });
  task = getSettingsServiceModel().tasks.find(task => task.key === "autotune");
  assert.equal(task.status, "Afgebroken: flow stabiliseert niet");
});

test("autotune shows current gains and hides old suggestions until a result is ready", () => {
  state.entities.flowKp = { value: 0.03 };
  state.entities.flowKi = { value: 0.0008 };
  state.entities.flowKpSuggested = { value: 0.12345 };
  state.entities.flowKiSuggested = { value: 0.00678 };
  for (const wire of ["IDLE", "STEP", "STEP2", "ABORT: NO_STEADY_STATE"]) {
    state.entities.flowAutotuneStatus = { state: wire };
    const card = getSettingsServiceModel().tasks.find(task => task.key === "autotune").renderCard();
    assert.match(card, /Huidige Kp/);
    assert.match(card, /Huidige Ki/);
    assert.match(card, /0,03/);
    assert.match(card, /0,0008/);
    assert.doesNotMatch(card, /0,12345|0,00678/);
  }
  state.entities.flowAutotuneStatus = { state: "DONE (CLOSED-LOOP)" };
  const card = getSettingsServiceModel().tasks.find(task => task.key === "autotune").renderCard();
  assert.match(card, /0,12345/);
  assert.match(card, /0,00678/);
  delete state.entities.flowKp;
  delete state.entities.flowKi;
  assert.doesNotMatch(getSettingsServiceModel().tasks.find(task => task.key === "autotune").renderCard(), /0,03|0,0008/);
});

test("autotune missing or empty gains are unavailable while a real zero remains valid", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale, { persist: false, applyDocument: false, notify: false });
    state.entities.flowAutotuneStatus = { state: "DONE (CLOSED-LOOP)" };
    for (const key of ["flowKp", "flowKi", "flowKpSuggested", "flowKiSuggested"]) {
      const valueFor = () => {
        const card = getSettingsServiceModel().tasks.find(task => task.key === "autotune").renderCard();
        return card.match(new RegExp(`data-oq-settings-field="${key}"[\\s\\S]*?oq-settings-static-value">([^<]+)`))[1];
      };
      delete state.entities[key];
      assert.equal(valueFor(), "—", `${locale}/${key}/missing`);
      for (const raw of ["", "   ", null, "unknown", "NaN", "unavailable"]) {
        state.entities[key] = { value: raw };
        assert.equal(valueFor(), "—", `${locale}/${key}/${raw}`);
      }
      state.entities[key] = { value: 0 };
      assert.equal(valueFor(), "0", `${locale}/${key}/real zero`);
    }
  }
});

test("autotune identifies both step tests and hides the previous failure during a new start", () => {
  for (const [wire, expected] of [["STEP", "Staptest 1 van 2"], ["STEP2", "Staptest 2 van 2"]]) {
    state.entities.flowAutotuneStatus = { state: wire };
    assert.equal(getSettingsServiceModel().tasks.find(task => task.key === "autotune").status, expected);
  }
  setLocale("en", { persist: false, applyDocument: false, notify: false });
  assert.equal(getCommissioningProgressModel("STEP2", "autotune").phase, "Step test 2 of 2");
  state.entities.flowAutotuneStatus = { state: "FAILED: INVALID_GAIN" };
  state.busyAction = "flowAutotuneStart";
  const task = getSettingsServiceModel().tasks.find(task => task.key === "autotune");
  assert.equal(task.status, getCommissioningProgressModel("REQUESTED", "autotune").phase);
  assert.doesNotMatch(task.renderCard(), /Failed/);
});

test("autotune translates validation preparation and visibly identifies temporary gains", () => {
  for (const [locale, phase, kpLabel, kiLabel, explanation, currentLabel] of [
    ["nl", "Doelflow bereiken vóór validatie", "Tijdelijke test-Kp", "Tijdelijke test-Ki", "De eerdere instellingen worden na afloop hersteld", "Huidige Kp"],
    ["en", "Reaching target flow before validation", "Temporary test Kp", "Temporary test Ki", "The previous settings are restored afterwards", "Current Kp"],
  ]) {
    setLocale(locale, { persist: false, applyDocument: false, notify: false });
    state.entities.flowKp = { value: 0.0971 };
    state.entities.flowKi = { value: 0.0016 };
    state.entities.flowKpSuggested = { value: 0.12345 };
    for (const wire of ["VALIDATION_RECOVER", "VALIDATION_RETRY: UNDER_TARGET", "VALIDATION_RETRY: OVERSHOOT", "VALIDATING_SETTLING", "VALIDATING"]) {
      state.entities.flowAutotuneStatus = { state: wire };
      const task = getSettingsServiceModel().tasks.find(task => task.key === "autotune");
      if (/^VALIDATION_/.test(wire)) assert.equal(task.status, phase);
      const card = task.renderCard();
      assert.ok(card.includes(kpLabel) && card.includes(kiLabel), `${locale}/${wire}`);
      assert.ok(card.includes(explanation));
      assert.doesNotMatch(card, /VALIDATION_RECOVER|VALIDATION_RETRY|0[,.]12345/);
      assert.match(card.match(/<button\b[^>]*data-oq-button-key="flowAutotuneApply"[^>]*>/)[0], /\bdisabled\b/);
    }
    state.entities.flowAutotuneStatus = { state: "FAILED: VALIDATION_BASELINE" };
    const card = getSettingsServiceModel().tasks.find(task => task.key === "autotune").renderCard();
    assert.ok(card.includes(currentLabel));
    assert.ok(!card.includes(kpLabel));
    assert.ok(!card.includes(explanation));
  }
});
