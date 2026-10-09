import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  clearTimeout: globalThis.clearTimeout,
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
};

const { ENTITY_DEFS, SETTINGS_BACKUP_SCHEMA_VERSION } = await import("../js/src/core/config.js");
const { state } = await import("../js/src/core/state.js");
const { buildSettingsBackupSnapshot, parseSettingsBackupPayload, restoreSettingsBackup } = await import("../js/src/features/storage-history.js");

const liveInputs = {
  apiInputOutsideTemperature: -40,
  apiInputRoomTemperature: 0,
  apiInputRoomSetpoint: 5,
  apiInputHeatingEnable: true,
  apiInputCoolingEnable: true,
  apiInputCoolingDewPoint: -20,
  apiInputExternalHeatDemand: 9000,
  apiInputHeatingSupplyTarget: 60,
  apiInputHeatingCurveModifier: 10,
};

function withBackupState(run) {
  const previous = {
    drafts: state.drafts,
    entities: state.entities,
    nativeOpen: state.nativeOpen,
    settingsBackupBusy: state.settingsBackupBusy,
    settingsBackupDraft: state.settingsBackupDraft,
    settingsBackupError: state.settingsBackupError,
    settingsBackupMqttPassword: state.settingsBackupMqttPassword,
    settingsBackupRestoreResult: state.settingsBackupRestoreResult,
  };
  state.drafts = {};
  state.entities = {};
  state.nativeOpen = true;
  state.settingsBackupBusy = false;
  state.settingsBackupError = "";
  state.settingsBackupMqttPassword = "";
  state.settingsBackupRestoreResult = null;
  return Promise.resolve().then(run).finally(() => Object.assign(state, previous));
}

test("settings export excludes live API values regardless of their validity and retains source settings", async () => {
  await withBackupState(() => {
    state.entities = {
      ...Object.fromEntries(Object.entries(liveInputs).flatMap(([key, value]) => [
        [key, { value }],
        [`${key}Valid`, { value: key !== "apiInputOutsideTemperature" }],
      ])),
      outsideTempSource: { value: "Auto" },
      roomTempSource: { value: "HA input" },
      heatingEnableSource: { value: "API input" },
      housePower: { value: 8000 },
    };
    const snapshot = buildSettingsBackupSnapshot();
    const values = Object.assign({}, ...Object.values(snapshot.settings));
    for (const key of Object.keys(liveInputs)) {
      assert.equal(Object.hasOwn(values, key), false, `${key} is live input, not a setting`);
    }
    assert.equal(snapshot.settings.sensor_sources.outsideTempSource, "Auto");
    assert.equal(snapshot.settings.sensor_sources.roomTempSource, "HA input");
    assert.equal(snapshot.settings.sensor_sources.heatingEnableSource, "API input");
    assert.equal(snapshot.settings.powerHouse.housePower, 8000);
    assert.equal(snapshot.schema_version, SETTINGS_BACKUP_SCHEMA_VERSION);
  });
});

function createRestoreController() {
  const calls = [];
  const sources = new Map();
  const inputs = Object.fromEntries(Object.keys(liveInputs).map((key) => [key, {
    value: key === "apiInputOutsideTemperature" ? -40 : 21,
    valid: key === "apiInputRoomTemperature",
    lastUpdateMs: key === "apiInputRoomTemperature" ? 1000 : null,
  }]));
  const initialInputs = structuredClone(inputs);
  let housePower = 7020;
  const response = (payload = {}) => ({ ok: true, status: 200, json: async () => payload });
  const entityPayload = (key, domain, name) => {
    const value = inputs[key]?.value ?? sources.get(name)
      ?? (key === "housePower" ? housePower : domain === "select" ? "Disabled" : 0);
    return { value, state: value, min_value: -40, max_value: 30000, step: 0.1 };
  };
  const fetch = async (rawUrl, options = {}) => {
    const url = new URL(String(rawUrl), "http://openquatt.local");
    if (url.pathname === "/openquatt/entities") {
      const rows = String(new URLSearchParams(options.body).get("entities") || "").split("\n").filter(Boolean);
      return response({ entities: Object.fromEntries(rows.map((row) => {
        const [key, domain, name] = row.split("\t");
        return [key, entityPayload(key, domain, name)];
      })), missing: [] });
    }
    if (url.pathname === "/openquatt/service-status") return response({ entities: {} });
    if (url.pathname === "/mqtt/status") return response({ csrf_token: "mock-token" });
    if (url.pathname.startsWith("/mqtt/")) return response({ ok: true });
    const [domain, name, action] = url.pathname.split("/").filter(Boolean).map(decodeURIComponent);
    const key = Object.keys(ENTITY_DEFS).find((key) => ENTITY_DEFS[key].name === name);
    if (options.method === "POST") {
      calls.push(url.pathname);
      if (inputs[key]) {
        inputs[key] = { value: Number(url.searchParams.get("value")), valid: true, lastUpdateMs: 60000 };
      } else if (domain === "select" && action === "set") {
        sources.set(name, url.searchParams.get("option"));
      } else if (key === "housePower") {
        housePower = Number(url.searchParams.get("value"));
      }
      return response();
    }
    return response(entityPayload(key, domain, name));
  };
  return { calls, fetch, inputs, initialInputs, sources, getHousePower: () => housePower };
}

for (const schemaVersion of [1, 2, 3]) {
  for (const outsideSource of ["Auto", "Outdoor unit", "HA input", "API input", "MQTT"]) {
    if (schemaVersion === 1 && outsideSource === "MQTT") continue;
    test(`schema ${schemaVersion} restore skips stale API inputs and preserves ${outsideSource}`, async () => {
      const controller = createRestoreController();
      const previousFetch = globalThis.fetch;
      try {
        globalThis.fetch = controller.fetch;
        await withBackupState(async () => {
          state.settingsBackupDraft = parseSettingsBackupPayload(JSON.stringify({
            schema_version: schemaVersion,
            settings: {
              sensor_sources: {
                ...liveInputs,
                outsideTempSource: outsideSource,
                roomTempSource: "HA input",
                roomSetpointSource: "API input",
                heatingEnableSource: "Disabled",
              },
              powerHouse: { housePower: 8000 },
            },
            ...(outsideSource === "MQTT" ? { mqtt: { enabled: true, broker: "mqtt.local", port: 1883 } } : {}),
          }));
          await restoreSettingsBackup();
          const result = state.settingsBackupRestoreResult;
          assert.ok(result, state.settingsBackupError);
          assert.equal(controller.calls.some((path) => path.includes("api_input_")), false);
          assert.deepEqual(controller.inputs, controller.initialInputs, "restore must not refresh, validate or overwrite live inputs");
          assert.equal(controller.sources.get("Outside Temperature Source"), outsideSource);
          assert.equal(controller.sources.get("Room Temperature Source"), "HA input");
          assert.equal(controller.sources.get("Room Setpoint Source"), "API input");
          assert.equal(controller.getHousePower(), 8000);
          assert.ok(result.applied.includes("housePower"));
          for (const key of Object.keys(liveInputs)) {
            assert.equal(result.applied.includes(key), false);
            assert.ok(result.unknown.some((item) => item.key === key), `${key} must be reported as not restored`);
          }
        });
      } finally {
        globalThis.fetch = previousFetch;
      }
    });
  }
}
