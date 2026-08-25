import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: { getItem: () => null },
};

const { state } = await import("../js/src/core/state.js");
const { getManualHpActualValue, getManualHpMaximumLevel } = await import("../js/src/settings/service.js");

test("CM100 geeft F20 alleen vrij voor een bevestigd uitgebreid V2-profiel", () => {
  state.entities = {
    hpGeneration: { state: "V2", value: "V2" },
    hp1CompressorLevelProfile: { state: "V2 F0-F20", value: "V2 F0-F20" },
    hp2CompressorLevelProfile: { state: "Unknown / F0-F10 safe", value: "Unknown / F0-F10 safe" },
  };

  assert.equal(getManualHpMaximumLevel("hp1CompressorLevelProfile"), 20);
  assert.equal(getManualHpMaximumLevel("hp2CompressorLevelProfile"), 10);
  assert.equal(getManualHpMaximumLevel("missingProfile"), 10);

  state.entities.hpGeneration = { state: "V1.5", value: "V1.5" };
  assert.equal(getManualHpMaximumLevel("hp1CompressorLevelProfile"), 10);
});

test("service hydrateert zowel de selectie als de gedetecteerde profielen", async () => {
  const [configSource, syncSource] = await Promise.all([
    readFile(new URL("../js/src/core/config.js", import.meta.url), "utf8"),
    readFile(new URL("../js/src/core/entity-sync.js", import.meta.url), "utf8"),
  ]);

  assert.match(configSource, /COMMISSIONING_STATE_KEYS[\s\S]*"hp1CompressorLevelProfile"/);
  assert.match(configSource, /COMMISSIONING_STATE_KEYS[\s\S]*"hp2CompressorLevelProfile"/);
  assert.match(syncSource, /service: \[[\s\S]*"hpGeneration"/);
});

test("CM100 toont de actieve select-index als fysiek F-level", () => {
  state.entities = {
    hp1Compressor: { value: 17 },
    hp1Freq: { value: 90 },
  };

  assert.equal(getManualHpActualValue("hp1Compressor", "hp1Freq"), "F17 (90 Hz)");
});
