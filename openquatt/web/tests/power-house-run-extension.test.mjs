import assert from "node:assert/strict";
import test from "node:test";
import { setLocale } from "../js/src/i18n/index.js";

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

test("run-extension copy and temperatures follow locale changes without changing thresholds", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.5, "°C"),
    roomSetpoint: numberEntity(20.5, "°C"),
    phRunExtensionStatus: { value: "extending", state: "extending" },
  });
  const before = getRunExtensionThresholds();
  try {
    setLocale("en", { persist: false });
    const markup = renderPowerHouseRunExtensionField();
    assert.match(markup, /Extended heating active/);
    assert.match(markup, /21\.0 °C/);
    assert.match(markup, /20\.8 °C/);
    assert.match(markup, /Waiting periods and safety protections may delay the start/);
    assert.doesNotMatch(markup, /Langer doorverwarmen|Uitgeschakeld/);
    assert.deepEqual(getRunExtensionThresholds(), before);
  } finally {
    setLocale("nl", { persist: false });
  }
  assert.match(renderPowerHouseRunExtensionField(), /Langer doorverwarmen actief/);
});

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
  assert.match(markup, /Deze schakelaar laat hem niet meteen starten/);
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
  assert.match(markup, /Wacht op een verwarmingsrun/);
  assert.doesNotMatch(markup, /Uitgeschakeld/);
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

test("herstart onder setpoint toont een negatieve relatieve marge", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.1, "°C"),
  });
  assert.match(renderPowerHouseRunExtensionField(), /setpoint − 0,1 °C/);
});
