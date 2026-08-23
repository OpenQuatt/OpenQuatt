import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
  localStorage: { getItem: () => null },
};

const { ENTITY_DEFS } = await import("../js/src/core/config.js");
const {
  createOduGenerationDetectionModel,
  normalizeDetectedOduGeneration,
} = await import("../js/src/core/odu-generation.js");
const {
  getOduGenerationChoiceMeta,
  renderOduGenerationDetectionStatus,
} = await import("../js/src/features/odu-generation-ui.js");
const { state } = await import("../js/src/core/state.js");
const [installationSource, quickStartSource, quickStartActionsSource, entitySyncSource, namedButtonActionsSource] = await Promise.all([
  readFile(new URL("../js/src/settings/installation.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/features/quickstart-actions.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/core/entity-sync.js", import.meta.url), "utf8"),
  readFile(new URL("../js/src/core/named-button-actions.js", import.meta.url), "utf8"),
]);

function textEntity(value) {
  return { value, state: value };
}

function resetGenerationState({ topology = "single", selected = "V1.5", hp1 = "V1.5", hp2 = null } = {}) {
  state.entities = {
    installationTopology: textEntity(topology),
    hpGeneration: {
      value: selected,
      state: selected,
      option: ["V1", "V1.5", "V2"],
    },
    hp1Generation: textEntity(hp1),
    hp1GenerationDetect: textEntity(""),
    ...(hp2 === null
      ? {}
      : {
          hp2Generation: textEntity(hp2),
          hp2GenerationDetect: textEntity(""),
        }),
  };
  state.drafts = {};
  state.inputDrafts = {};
  state.optionalMissingEntities = {};
  state.loadingEntities = false;
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.currentStep = "generation";
  state.lastKnownInstallationTopology = "";
}

test("web accepteert alleen canonieke firmwarelabels en bevat geen registermapping", () => {
  assert.equal(normalizeDetectedOduGeneration("V1"), "V1");
  assert.equal(normalizeDetectedOduGeneration("V1.5"), "V1.5");
  assert.equal(normalizeDetectedOduGeneration("V2"), "V2");
  assert.equal(normalizeDetectedOduGeneration("Unknown"), "Unknown");
  assert.equal(normalizeDetectedOduGeneration("v2"), "Unknown");
  assert.equal(normalizeDetectedOduGeneration("0x1037"), "Unknown");
  assert.equal(normalizeDetectedOduGeneration(4151), "Unknown");
});

test("Single adviseert exact de bekende firmwaredetectie", () => {
  const model = createOduGenerationDetectionModel({
    topology: "single",
    configuredGeneration: "V1.5",
    hp1Available: true,
    hp1Generation: "V2",
  });

  assert.equal(model.recommendation, "V2");
  assert.equal(model.status, "mismatch");
  assert.deepEqual(model.heatPumps.map(({ index, generation }) => [index, generation]), [[1, "V2"]]);
});

test("Duo-advies volgt uitsluitend de afgesproken generatiematrix", () => {
  const detect = (hp1Generation, hp2Generation) => createOduGenerationDetectionModel({
    topology: "duo",
    configuredGeneration: "V1",
    hp1Available: true,
    hp1Generation,
    hp2Available: true,
    hp2Generation,
  });

  assert.equal(detect("V1", "V1").recommendation, "V1");
  assert.equal(detect("V1", "V1.5").recommendation, "V1");
  assert.equal(detect("V1.5", "V1").recommendation, "V1");
  assert.equal(detect("V1.5", "V1.5").recommendation, "V1.5");
  assert.equal(detect("V2", "V2").recommendation, "V2");
  assert.equal(detect("V1.5", "V2").recommendation, "");
  assert.equal(detect("V1", "V2").recommendation, "");
});

test("Unknown en onvolledige Duo-detectie geven fail-closed geen advies", () => {
  const unknown = createOduGenerationDetectionModel({
    topology: "duo",
    configuredGeneration: "V2",
    hp1Available: true,
    hp1Generation: "Unknown",
    hp2Available: true,
    hp2Generation: "V2",
  });
  const missing = createOduGenerationDetectionModel({
    topology: "duo",
    configuredGeneration: "V2",
    hp1Available: true,
    hp1Generation: "V2",
  });

  assert.equal(unknown.status, "unknown");
  assert.equal(unknown.recommendation, "");
  assert.equal(missing.status, "unknown");
  assert.equal(missing.recommendation, "");
});

test("onbekende topologie geeft ook met bekende HP1 fail-closed geen advies", () => {
  const model = createOduGenerationDetectionModel({
    configuredGeneration: "V1",
    hp1Available: true,
    hp1Generation: "V1",
  });

  assert.equal(model.topology, "unknown");
  assert.equal(model.status, "unknown");
  assert.equal(model.recommendation, "");
});

test("onboarding toont Duo per HP en onderscheidt geselecteerd van aanbevolen zonder auto-save", () => {
  resetGenerationState({ topology: "duo", selected: "V1.5", hp1: "V1", hp2: "V1.5" });

  const markup = renderOduGenerationDetectionStatus();

  assert.match(markup, /HP1/);
  assert.match(markup, /Quatt ODU V1/);
  assert.match(markup, /HP2/);
  assert.match(markup, /Quatt ODU V1\.5/);
  assert.match(markup, /Gemengde Duo/);
  assert.match(markup, /Advies V1/);
  assert.match(markup, /V1 wordt aanbevolen; V1\.5 is geselecteerd/);
  assert.match(markup, /aria-live="polite"/);
  assert.match(markup, /aria-label="Detecteer HP1 opnieuw"/);
  assert.match(markup, /aria-label="Detecteer HP2 opnieuw"/);
  assert.match(markup, /data-oq-button-key="hp1GenerationDetect"/);
  assert.match(markup, /data-oq-button-key="hp2GenerationDetect"/);
  assert.equal(getOduGenerationChoiceMeta("V1", "V1.5", "V1"), "Aanbevolen");
  assert.equal(getOduGenerationChoiceMeta("V1.5", "V1.5", "V1"), "Geselecteerd");
  assert.match(installationSource, /meta: getOduGenerationChoiceMeta\(option, currentValue, detectionModel\.recommendation\)/);
  assert.match(quickStartSource, /renderHpGenerationField\(\)/);
  assert.equal(state.entities.hpGeneration.value, "V1.5");
  assert.deepEqual(state.drafts, {});

  state.busyAction = "hp1GenerationDetect";
  assert.match(renderOduGenerationDetectionStatus(), /aria-label="HP1 wordt opnieuw gedetecteerd"/);
});

test("Unknown blijft zichtbaar, toont geen aanbevolen badge en behoudt handmatige fallback", () => {
  resetGenerationState({ topology: "duo", selected: "V2", hp1: "Unknown", hp2: "V2" });

  const markup = renderOduGenerationDetectionStatus();

  assert.match(markup, />Unknown</);
  assert.match(markup, /geen versie geadviseerd/i);
  assert.equal(getOduGenerationChoiceMeta("V2", "V2", ""), "Geselecteerd");
  assert.match(installationSource, /renderSettingsChoiceOption\(/);
  assert.equal(state.entities.hpGeneration.value, "V2");
});

test("geselecteerde uniforme detectie krijgt één gecombineerde badge", () => {
  resetGenerationState({ topology: "duo", selected: "V2", hp1: "V2", hp2: "V2" });

  const markup = renderOduGenerationDetectionStatus();

  assert.equal(getOduGenerationChoiceMeta("V2", "V2", "V2"), "Geselecteerd · aanbevolen");
  assert.match(markup, /Komt overeen/);
  assert.doesNotMatch(markup, /komt overeen met de detectie/);
});

test("onboarding en installatiehydratie laden status en optionele detectieknoppen", () => {
  for (const [key, name] of [
    ["hp1Generation", "HP1 - ODU generation"],
    ["hp2Generation", "HP2 - ODU generation"],
    ["hp1GenerationDetect", "HP1 - Detect ODU generation"],
    ["hp2GenerationDetect", "HP2 - Detect ODU generation"],
  ]) {
    assert.equal(ENTITY_DEFS[key].name, name);
    assert.equal(ENTITY_DEFS[key].optional, true);
  }
  assert.match(quickStartActionsSource, /stepId === "generation"[\s\S]*\.\.\.ODU_GENERATION_KEYS[\s\S]*\.\.\.ODU_GENERATION_DETECT_KEYS/);
  assert.match(quickStartActionsSource, /stepId === "confirm"[\s\S]*\.\.\.ODU_GENERATION_KEYS[\s\S]*\.\.\.ODU_GENERATION_DETECT_KEYS/);
  assert.match(entitySyncSource, /installation:\s*\[[\s\S]*\.\.\.ODU_GENERATION_KEYS[\s\S]*\.\.\.ODU_GENERATION_DETECT_KEYS/);
  assert.match(entitySyncSource, /state\.currentStep === "generation" \|\| state\.currentStep === "confirm"/);
  assert.match(entitySyncSource, /\.\.\.quickStartGenerationKeys/);
  assert.match(namedButtonActionsSource, /ODU_GENERATION_DETECT_KEYS\.indexOf\(buttonKey\)/);
  assert.match(namedButtonActionsSource, /refreshKeys: \[ODU_GENERATION_KEYS\[generationDetectIndex\]\]/);
});
