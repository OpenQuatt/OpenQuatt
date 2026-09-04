import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

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

test("dev-preview kan beide ODU-editors veilig en geblokkeerd doorlopen", () => {
  assert.match(mockFixturesSource, /oduWriteState/);
  assert.match(mockFixturesSource, /Standby · 0 Hz/);
  assert.match(mockFixturesSource, /Heating · 30 Hz/);
  assert.match(mockSource, /data-oq-dev-control="odu-write-state"/);
  assert.match(mockSource, /function applyOduWriteTestState\(\)/);
  assert.match(mockSource, /window\.__OQ_DEV_ODU_WRITE_STATE__ = state\.oduWriteState/);
  assert.match(mockSource, /service\.status = "PENDING_SAFE"/);
  assert.match(mockSource, /settings\.status = "IN_SYNC"/);
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
  assert.match(cssSource, /\.oq-settings-odu-launcher-list \{\s+gap: 12px;/);
});
