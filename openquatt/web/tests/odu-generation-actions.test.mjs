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
const { triggerNamedButtonGroup } = await import("../js/src/core/entity-write-actions.js");
const { state } = await import("../js/src/core/state.js");

const refreshKeys = [
  "hp1Generation",
  "hp1GenerationVariant",
  "hp1CustomerModelCode",
  "hp2Generation",
  "hp2GenerationVariant",
  "hp2CustomerModelCode",
];

function textEntity(value) {
  return { state: value, value };
}

function resetState() {
  state.entities = {
    hp1GenerationDetect: textEntity(""),
    hp2GenerationDetect: textEntity(""),
    hp1Generation: textEntity("Unknown"),
    hp1GenerationVariant: textEntity("Unknown"),
    hp1CustomerModelCode: textEntity("Unknown"),
    hp2Generation: textEntity("Unknown"),
    hp2GenerationVariant: textEntity("Unknown"),
    hp2CustomerModelCode: textEntity("Unknown"),
  };
  state.optionalMissingEntities = {};
  state.loadingEntities = false;
  state.entitySyncInFlight = false;
  state.pendingEntitySyncOptions = null;
  state.lastEntitySyncSuccessAt = Date.now();
  state.lastEntityResponseAt = Date.now();
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
}

function entityRefreshResponse(options) {
  const params = new URLSearchParams(String(options.body || ""));
  const keys = String(params.get("entities") || "")
    .split("\n")
    .filter(Boolean)
    .map((line) => line.split("\t")[0]);
  const values = {
    hp1Generation: "V2",
    hp1GenerationVariant: "V2 old model",
    hp1CustomerModelCode: "AMH6",
    hp2Generation: "V1.5",
    hp2GenerationVariant: "V1.5",
    hp2CustomerModelCode: "Missing",
  };
  return {
    ok: true,
    status: 200,
    json: async () => ({
      entities: Object.fromEntries(keys.map((key) => [key, textEntity(values[key] || "")])),
      missing: [],
    }),
  };
}

test.afterEach(() => {
  globalThis.fetch = originalFetch;
});

test("één Duo-actie drukt beide firmwareknoppen en ververst de volledige fingerprint eenmaal", async () => {
  resetState();
  const requests = [];
  globalThis.fetch = async (url, options = {}) => {
    requests.push(String(url));
    if (String(url) === "/openquatt/entities") return entityRefreshResponse(options);
    return { ok: true, status: 200 };
  };

  await triggerNamedButtonGroup(["hp1GenerationDetect", "hp2GenerationDetect"], {
    busyAction: "odu-generation-detect-all",
    refreshKeys,
    successNotice: "ODU-detectie opnieuw aangevraagd.",
  });

  assert.equal(requests.filter((url) => url.includes("/button/")).length, 2);
  assert.equal(requests.filter((url) => url === "/openquatt/entities").length, 1);
  assert.equal(state.entities.hp1Generation.value, "V2");
  assert.equal(state.entities.hp1GenerationVariant.value, "V2 old model");
  assert.equal(state.entities.hp1CustomerModelCode.value, "AMH6");
  assert.equal(state.entities.hp2Generation.value, "V1.5");
  assert.equal(state.controlNotice, "ODU-detectie opnieuw aangevraagd.");
  assert.equal(state.controlError, "");
  assert.equal(state.busyAction, "");
});

test("gedeeltelijk mislukte Duo-actie ververst resultaten maar meldt de fout", async () => {
  resetState();
  let refreshCount = 0;
  globalThis.fetch = async (url, options = {}) => {
    if (String(url) === "/openquatt/entities") {
      refreshCount += 1;
      return entityRefreshResponse(options);
    }
    if (decodeURIComponent(String(url)).includes("HP2 - Detect ODU generation")) {
      return { ok: false, status: 503 };
    }
    return { ok: true, status: 200 };
  };

  await triggerNamedButtonGroup(["hp1GenerationDetect", "hp2GenerationDetect"], {
    busyAction: "odu-generation-detect-all",
    refreshKeys,
    errorPrefix: "ODU-detectie niet volledig uitgevoerd",
  });

  assert.equal(refreshCount, 1);
  assert.match(state.controlError, /ODU-detectie niet volledig uitgevoerd\. HTTP 503/);
  assert.equal(state.entities.hp1Generation.value, "V2");
  assert.equal(state.busyAction, "");
});

test("late afronding van detectie overschrijft geen nieuwere actie", async () => {
  resetState();
  globalThis.fetch = async () => {
    state.busyAction = "restartAction";
    return { ok: true, status: 200 };
  };

  await triggerNamedButtonGroup(["hp1GenerationDetect"], {
    busyAction: "odu-generation-detect-all",
    successNotice: "ODU-detectie opnieuw aangevraagd.",
  });

  assert.equal(state.busyAction, "restartAction");
  assert.equal(state.controlNotice, "");
  assert.equal(state.controlError, "");
});

test("nieuwe webapp blijft compatibel wanneer oude firmware fingerprintdiagnostiek mist", async () => {
  resetState();
  delete state.entities.hp1GenerationVariant;
  delete state.entities.hp1CustomerModelCode;
  globalThis.fetch = async (url) => {
    if (String(url) === "/openquatt/entities") {
      return {
        ok: true,
        status: 200,
        json: async () => ({
          entities: { hp1Generation: textEntity("V1.5") },
          missing: ["hp1GenerationVariant", "hp1CustomerModelCode"],
        }),
      };
    }
    return { ok: true, status: 200 };
  };

  await triggerNamedButtonGroup(["hp1GenerationDetect"], {
    busyAction: "odu-generation-detect-all",
    refreshKeys: refreshKeys.slice(0, 3),
  });

  assert.equal(state.entities.hp1Generation.value, "V1.5");
  assert.ok(state.optionalMissingEntities.hp1GenerationVariant);
  assert.ok(state.optionalMissingEntities.hp1CustomerModelCode);
  assert.equal(state.controlError, "");
});
