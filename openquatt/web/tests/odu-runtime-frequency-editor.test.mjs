import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const [configSource, installationSource, mockSource] = await Promise.all([
  readFile(new URL("../js/src/core/config.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/settings/installation.js", import.meta.url), "utf8"),
  readFile(new URL("../js/mock-device.js", import.meta.url), "utf8"),
]);

test("runtime-editor hydrateert F0-F20 maar toont de uitbreiding alleen bij V2-new", () => {
  assert.match(configSource, /ODU_RUNTIME_FREQUENCY_LEVELS = Array\.from\(\{ length: 21 \}/);
  assert.match(installationSource, /=== "V2 new model"/);
  assert.match(installationSource, /slice\(0, extendedLayout \? 21 : 11\)/);
  assert.match(installationSource, /getOduRuntimeFrequencyLevels\(hpIndex\)/);
  assert.match(installationSource, /level === 0 \? value !== 0 : value <= 0/);
  assert.match(mockSource, /ODU_RUNTIME_FREQUENCY_TABLE_V2_NEW/);
  assert.match(mockSource, /getMockOduRuntimeFrequencyLevels/);
  assert.match(mockSource, /registerCount.*=== 21 \? 42 : 22/);
});
