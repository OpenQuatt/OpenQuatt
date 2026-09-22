import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/dev.html" },
  setTimeout,
  clearTimeout,
  localStorage: { getItem: () => null, setItem: () => {} },
  sessionStorage: { getItem: () => null, setItem: () => {}, removeItem: () => {} },
};

const {
  postDebugRecordingDevice,
  refreshDebugRecordingDeviceStatus,
} = await import("../js/src/features/debug-recording.js");
const { state } = await import("../js/src/core/state.js");

function seedRecorderStatus(overrides = {}) {
  state.debugRecordingDeviceStatus = {
    ok: true,
    available: true,
    active: false,
    sample_count: 0,
    csrf_token: "test-debug-csrf-token",
    ...overrides,
  };
  state.debugRecordingActive = Boolean(state.debugRecordingDeviceStatus.active);
  state.debugRecordingError = "";
  state.debugRecordingDevicePollTimer = null;
}

test("alle muterende debugrequests sturen het firmware-CSRF-token mee", async (t) => {
  const originalFetch = window.fetch;
  t.after(() => {
    window.fetch = originalFetch;
  });
  seedRecorderStatus();
  const requests = [];
  window.fetch = async (url, options) => {
    requests.push({ url, options });
    return { ok: true, status: 200, json: async () => ({ ok: true }) };
  };

  for (const path of ["configure?reset=1", "start?rolling=1", "restart", "stop", "enabled"]) {
    await postDebugRecordingDevice(path, path === "enabled" ? { enabled: "1" } : { entities: "key\tsensor\tName" });
  }

  assert.equal(requests.length, 5);
  for (const { options } of requests) {
    assert.equal(options.method, "POST");
    assert.equal(new URLSearchParams(options.body).get("csrf_token"), "test-debug-csrf-token");
  }
});

test("een definitieve 403 ververst het CSRF-token en herhaalt de mutatie eenmaal", async (t) => {
  const originalFetch = window.fetch;
  t.after(() => {
    window.fetch = originalFetch;
  });
  seedRecorderStatus();
  const postedTokens = [];
  window.fetch = async (_url, options = {}) => {
    if (options.method !== "POST") {
      return {
        ok: true,
        status: 200,
        json: async () => ({ ok: true, available: true, csrf_token: "rotated-debug-csrf-token" }),
      };
    }
    postedTokens.push(new URLSearchParams(options.body).get("csrf_token"));
    if (postedTokens.length === 1) return { ok: false, status: 403 };
    return { ok: true, status: 200, json: async () => ({ ok: true }) };
  };

  await postDebugRecordingDevice("stop");

  assert.deepEqual(postedTokens, ["test-debug-csrf-token", "rotated-debug-csrf-token"]);
});

test("een tijdelijke statusfout bewaart de laatst bekende opname en plant een retry", async (t) => {
  const originalFetch = window.fetch;
  const originalSetTimeout = window.setTimeout;
  const originalClearTimeout = window.clearTimeout;
  t.after(() => {
    window.fetch = originalFetch;
    window.setTimeout = originalSetTimeout;
    window.clearTimeout = originalClearTimeout;
  });
  const lastKnownStatus = {
    ok: true,
    available: true,
    active: true,
    rolling: true,
    sample_count: 42,
    csrf_token: "test-debug-csrf-token",
  };
  seedRecorderStatus(lastKnownStatus);
  state.systemModal = "debug-recording";
  let retryDelay = 0;
  window.fetch = async () => {
    throw new Error("tijdelijk offline");
  };
  window.setTimeout = (_callback, delay) => {
    retryDelay = delay;
    return 123;
  };
  window.clearTimeout = () => {};

  await refreshDebugRecordingDeviceStatus({ silent: true });

  assert.equal(state.debugRecordingDeviceStatus.sample_count, 42);
  assert.equal(state.debugRecordingDeviceStatus.available, true);
  assert.match(state.debugRecordingError, /tijdelijk offline/);
  assert.ok(retryDelay >= 4000 && retryDelay <= 30000);
});
