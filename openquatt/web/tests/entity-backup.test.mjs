import assert from "node:assert/strict";
import test from "node:test";

globalThis.window = { location: { pathname: "/" } };
globalThis.__OQ_PREVIEW__ = false;

const { getEntityBackupSwitchState, verifyEntityBackupSelectState, verifyEntityBackupSwitchState } = await import("../js/src/core/entity-backup.js");

test("backup switch state parser accepts ESPHome boolean and text states", () => {
  assert.equal(getEntityBackupSwitchState({ value: true }), true);
  assert.equal(getEntityBackupSwitchState({ state: "OFF" }), false);
  assert.equal(getEntityBackupSwitchState({ state: "unknown" }), null);
});

test("backup switch verification reads the controller state without cache", async () => {
  const originalFetch = globalThis.fetch;
  let request = null;
  globalThis.fetch = async (url, options) => {
    request = { url, options };
    return { ok: true, json: async () => ({ state: "OFF" }) };
  };

  try {
    assert.equal(await verifyEntityBackupSwitchState("usageTelemetryEnabled", false), true);
    assert.match(request.url, /switch\/Usage%20statistics$/);
    assert.equal(request.options.cache, "no-store");
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("backup select verification requires the controller to echo the expected option", async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => ({ ok: true, json: async () => ({ value: "HA input", state: "HA input" }) });

  try {
    assert.equal(await verifyEntityBackupSelectState("heatingEnableSource", "HA input"), true);
    assert.equal(await verifyEntityBackupSelectState("heatingEnableSource", "CIC"), false);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("backup switch verification fails closed when telemetry remains enabled", async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => ({ ok: true, json: async () => ({ state: "ON" }) });

  try {
    assert.equal(await verifyEntityBackupSwitchState("usageTelemetryEnabled", false), false);
  } finally {
    globalThis.fetch = originalFetch;
  }
});
