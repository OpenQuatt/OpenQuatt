import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false };
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
  localStorage: { getItem: () => null },
};

const originalFetch = globalThis.fetch;
const { syncEntities } = await import("../js/src/core/entity-sync.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
const { setViewPatchControls } = await import("../js/src/core/view-patch-controls.js");
const { state } = await import("../js/src/core/state.js");
const { getOduGenerationDetectionModel } = await import("../js/src/features/odu-generation-ui.js");

function textEntity(value) {
  return { state: value, value };
}

test.afterEach(() => {
  setRenderCallback(null);
  globalThis.fetch = originalFetch;
});

test("late ODU-generatiewaarden hertekenen onboarding alleen bij een wijziging", async () => {
  const now = Date.now();
  state.appView = "energy";
  state.nativeOpen = false;
  state.loadingEntities = false;
  state.draggingCurveKey = "";
  state.busyAction = "";
  state.settingsInteractionLock = false;
  state.updateInstallBusy = false;
  state.updateInstallPhaseHint = "";
  state.focusedField = "";
  state.entitySyncInFlight = false;
  state.pendingEntitySyncOptions = null;
  state.deviceReconnectMode = "";
  state.entitySyncFailureCount = 0;
  state.lastEntitySyncSuccessAt = now;
  state.lastEntityResponseAt = now;
  state.lastBulkEntitySyncAt = now;
  state.lastStaticEntitySyncAt = now;
  state.quickStartModalOpen = true;
  state.currentStep = "generation";
  state.complete = false;
  state.systemModal = "";
  state.updateModalOpen = false;
  state.interfacePanelOpen = false;
  state.headerRenderSignature = "";
  state.optionalMissingEntities = {};
  state.entities = {
    setupComplete: textEntity(false),
    installationTopology: textEntity("duo"),
    hpGeneration: textEntity("V1.5"),
    hp1Generation: textEntity("Unknown"),
    hp2Generation: textEntity("Unknown"),
  };

  let renderCount = 0;
  let energyPatchCount = 0;
  setRenderCallback(() => {
    renderCount += 1;
  });
  setViewPatchControls({
    patchEnergyDom: () => {
      energyPatchCount += 1;
      return true;
    },
  });

  globalThis.fetch = async (url, options = {}) => {
    assert.equal(String(url), "/openquatt/entities");
    const params = new URLSearchParams(String(options.body || ""));
    const keys = String(params.get("entities") || "")
      .split("\n")
      .filter(Boolean)
      .map((line) => line.split("\t")[0]);
    const entities = Object.fromEntries(keys.map((key) => {
      if (key === "hp1Generation") return [key, textEntity("V1")];
      if (key === "hp2Generation") return [key, textEntity("V1.5")];
      return [key, state.entities[key] || textEntity("")];
    }));
    return {
      ok: true,
      status: 200,
      json: async () => ({ entities, missing: [] }),
    };
  };

  await syncEntities();

  assert.equal(renderCount, 1);
  assert.equal(energyPatchCount, 0);
  assert.deepEqual(
    getOduGenerationDetectionModel().heatPumps.map(({ generation }) => generation),
    ["V1", "V1.5"],
  );
  assert.equal(getOduGenerationDetectionModel().recommendation, "V1");

  await syncEntities();

  assert.equal(renderCount, 1);
  assert.equal(energyPatchCount, 1);
});
