import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
  localStorage: {
    getItem: () => null,
  },
};

const testDir = path.dirname(fileURLToPath(import.meta.url));
const mockSource = await readFile(path.join(testDir, "../js/mock-device.js"), "utf8");
const devHtml = await readFile(path.join(testDir, "../dev.html"), "utf8");
const { state } = await import("../js/src/core/state.js");
const { SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");
const { renderSettingsServiceTaskModal } = await import("../js/src/settings/service.js");

function numberEntity(value, maxValue = 20) {
  return {
    value,
    state: String(value),
    min_value: 0,
    max_value: maxValue,
    step: 1,
    uom: "",
  };
}

function renderManualHpModal(hpGeneration, requestedLevel = 0) {
  state.entities = {
    hpGeneration: { value: hpGeneration, state: hpGeneration, option: ["V1", "V1.5", "V2"] },
    cm100Active: { value: true, state: "ON" },
    commissioningStatus: { value: "CM100 READY", state: "CM100 READY" },
    manualHpStart: { state: "" },
    manualHpAbort: { state: "" },
    manualHpStatus: { value: "IDLE", state: "IDLE" },
    manualHpGuardStatus: { value: "Vrijgegeven", state: "Vrijgegeven" },
    manualHp1Mode: { value: "Standby", state: "Standby", option: ["Standby", "Heating", "Cooling"] },
    manualHp1Level: numberEntity(requestedLevel),
    flowSelected: numberEntity(800, 1500),
    hp1Compressor: numberEntity(0),
    hp1Freq: numberEntity(0),
    hp1Failures: { value: "None", state: "None" },
  };
  state.drafts = {};
  state.inputDrafts = {};
  state.loadingEntities = false;
  state.busyAction = "";
  state.commissioningTaskLock = "";
  state.pendingManualHpStart = false;
  state.systemModal = "service-task-manual-hp";
  return renderSettingsServiceTaskModal();
}

test("CM100 handmatige warmtepomp begrenst V1/V1.5 op 10 en V2 op 20", () => {
  const v15Markup = renderManualHpModal("V1.5", 20);
  assert.match(v15Markup, /data-oq-field="manualHp1Level" min="0" max="10"/);
  assert.match(v15Markup, /max="10" step="1" value="10"/);
  assert.match(v15Markup, /Aangevraagde stand 0 tot en met 10/);

  const v2Markup = renderManualHpModal("V2");
  assert.match(v2Markup, /data-oq-field="manualHp1Level" min="0" max="20"/);
  assert.match(v2Markup, /Aangevraagde stand 0 tot en met 20 voor Quatt V2/);

  const unknownMarkup = renderManualHpModal("");
  assert.match(unknownMarkup, /data-oq-field="manualHp1Level" min="0" max="10"/);
});

test("service-hydration en mock volgen de geselecteerde Quatt-versie", () => {
  assert.ok(SETTINGS_GROUP_KEY_MAP.service.includes("hpGeneration"));
  assert.match(mockSource, /\["Manual HP1 compressor level", 0, 0, 10, 1, ""\]/);
  assert.match(mockSource, /\["Manual HP2 compressor level", 0, 0, 10, 1, ""\]/);
  assert.match(mockSource, /function getManualHpMaxLevel\(\)[\s\S]*=== "V2" \? 20 : 10/);
  assert.match(mockSource, /name === "Quatt Hybrid version"[\s\S]*syncManualHpLevelRange\(\)/);
  assert.match(devHtml, /mock-device\.js\?v=q-manual-hp-level-20-v2/);
  assert.match(devHtml, /openquatt-preview\.js\?v=q-manual-hp-level-20-v2/);
});
