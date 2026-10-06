import assert from "node:assert/strict";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
  localStorage: {
    getItem: () => null,
  },
};

const bundle = await build({
  bundle: true, write: false, format: "esm", platform: "node", define: { __OQ_PREVIEW__: "false" },
  stdin: {
    resolveDir: fileURLToPath(new URL(".", import.meta.url)),
    contents: `
      export { state } from "../js/src/core/state.js";
      export { setLocale } from "../js/src/i18n/index.js";
      export { handleInput } from "../js/src/core/entity-actions.js";
      export { formatRunExtensionTemp, getRunExtensionStatusCopy, getRunExtensionThresholds,
        renderPowerHouseRunExtensionField, patchRunExtensionThresholds } from "../js/src/settings/heating.js";
    `,
  },
  plugins: [{ name: "test-assets", setup(plugin) {
    plugin.onResolve({ filter: /^virtual:embedded-assets$/ }, () => ({ path: "assets", namespace: "test-assets" }));
    plugin.onLoad({ filter: /.*/, namespace: "test-assets" }, () => ({
      contents: 'export const HP_GENERATION_IMAGE_V1 = "", HP_GENERATION_IMAGE_V2 = "", LOGO_MARKUP = "";',
    }));
  } }],
});
const { state, setLocale, handleInput, formatRunExtensionTemp, getRunExtensionStatusCopy,
  getRunExtensionThresholds, renderPowerHouseRunExtensionField, patchRunExtensionThresholds } =
  await import(`data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString("base64")}`);

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
    assert.match(markup, /Extended heating at minimum output/);
    assert.match(markup, /21\.0 °C/);
    assert.match(markup, /20\.8 °C/);
    assert.match(markup, /Waiting periods and safety protections may delay the start/);
    assert.doesNotMatch(markup, /Langer doorverwarmen|Uitgeschakeld/);
    assert.deepEqual(getRunExtensionThresholds(), before);
  } finally {
    setLocale("nl", { persist: false });
  }
  assert.match(renderPowerHouseRunExtensionField(), /Doorverwarmen op minimumvermogen/);
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
  assert.doesNotMatch(markup, /Doorverwarmen tot/);
});

test("ON toont stop-margin en afgeleide stop/herstart", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.5, "°C"),
    roomSetpoint: numberEntity(20.5, "°C"),
    phRunExtensionStatus: { value: "inactive", state: "inactive" },
  });
  const markup = renderPowerHouseRunExtensionField();
  assert.match(markup, /Doorverwarmen tot/);
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
  assert.equal(getRunExtensionStatusCopy("extending"), "Doorverwarmen op minimumvermogen");
  assert.equal(getRunExtensionStatusCopy("wait_warm_restart"), "Wacht op afkoeling");
  assert.equal(getRunExtensionStatusCopy("warm_restart"), "Warme herstart gevraagd");
  assert.equal(getRunExtensionStatusCopy("comfort_stop"), "Comfortstop");
});

test("herstart onder setpoint toont een negatieve relatieve marge", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.1, "°C"),
  });
  assert.match(renderPowerHouseRunExtensionField(), /setpoint − 0,1 °C/);
});

test("configureerbare herstart toont de effectieve koude comfortgrens met voorbeelden", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.7, "°C"),
    phRunExtensionRestartCooldown: numberEntity(1.2, "°C"),
    phComfortBelow: numberEntity(0.2, "°C"),
    roomSetpoint: numberEntity(21, "°C"),
  });
  const thresholds = getRunExtensionThresholds();
  assert.equal(thresholds.stop.toFixed(1), "21.7");
  assert.equal(thresholds.configuredRestart.toFixed(1), "20.5");
  assert.equal(thresholds.restart.toFixed(1), "20.8");
  assert.equal(thresholds.limited, true);
  const markup = renderPowerHouseRunExtensionField();
  assert.match(markup, /Afkoelen vóór warme herstart/);
  assert.match(markup, /21,7 °C en 0,2 °C/);
  assert.match(markup, /Je instelling geeft 20,5 °C.*20,8 °C/);
  assert.match(markup, /data-info-id="phRunExtensionRestartCooldown"/);
  setLocale("en", { persist: false });
  try {
    assert.match(renderPowerHouseRunExtensionField(), /Cooling before warm restart/);
    assert.match(renderPowerHouseRunExtensionField(), /20\.5 °C.*20\.8 °C/);
  } finally { setLocale("nl", { persist: false }); }
});

test("default, setpoint-herstart en halve comfortstappen volgen dezelfde berekening", () => {
  resetSettingsState({
    phRunExtensionStopMargin: numberEntity(0.7),
    phRunExtensionRestartCooldown: numberEntity(0.2),
    phComfortBelow: numberEntity(0.15),
    roomSetpoint: numberEntity(21),
  });
  assert.equal(getRunExtensionThresholds().restart.toFixed(1), "21.5");
  state.entities.phRunExtensionRestartCooldown.value = 0.7;
  assert.equal(getRunExtensionThresholds().restart, 21);
  state.entities.phRunExtensionRestartCooldown.value = 1.2;
  assert.equal(formatRunExtensionTemp(getRunExtensionThresholds().restart), "20,85 °C");
  state.drafts.phRunExtensionRestartCooldown = 0.2;
  assert.equal(getRunExtensionThresholds().restart.toFixed(1), "21.5");
});

test("nieuwe firmware toont geen verzonnen herstart bij ontbrekende instellingen", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionRestartCooldown: numberEntity(0.2),
    phRunExtensionStopMargin: numberEntity(0.5),
    roomSetpoint: numberEntity(21),
  });
  assert.ok(Number.isNaN(getRunExtensionThresholds().restart));
  state.entities.phComfortBelow = numberEntity("");
  assert.ok(Number.isNaN(getRunExtensionThresholds().restart));
  state.entities.phComfortBelow = numberEntity(0.2);
  state.entities.phRunExtensionRestartCooldown.value = "";
  assert.ok(Number.isNaN(getRunExtensionThresholds().restart));
});

test("oude firmware gebruikt haar vaste afstand zonder nieuwe koude-grensbegrenzing", () => {
  resetSettingsState({
    phRunExtensionStopMargin: numberEntity(0.1),
    phComfortBelow: numberEntity(0),
    roomSetpoint: numberEntity(21),
  });
  assert.equal(getRunExtensionThresholds().restart.toFixed(1), "20.9");
  state.entities.phRunExtensionRestartCooldown = numberEntity(0.2);
  assert.equal(getRunExtensionThresholds().restart, 21);
});


test("input en live patches houden herstartvoorvertoning actueel zonder de invoer te vervangen", async () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.7),
    phRunExtensionRestartCooldown: numberEntity(0.2),
    phComfortBelow: numberEntity(0.2),
    roomSetpoint: numberEntity(21),
  });
  const preview = { innerHTML: "" };
  const previousRoot = state.root;
  state.root = { querySelector(selector) {
    assert.equal(selector, "[data-oq-run-extension-thresholds]");
    return preview;
  } };
  try {
    const input = { dataset: { oqField: "phRunExtensionRestartCooldown" }, type: "number", value: "1.2" };
    handleInput({ target: input });
    assert.match(preview.innerHTML, /20,5 °C.*20,8 °C/s);
    assert.equal(input.value, "1.2");
    state.entities.roomSetpoint.value = 22;
    patchRunExtensionThresholds();
    assert.match(preview.innerHTML, /21,5 °C.*21,8 °C/s);
    input.value = "";
    handleInput({ target: input });
    assert.ok(Number.isNaN(getRunExtensionThresholds().restart));
    assert.doesNotMatch(preview.innerHTML, /Je instelling geeft/);
    state.inputDrafts = {};
    state.drafts = {};
    state.entities.roomSetpoint.value = "";
    patchRunExtensionThresholds();
    assert.ok(Number.isNaN(getRunExtensionThresholds().setpoint));
  } finally { state.root = previousRoot; }
});
