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
        renderPowerHouseRunExtensionField, renderPowerHouseAdvancedField, renderPowerHouseComfortThresholds, patchRunExtensionThresholds } from "../js/src/settings/heating.js";
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
  getRunExtensionThresholds, renderPowerHouseRunExtensionField, renderPowerHouseAdvancedField, renderPowerHouseComfortThresholds, patchRunExtensionThresholds } =
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
    assert.match(markup, /Waiting periods may delay the start/);
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
  assert.match(markup, /Inschakelen start een stilstaande warmtepomp niet zelfstandig/);
  assert.doesNotMatch(markup, /Stopgrens boven gewenste temperatuur/);
});

test("ON toont stop-margin en afgeleide stop/herstart", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.5, "°C"),
    roomSetpoint: numberEntity(20.5, "°C"),
    phRunExtensionStatus: { value: "inactive", state: "inactive" },
  });
  const markup = renderPowerHouseRunExtensionField();
  assert.match(markup, /Stopgrens boven gewenste temperatuur/);
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
  assert.doesNotMatch(markup, /<strong>21,0 °C/);
});

test("status extending en wait_warm_restart geven juiste tekst", () => {
  assert.equal(getRunExtensionStatusCopy("extending"), "Doorverwarmen op minimumvermogen");
  assert.equal(getRunExtensionStatusCopy("wait_warm_restart"), "Wacht op afkoeling");
  assert.equal(getRunExtensionStatusCopy("warm_restart"), "Herstart aangevraagd");
  assert.equal(getRunExtensionStatusCopy("comfort_stop"), "Stop aangevraagd");
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
  assert.match(markup, /Afkoeling vóór opnieuw verwarmen/);
  assert.match(markup, /21,7 °C min 0,2 °C/);
  assert.match(markup, /je invoer komt uit op 20,5 °C.*20,8 °C/);
  assert.match(markup, /data-info-id="phRunExtensionRestartCooldown"/);
  setLocale("en", { persist: false });
  try {
    assert.match(renderPowerHouseRunExtensionField(), /Cooling before heating again/);
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
  const comfort = { innerHTML: "" };
  const previousRoot = state.root;
  state.root = { querySelector(selector) {
    if (selector === "[data-oq-power-house-comfort-thresholds]") return comfort;
    if (selector === "[data-oq-run-extension-status]") return null;
    assert.equal(selector, "[data-oq-run-extension-thresholds]");
    return preview;
  } };
  try {
    const input = { dataset: { oqField: "phRunExtensionRestartCooldown" }, type: "number", value: "1.2" };
    handleInput({ target: input });
    assert.match(preview.innerHTML, /20,5 °C.*20,8 °C/s);
    assert.match(preview.innerHTML, /Voorvertoning — wijziging nog niet bevestigd/);
    assert.match(preview.innerHTML, /Nog bevestigd: stop aanvragen bij 21,7 °C.*21,5 °C/);
    assert.equal(input.value, "1.2");
    state.entities.roomSetpoint.value = 22;
    patchRunExtensionThresholds();
    assert.match(preview.innerHTML, /21,5 °C.*21,8 °C/s);
    input.value = "";
    handleInput({ target: input });
    assert.ok(Number.isNaN(getRunExtensionThresholds().restart));
    assert.doesNotMatch(preview.innerHTML, /je invoer komt uit op/);
    state.inputDrafts = {};
    state.drafts = {};
    state.entities.roomSetpoint.value = "";
    patchRunExtensionThresholds();
    assert.ok(Number.isNaN(getRunExtensionThresholds().setpoint));
  } finally { state.root = previousRoot; }
});

test("comfortbediening scheidt dagelijkse keuzes en behouden geavanceerde afstelling", () => {
  resetSettingsState({
    roomSetpoint: numberEntity(21),
    phComfortBelow: numberEntity(0.2),
    phComfortAbove: numberEntity(0.3),
    phKp: numberEntity(3000),
    phResponseProfile: { value: "Balanced", state: "Balanced", option: ["Calm", "Balanced", "Responsive", "Custom"] },
    phRunExtension: switchEntity(false),
  });
  const before = structuredClone(state.entities);
  const markup = renderPowerHouseAdvancedField();
  assert.match(markup, /Power House — comfort/);
  assert.match(markup, /Op temperatuur houden/);
  const [daily, advanced] = markup.split('<details class="oq-settings-advanced"');
  assert.match(daily, /Reageren op afkoeling/);
  assert.match(daily, /20,8 °C/);
  assert.doesNotMatch(daily, /Comfort boven setpoint|Temperatuurreactie|Reactieprofiel|oq-ph-concept/);
  assert.match(advanced, /data-oq-settings-advanced="power-house">/);
  assert.match(advanced, /Comfort boven setpoint/);
  assert.match(advanced, /Temperatuurreactie/);
  assert.match(advanced, /Power House responsprofiel/);
  assert.deepEqual(state.entities, before);
  state.settingsAdvancedOpen["power-house"] = true;
  assert.match(renderPowerHouseAdvancedField(), /data-oq-settings-advanced="power-house" open/);
  setLocale("en", { persist: false });
  try {
    const english = renderPowerHouseAdvancedField();
    assert.match(english, /Maintain temperature|Respond to cooling/);
    assert.match(english, /20\.8 °C/);
    assert.doesNotMatch(english, /Op temperatuur houden|Reageren op afkoeling/);
  } finally { setLocale("nl", { persist: false }); }
});

test("voorvertoning verandert bevestigde grenzen en actuele aanvraag niet", () => {
  resetSettingsState({
    roomSetpoint: numberEntity(21),
    phComfortBelow: numberEntity(0.2),
    phRunExtension: switchEntity(true),
    phRunExtensionStopMargin: numberEntity(0.7),
    phRunExtensionRestartCooldown: numberEntity(0.7),
    phRunExtensionStatus: { value: "warm_restart", state: "warm_restart" },
  });
  assert.match(renderPowerHouseRunExtensionField(), /Met de bevestigde instellingen/);
  state.inputDrafts.phRunExtensionStopMargin = "0.9";
  state.drafts.phRunExtensionStopMargin = 0.9;
  let markup = renderPowerHouseRunExtensionField();
  assert.match(markup, /Voorvertoning — wijziging nog niet bevestigd/);
  assert.match(markup, /21,9 °C/);
  assert.match(markup, /21,2 °C/);
  assert.match(markup, /Nog bevestigd: stop aanvragen bij 21,7 °C.*21,0 °C/);
  assert.match(markup, /Herstart aangevraagd/);
  assert.equal(getRunExtensionThresholds(false).stop, 21.7);
  state.entities.phRunExtensionStopMargin.value = 0.9;
  state.inputDrafts = {};
  state.drafts = {};
  markup = renderPowerHouseRunExtensionField();
  assert.match(markup, /Met de bevestigde instellingen/);
  assert.doesNotMatch(markup, /Voorvertoning|Nog bevestigd/);
});

test("afkoelmarge geeft ook zonder doorverwarmen een actuele koude-grensvoorvertoning", () => {
  resetSettingsState({ roomSetpoint: numberEntity(21), phComfortBelow: numberEntity(0.15), phRunExtension: switchEntity(false) });
  assert.match(renderPowerHouseComfortThresholds(), /20,85 °C/);
  const comfort = { innerHTML: "" };
  const previousRoot = state.root;
  state.root = { querySelector(selector) { return selector === "[data-oq-power-house-comfort-thresholds]" ? comfort : null; } };
  try {
    const input = { dataset: { oqField: "phComfortBelow" }, type: "number", value: "0.25" };
    handleInput({ target: input });
    assert.match(comfort.innerHTML, /20,75 °C/);
    assert.match(comfort.innerHTML, /Voorvertoning — wijziging nog niet bevestigd/);
    assert.match(comfort.innerHTML, /Nog bevestigde koude grens: 20,85 °C/);
    assert.equal(input.value, "0.25");
    input.value = "";
    handleInput({ target: input });
    assert.match(comfort.innerHTML, /<strong>—<\/strong>/);
    assert.equal(state.entities.phComfortBelow.value, 0.15);
  } finally { state.root = previousRoot; }
});

test("live status blijft actueel tijdens invoer zonder de voorvertoning te bevestigen", () => {
  resetSettingsState({
    phRunExtension: switchEntity(true),
    phRunExtensionStatus: { value: "extending", state: "extending" },
    roomSetpoint: numberEntity(21),
    phComfortBelow: numberEntity(0.2),
    phRunExtensionStopMargin: numberEntity(0.7),
    phRunExtensionRestartCooldown: numberEntity(0.2),
  });
  const status = { textContent: "" };
  const preview = { innerHTML: "" };
  const previousRoot = state.root;
  state.root = { querySelector(selector) {
    if (selector === "[data-oq-run-extension-status]") return status;
    return selector === "[data-oq-run-extension-thresholds]" ? preview : null;
  } };
  try {
    const input = { dataset: { oqField: "phRunExtensionRestartCooldown" }, type: "number", value: "0.7" };
    handleInput({ target: input });
    assert.equal(status.textContent, "Doorverwarmen op minimumvermogen");
    state.entities.phRunExtensionStatus.value = "comfort_stop";
    state.entities.phRunExtensionStatus.state = "comfort_stop";
    patchRunExtensionThresholds();
    assert.equal(status.textContent, "Stop aangevraagd");
    assert.match(preview.innerHTML, /Voorvertoning — wijziging nog niet bevestigd/);
    assert.match(preview.innerHTML, /Nog bevestigd: stop aanvragen bij 21,7 °C.*21,5 °C/);
    assert.equal(input.value, "0.7");
    setLocale("en", { persist: false });
    patchRunExtensionThresholds();
    assert.equal(status.textContent, "Stop requested");
  } finally { state.root = previousRoot; setLocale("nl", { persist: false }); }
});
