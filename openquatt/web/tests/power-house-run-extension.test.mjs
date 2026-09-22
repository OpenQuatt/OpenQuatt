import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const {
  formatRunExtensionTemp,
  getRunExtensionStatusCopy,
  getRunExtensionThresholds,
  renderPowerHouseRunExtensionField,
} = await import("../js/src/settings/heating.js");

function numberEntity(value, uom = "", extra = {}) {
  return {
    value,
    state: String(value),
    min_value: 0,
    max_value: 10000,
    step: 0.1,
    uom,
    ...extra,
  };
}

function switchEntity(enabled) {
  return { value: enabled, state: enabled };
}

function resetSettingsState(entities = {}) {
  state.entities = entities;
  state.drafts = {};
  state.inputDrafts = {};
  state.settingsAdvancedOpen = {};
  state.loadingEntities = false;
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.systemModal = "";
  state.pendingControlModeOverride = "";
}

test("oude firmware zonder run-extension entities verbergt de card", () => {
  resetSettingsState({});
  assert.equal(renderPowerHouseRunExtensionField(), "");
});

test("switch aanwezig toont de card, OFF verbergt stop-margin", () => {
  resetSettingsState({ phRunExtension: switchEntity(false) });
  const markup = renderPowerHouseRunExtensionField();
  assert.match(markup, /Langer doorverwarmen/);
  assert.match(markup, /na een comfortstop kan Power House bij nieuwe warmtevraag wel herstarten/);
  assert.doesNotMatch(markup, /Stop boven gewenste temperatuur/);
});

test("ON toont stop-margin en afgeleide stop/herstart", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.5, "°C"),
    roomSetpoint: numberEntity(20.5, "°C"),
    phRunExtensionStatus: { value: "inactive", state: "inactive" },
  });
  const markup = renderPowerHouseRunExtensionField();
  assert.match(markup, /Stop boven gewenste temperatuur/);
  assert.match(markup, /21,0 °C/);
  assert.match(markup, /20,8 °C/);
});

test("setpoint 20.5 + margin 0.5 geeft stop 21.0 en herstart 20.8", () => {
  resetSettingsState({
    roomSetpoint: numberEntity(20.5, "°C"),
    phRunExtensionStopMargin: numberEntity(0.5, "°C"),
  });
  const thresholds = getRunExtensionThresholds();
  assert.equal(thresholds.stop.toFixed(1), "21.0");
  assert.equal(thresholds.restart.toFixed(1), "20.8");
  assert.equal(formatRunExtensionTemp(thresholds.stop), "21,0 °C");
});

test("zonder setpoint geen verzonnen absolute waarde", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.5, "°C"),
  });
  const markup = renderPowerHouseRunExtensionField();
  assert.match(markup, /setpoint \+ 0,5 °C/);
  assert.doesNotMatch(markup, /21,0 °C/);
});

test("status extending en wait_warm_restart geven juiste tekst", () => {
  assert.equal(getRunExtensionStatusCopy("extending"), "Langer doorverwarmen actief");
  assert.equal(getRunExtensionStatusCopy("wait_warm_restart"), "Wacht op afkoeling");
  assert.equal(getRunExtensionStatusCopy("warm_restart"), "Warme herstart");
  assert.equal(getRunExtensionStatusCopy("comfort_stop"), "Comfortstop");
});
