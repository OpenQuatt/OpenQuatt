import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const [configSource, featureSource, installationSource, mockSource] = await Promise.all([
  readFile(new URL("../js/src/core/config.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/features/odu-runtime-frequency.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/settings/installation.js", import.meta.url), "utf8"),
  readFile(new URL("../js/mock-device.js", import.meta.url), "utf8"),
]);

test("runtime-editor gebruikt de native per-HP service zonder ESPHome-entitydefinities", () => {
  assert.doesNotMatch(configSource, /ODU_RUNTIME_FREQUENCY_KEYS/);
  assert.doesNotMatch(configSource, /oduRuntimeCoolingF\d/);
  assert.match(featureSource, /ODU_RUNTIME_FREQUENCY_LEVELS = Array\.from\(\{ length: 21 \}/);
  assert.match(featureSource, /\/openquatt\/odu-runtime\/hp\$\{hpIndex\}\/\$\{action\}/);
  assert.match(featureSource, /if \(!status\?\.loaded\) return/);
  assert.match(featureSource, /body\.set\("csrf_token", status\.csrfToken\)/);
  assert.match(featureSource, /state\.oduRuntimeFrequencyDetailsOpen/);
});

test("runtime-editor hydrateert F0-F20 maar toont de uitbreiding alleen na native detectie", () => {
  assert.match(installationSource, /runtimeStatus\.extendedLayout/);
  assert.match(installationSource, /slice\(0, extendedLayout \? 21 : 11\)/);
  assert.match(installationSource, /getOduRuntimeFrequencyLevels\(hpIndex\)/);
  assert.match(installationSource, /Number\.isInteger\(value\)/);
  assert.match(installationSource, /level === 0 \? value !== 0 : value <= 0/);
  assert.match(installationSource, /data-oq-odu-runtime-hp/);
  assert.doesNotMatch(installationSource, /data-oq-field="oduRuntime/);
  assert.match(mockSource, /ODU_RUNTIME_FREQUENCY_TABLE_V2_NEW/);
  assert.match(mockSource, /handleMockOduRuntimeRequest/);
  assert.match(mockSource, /status\|load\|arm\|apply/);
  assert.match(mockSource, /Number\.isInteger\(value\)/);
  assert.match(mockSource, /service\.extendedLayout \? 42 : 22/);
});
