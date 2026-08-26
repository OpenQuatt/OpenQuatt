import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

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
    manualHp1Mode: { state: "Standby", value: "Standby" },
    manualHp2Mode: { state: "Standby", value: "Standby" },
  };

  assert.equal(getManualHpMaximumLevel("hp1CompressorLevelProfile", "manualHp1Mode"), 20);
  assert.equal(getManualHpMaximumLevel("hp2CompressorLevelProfile", "manualHp2Mode"), 10);
  assert.equal(getManualHpMaximumLevel("missingProfile", "manualHp1Mode"), 10);

  state.entities.hp1CompressorLevelProfile = {
    state: "V2 heating F0-F20",
    value: "V2 heating F0-F20",
  };
  state.entities.manualHp1Mode = { state: "Heating", value: "Heating" };
  assert.equal(getManualHpMaximumLevel("hp1CompressorLevelProfile", "manualHp1Mode"), 20);
  state.entities.manualHp1Mode = { state: "Cooling", value: "Cooling" };
  assert.equal(getManualHpMaximumLevel("hp1CompressorLevelProfile", "manualHp1Mode"), 10);

  state.entities.hp1CompressorLevelProfile = {
    state: "V2 cooling F0-F20",
    value: "V2 cooling F0-F20",
  };
  assert.equal(getManualHpMaximumLevel("hp1CompressorLevelProfile", "manualHp1Mode"), 20);
  state.entities.manualHp1Mode = { state: "Heating", value: "Heating" };
  assert.equal(getManualHpMaximumLevel("hp1CompressorLevelProfile", "manualHp1Mode"), 10);

  state.entities.hpGeneration = { state: "V1.5", value: "V1.5" };
  assert.equal(getManualHpMaximumLevel("hp1CompressorLevelProfile", "manualHp1Mode"), 10);
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

test("mock houdt V2-variant en compressorprofiel onafhankelijk", async () => {
  const [fixtureSource, mockSource] = await Promise.all([
    readFile(new URL("../js/mock-fixtures.js", import.meta.url), "utf8"),
    readFile(new URL("../js/mock-device.js", import.meta.url), "utf8"),
  ]);
  const context = { window: { __OQ_MOCK_SCENARIOS__: [] } };
  vm.runInNewContext(fixtureSource, context, { filename: "mock-fixtures.js" });

  const profiles = context.window.__OQ_MOCK_FIXTURES__.oduProfiles;
  assert.equal(profiles.V2OldModel.generation, profiles.V2.generation);
  assert.equal(profiles.V2OldModel.compressorLevelProfile, "Unknown / F0-F10 safe");
  assert.equal(profiles.V2.compressorLevelProfile, "V2 F0-F20");
  assert.match(mockSource, /profile\.compressorLevelProfile \|\| "Unknown \/ F0-F10 safe"/);
  assert.doesNotMatch(mockSource, /profile\.variant\s*===\s*"V2 new model"/);
});

test("CM100 toont de actieve select-index als fysiek F-level", () => {
  state.entities = {
    hp1Compressor: { value: 17 },
    hp1Freq: { value: 90 },
  };

  assert.equal(getManualHpActualValue("hp1Compressor", "hp1Freq"), "F17 (90 Hz)");
});
