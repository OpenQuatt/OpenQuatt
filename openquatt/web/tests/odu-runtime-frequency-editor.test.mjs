import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { renderOduRuntimeFrequencyModal } = await import("../js/src/features/odu-runtime-frequency.js");

const [
  configSource,
  featureSource,
  installationSource,
  viewActionsSource,
  mockSource,
  mockFixturesSource,
  devSource,
  cssSource,
] = await Promise.all([
  readFile(new URL("../js/src/core/config.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/features/odu-runtime-frequency.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/settings/installation.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/features/view-actions.js", import.meta.url), "utf8"),
  readFile(new URL("../js/mock-device.js", import.meta.url), "utf8"),
  readFile(new URL("../js/mock-fixtures.js", import.meta.url), "utf8"),
  readFile(new URL("../dev.html", import.meta.url), "utf8"),
  readFile(new URL("../css/src/12-settings-service.css", import.meta.url), "utf8"),
]);

test("gedeelde editor bewaart frequentie-gates en servicegebonden getalvelden", () => {
  state.entities = { installationTopology: { value: "single" }, hp1Mode: { value: "Standby" }, hp1Freq: { value: 0 } };
  state.oduRuntimeFrequencyDrafts = {};
  state.busyAction = "";
  const table = Array.from({ length: 11 }, (_, level) => level * 5);
  const status = { available: true, loaded: true, armed: true, busy: false, levelCount: 11, cooling: table, heating: table };
  for (const patch of [{}, { loaded: false }, { armed: false }, { busy: true }, { available: false }]) {
    state.oduRuntimeFrequencyStatuses = { 1: { ...status, ...patch } };
    const markup = renderOduRuntimeFrequencyModal();
    const button = markup.match(/<button[^>]*data-oq-action="odu-runtime-apply"[^>]*>/)[0];
    assert.equal(/\bdisabled\b/.test(button), Object.keys(patch).length > 0);
    assert.doesNotMatch(markup, /data-oq-field=/);
    if (patch.loaded !== false) {
      assert.equal((markup.match(/type="number"/g) || []).length, 22);
      assert.match(markup, /aria-label="HP1 koelen F0 in Hz"/);
      assert.match(markup, /aria-label="HP1 verwarmen F10 in Hz"/);
      assert.match(markup, /Koelen<br>Hz/);
      assert.match(markup, /Verwarmen<br>Hz/);
      assert.doesNotMatch(markup, /oq-helper-control--suffix|oq-helper-unit-chip/);
    }
  }
  state.oduRuntimeFrequencyStatuses = { 1: status };
  for (const [mode, frequency] of [["Heating", 30], ["Standby", 30], ["Unknown", 0], ["Standby", "Unknown"]]) {
    state.entities.hp1Mode.value = mode;
    state.entities.hp1Freq.value = frequency;
    const button = renderOduRuntimeFrequencyModal().match(/<button[^>]*data-oq-action="odu-runtime-apply"[^>]*>/)[0];
    assert.match(button, /\bdisabled\b/, `${mode} / ${frequency} must remain blocked`);
  }
});

test("runtime-editor gebruikt de native per-HP service zonder ESPHome-entitydefinities", () => {
  assert.doesNotMatch(configSource, /ODU_RUNTIME_FREQUENCY_KEYS/);
  assert.doesNotMatch(configSource, /oduRuntimeCoolingF\d/);
  assert.match(featureSource, /ODU_RUNTIME_FREQUENCY_LEVELS = Array\.from\(\{ length: 21 \}/);
  assert.match(featureSource, /\/openquatt\/odu-runtime\/hp\$\{hpIndex\}\/\$\{action\}/);
  assert.match(featureSource, /if \(!status\?\.loaded\) return/);
  assert.match(featureSource, /body\.set\("csrf_token", status\.csrfToken\)/);
  assert.match(featureSource, /state\.systemModal === "odu-frequency-settings"/);
  assert.match(featureSource, /renderOduRuntimeFrequencyModal/);
  assert.doesNotMatch(installationSource, /Runtime only/);
});

test("runtime-editor hydrateert F0-F20 maar toont de uitbreiding alleen na native detectie", () => {
  assert.match(featureSource, /extendedLayout: payload\.extended_layout === true/);
  assert.match(featureSource, /slice\(0, status\?\.levelCount === 21 \? 21 : 11\)/);
  assert.match(featureSource, /Number\.isInteger\(value\)/);
  assert.match(featureSource, /level === 0 \? value !== 0 : value < 1/);
  assert.match(featureSource, /data-oq-odu-runtime-hp/);
  assert.match(featureSource, /!status\?\.loaded \|\| validation\.valid/);
  assert.doesNotMatch(featureSource, /data-oq-field="oduRuntime/);
  assert.match(mockSource, /ODU_RUNTIME_FREQUENCY_TABLE_V2_NEW/);
  assert.match(mockSource, /handleMockOduRuntimeRequest/);
  assert.match(mockSource, /status\|load\|arm\|apply/);
  assert.match(mockSource, /pathname\.match\(\/\\\/openquatt\\\/odu-runtime/);
  assert.match(mockSource, /Number\.isInteger\(value\)/);
  assert.match(mockSource, /service\.extendedLayout \? 42 : 22/);
});

test("dev-preview onderscheidt direct toepasbare bodemplaatinstellingen van de geblokkeerde frequentietabel", () => {
  assert.match(mockFixturesSource, /oduWriteState/);
  assert.match(mockFixturesSource, /Standby · 0 Hz/);
  assert.match(mockFixturesSource, /Heating · 30 Hz/);
  assert.match(mockSource, /data-oq-dev-control="odu-write-state"/);
  assert.match(mockSource, /function applyOduWriteTestState\(\)/);
  assert.match(mockSource, /window\.__OQ_DEV_ODU_WRITE_STATE__ = state\.oduWriteState/);
  assert.doesNotMatch(mockSource, /service\.status = "PENDING_SAFE"/);
  assert.match(mockSource, /service\.status = "IN_SYNC"/);
  assert.match(mockSource, /BLOCKED: ODU is not in standby/);
  assert.match(featureSource, /__OQ_PREVIEW__ && typeof window !== "undefined"/);
  assert.match(featureSource, /window\.__OQ_DEV_ODU_WRITE_STATE__/);
  assert.match(devSource, /mock-fixtures\.js\?v=odu-settings-v3/);
  assert.match(devSource, /mock-device\.js\?v=odu-settings-v3/);
  assert.match(devSource, /openquatt-preview\.js\?v=odu-settings-v3/);
});

test("buitenunitinstellingen openen beide editors zonder interne termen in de hoofdtekst", () => {
  assert.match(installationSource, /Instellingen buitenunit/);
  assert.match(installationSource, /open-odu-bottom-plate-settings/);
  assert.match(installationSource, /open-odu-frequency-settings/);
  assert.doesNotMatch(installationSource, /runtime shadow/i);
  assert.doesNotMatch(installationSource, /EEPROM/);
  assert.match(viewActionsSource, /"open-odu-bottom-plate-settings": \(\) => \{\s+state\.controlNotice = "";/);
  assert.match(viewActionsSource, /"open-odu-frequency-settings": \(\) => \{\s+state\.controlNotice = "";/);
  assert.match(viewActionsSource, /"toggle-odu-frequency-technical-details"/);
  assert.match(featureSource, /oduRuntimeFrequencyTechnicalDetailsOpen \? " open"/);
  assert.match(cssSource, /\.oq-settings-odu-launcher-list \{\s+display: grid;\s+gap: 16px;/);
  assert.match(cssSource, /\.oq-settings-odu-runtime-warning \{[^}]*margin-bottom: 16px;/s);
  assert.match(cssSource, /\.oq-settings-odu-modal > \.oq-helper-modal-head > div \{\s+min-width: 0;\s+overflow-wrap: anywhere;/);
  assert.match(featureSource, /V1 en V1\.5 voorzichtig met koelwaarden onder 30 Hz/);
  assert.match(featureSource, /Bij V2 is 20 Hz toegestaan/);
  assert.match(featureSource, /volledig stroomloos.*oorspronkelijke frequenties/);
  assert.doesNotMatch(featureSource, /OEM-ondergrens|suction superheat|natte zuigretour/i);
});
