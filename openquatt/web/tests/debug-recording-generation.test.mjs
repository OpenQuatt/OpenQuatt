import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/dev.html" },
  setTimeout,
  clearTimeout,
  localStorage: { getItem: () => null },
};

const { normalizeDebugRecordingValue } = await import("../js/src/features/debug-recording.js");
const { state } = await import("../js/src/core/state.js");

test("gemengde Duo-generaties blijven per ODU gescheiden", () => {
  state.entities = {
    hp1Generation: { state: "V1", value: "V1" },
    hp2Generation: { state: "V1.5", value: "V1.5" },
  };

  assert.equal(normalizeDebugRecordingValue("hp1Generation"), "V1");
  assert.equal(normalizeDebugRecordingValue("hp2Generation"), "V1.5");
});

test("onbekende ODU-generatie blijft expliciet Unknown in browseropnames", () => {
  state.entities = {
    hp1Generation: { state: "Unknown", value: "Unknown" },
    hp2Generation: { state: "unknown", value: "unknown" },
  };

  assert.equal(normalizeDebugRecordingValue("hp1Generation"), "Unknown");
  assert.equal(normalizeDebugRecordingValue("hp2Generation"), "Unknown");
});

test("mock-Duo gebruikt canonieke generaties met overeenkomende control_board_item-waarden", async () => {
  const fixtureSource = await readFile(new URL("../js/mock-fixtures.js", import.meta.url), "utf8");
  const context = { window: { __OQ_MOCK_SCENARIOS__: [] } };
  vm.runInNewContext(fixtureSource, context, { filename: "mock-fixtures.js" });

  const identities = context.window.__OQ_MOCK_FIXTURES__.oduIdentities;
  const profiles = context.window.__OQ_MOCK_FIXTURES__.oduProfiles;
  const generationOptions = context.window.__OQ_MOCK_FIXTURES__.devControlOptions.oduGeneration;
  assert.equal(identities[1].generation, "V1");
  assert.equal(identities[1].controlBoardItem, 0x0037);
  assert.equal(identities[2].generation, "V1.5");
  assert.equal(identities[2].controlBoardItem, 0x0E37);
  assert.equal(identities[2].pcbProgram, 0x011E);
  assert.equal(identities[2].compressorCode, 0);
  assert.notEqual(identities[1].generation, identities[2].generation);
  assert.equal(profiles.V2OldModel.generation, "V2");
  assert.equal(profiles.V2OldModel.variant, "V2 old model");
  assert.equal(profiles.V2OldModel.controlBoardItem, 0x0E37);
  assert.equal(profiles.V2OldModel.pcbProgram, 0x0122);
  assert.equal(profiles.V2OldModel.compressorCode, 2825);
  assert.equal(profiles.V2OldModel.customerModel, "AMH6");
  assert.equal(profiles.V2.controlBoardItem, 0x1037);
  assert.equal(profiles.V2.pcbProgram, 0x0201);
  assert.equal(profiles.V2.compressorCode, 2825);
  assert.equal(profiles.V2.customerModel, "AMH6");
  assert.equal(profiles.Unknown.generation, "Unknown");
  assert.equal(profiles.Unknown.controlBoardItem, 0xFFFF);
  assert.deepEqual(
    Array.from(generationOptions, ({ value }) => value),
    ["V1", "V1.5", "V2", "Unknown"],
  );

  const hp2Entities = context.window.__OQ_MOCK_FIXTURES__.hp2Entities;
  assert.equal(hp2Entities.find(([, name]) => name === "HP2 - Control board item number")?.[2]?.value, 0x0E37);
  assert.equal(hp2Entities.find(([, name]) => name === "HP2 - ODU generation")?.[2]?.value, "V1.5");
  assert.equal(hp2Entities.find(([, name]) => name === "HP2 - ODU generation variant")?.[2]?.value, "V1.5");
  assert.equal(hp2Entities.find(([, name]) => name === "HP2 - ODU customer model code")?.[2]?.value, "Missing");
});

test("mock-identiteit en herdetectie gebruiken dezelfde profielbron en publiceren eerst Unknown", async () => {
  const mockSource = await readFile(new URL("../js/mock-device.js", import.meta.url), "utf8");

  assert.match(mockSource, /core\[0\] = compressorCode;/);
  assert.match(mockSource, /core\[8\] = pcbProgram;/);
  assert.match(mockSource, /core\[13\] = controlBoardItem;/);
  assert.match(mockSource, /customerModelWords: encodeMockAsciiWords\(customerModel\)/);
  assert.match(mockSource, /const profile = getMockOduProfile\(hp\);/);
  assert.match(mockSource, /syncMockOduIdentityEntities\(1\);/);
  assert.match(mockSource, /setText\("text_sensor", `HP\$\{hp\} - ODU generation`, "Unknown"\);/);
  assert.match(mockSource, /setText\("text_sensor", `HP\$\{hp\} - ODU generation variant`, "Unknown"\);/);
  assert.match(mockSource, /setText\("text_sensor", `HP\$\{hp\} - ODU customer model code`, "Unknown"\);/);
  assert.match(mockSource, /data-oq-dev-control="hp1-generation"/);
  assert.match(mockSource, /data-oq-dev-control="hp2-generation"/);
  assert.match(mockSource, /setMockOduGeneration\(hp, generation\.value\);/);
  assert.doesNotMatch(mockSource, /core\[13\] = v2 \? 0x1202 : 0x1101;/);
});
