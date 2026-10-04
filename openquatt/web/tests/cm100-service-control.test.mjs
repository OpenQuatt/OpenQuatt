import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { renderSettingsServiceSection } = await import("../js/src/settings/service.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const initial = structuredClone(state);

test.beforeEach(() => {
  Object.assign(state, structuredClone(initial));
  setLocale("nl", { persist: false });
  state.loadingEntities = false;
  state.entities.commissioningCm100Start = { value: false };
  state.entities.commissioningCm100Stop = { value: false };
});

function setCm100(active, status) {
  state.entities.cm100Active = { value: active };
  state.entities.commissioningStatus = { value: status, state: status };
}

function button(key, label, disabled = false) {
  const html = renderSettingsServiceSection();
  const buttons = html.match(/<button\b[^>]*data-oq-button-key="commissioningCm100(?:Start|Stop)"[^>]*>[\s\S]*?<\/button>/g) || [];
  assert.equal(buttons.length, 1);
  assert.match(buttons[0], new RegExp(`data-oq-button-key="${key}"`));
  assert.match(buttons[0], /oq-helper-button--primary/);
  assert.ok(buttons[0].includes(label));
  assert.equal(/\bdisabled\b/.test(buttons[0]), disabled);
}

test("inactive service offers one primary start action, including stopped and refused status", () => {
  for (const status of ["IDLE", "CM100 STOPPED", "REFUSED: CM Override must be Auto", "FAILED"]) {
    setCm100(false, status);
    button("commissioningCm100Start", "Service starten");
  }
});

test("active service offers stop regardless of task or refusal text", () => {
  for (const status of ["CM100 READY", "MANUAL HP ACTIVE", "BOILER TEST STARTED", "REFUSED: BUSY", "IDLE"]) {
    setCm100(true, status);
    button("commissioningCm100Stop", "Service stoppen");
  }
});

test("pending start blocks a second request before and after the HTTP response", () => {
  setCm100(false, "IDLE");
  state.pendingCommissioningCm100Start = true;
  state.commissioningTaskLock = "cm100";
  state.busyAction = "commissioningCm100Start";
  button("commissioningCm100Start", "Service starten…", true);
  state.busyAction = "";
  button("commissioningCm100Start", "Service starten…", true);
  setCm100(false, "WAITING_FOR_CM100");
  button("commissioningCm100Start", "Service starten…", true);
  setCm100(true, "CM100 READY");
  renderSettingsServiceSection(); // Existing lock reconciliation completes on the next render.
  button("commissioningCm100Stop", "Service stoppen");
});

test("stop stays a stop action until the device confirms exit", () => {
  setCm100(true, "CM100 READY");
  state.busyAction = "commissioningCm100Stop";
  button("commissioningCm100Stop", "Service stoppen…", true);
  state.busyAction = "";
  setCm100(true, "ABORT REQUESTED");
  button("commissioningCm100Stop", "Service stoppen…", true);
  setCm100(false, "IDLE");
  button("commissioningCm100Start", "Service starten");
});

test("failed request leaves the action selected from the actual device state", () => {
  setCm100(true, "CM100 READY");
  state.controlError = "HTTP 503";
  button("commissioningCm100Stop", "Service stoppen");
  setCm100(false, "IDLE");
  button("commissioningCm100Start", "Service starten");
});

test("unknown and loading state cannot start service", () => {
  button("commissioningCm100Start", "Service starten", true);
  for (const value of ["UNKNOWN", "UNAVAILABLE", null]) {
    setCm100(value, "CM100 READY");
    button("commissioningCm100Stop", "Service stoppen", true);
  }
  setCm100(false, "IDLE");
  state.loadingEntities = true;
  button("commissioningCm100Start", "Service starten", true);
});

test("missing action never substitutes the opposite write", () => {
  setCm100(true, "CM100 READY");
  delete state.entities.commissioningCm100Stop;
  button("commissioningCm100Stop", "Service stoppen", true);
  delete state.entities.commissioningCm100Start;
  assert.doesNotMatch(renderSettingsServiceSection(), /data-oq-button-key="commissioningCm100/);
});

test("legacy status fallback requires an explicit known status", () => {
  delete state.entities.cm100Active;
  for (const [status, key] of [["CM100 READY", "Stop"], ["CM100 STOPPED", "Start"], ["IDLE", "Start"]]) {
    state.entities.commissioningStatus = { value: status };
    button(`commissioningCm100${key}`, key === "Stop" ? "Service stoppen" : "Service starten");
  }
  state.entities.commissioningStatus = { value: "REFUSED" };
  button("commissioningCm100Start", "Service starten", true);
});

test("locale changes translate the current action and pending labels without writes", () => {
  setCm100(true, "CM100 READY");
  setLocale("en", { persist: false });
  button("commissioningCm100Stop", "Stop service");
  state.busyAction = "commissioningCm100Stop";
  button("commissioningCm100Stop", "Stopping service…", true);
  state.busyAction = "";
  setCm100(false, "IDLE");
  button("commissioningCm100Start", "Start service");
  state.busyAction = "commissioningCm100Start";
  button("commissioningCm100Start", "Starting service…", true);
  setLocale("nl", { persist: false });
  button("commissioningCm100Start", "Service starten…", true);
});


test("active but waiting CM100 preserves the existing stop gate", () => {
  setCm100(true, "WAITING_FOR_CM100");
  button("commissioningCm100Stop", "Service stoppen", true);
});


test("binary CM100 representations select the action consistently", () => {
  for (const value of [true, "ON", " true ", 1, "1"]) {
    setCm100(value, "IDLE");
    button("commissioningCm100Stop", "Service stoppen");
  }
  for (const value of [false, "OFF", " false ", 0, "0"]) {
    setCm100(value, "IDLE");
    button("commissioningCm100Start", "Service starten");
  }
});

test("an unknown CM100 entity never enables the legacy status fallback", () => {
  for (const entity of [{}, { value: "UNKNOWN" }, { value: "UNAVAILABLE" }, { value: "invalid" }, { state: "UNKNOWN", value: "on" }]) {
    for (const status of ["IDLE", "CM100 STOPPED", "CM100 READY"]) {
      setCm100(false, status);
      state.entities.cm100Active = entity;
      const active = status === "CM100 READY";
      button(active ? "commissioningCm100Stop" : "commissioningCm100Start", active ? "Service stoppen" : "Service starten", true);
    }
  }
});
