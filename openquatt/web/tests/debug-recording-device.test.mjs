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
  configureDebugRecordingDevice,
  postDebugRecordingDevice,
  refreshDebugRecordingDeviceStatus,
  startDebugRecordingMode,
  stopDebugRecording,
} = await import("../js/src/features/debug-recording.js");
const { DEBUG_RECORDING_KEYS } = await import("../js/src/core/config.js");
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

  for (const path of ["configure?reset=1", "start?rolling=1", "freeze", "stop"]) {
    await postDebugRecordingDevice(path, { entities: "key\tsensor\tName" });
  }

  assert.equal(requests.length, 4);
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

test("de volledige debugset wordt transactioneel in meerdere configure-chunks aangeboden", async (t) => {
  const originalFetch = window.fetch;
  t.after(() => {
    window.fetch = originalFetch;
  });
  seedRecorderStatus();
  const requestedKeys = [];
  let requestedCount = 0;
  let requestCount = 0;
  window.fetch = async (url, options) => {
    requestCount += 1;
    const body = new URLSearchParams(options.body);
    assert.equal(body.get("csrf_token"), "test-debug-csrf-token");
    for (const line of String(body.get("entities") || "").split("\n")) {
      if (!line) continue;
      const [key] = line.split("\t");
      requestedKeys.push(key);
      requestedCount += 1;
    }
    return {
      ok: true,
      status: 200,
      json: async () => ({
        configuration_pending: true,
        pending_requested_field_count: requestedCount,
      }),
    };
  };

  const status = await configureDebugRecordingDevice();
  assert.ok(requestCount > 1, "de request blijft begrensd en wordt gechunkt");
  assert.equal(status.pending_requested_field_count, DEBUG_RECORDING_KEYS.length);
  assert.deepEqual(requestedKeys, DEBUG_RECORDING_KEYS);
});

test("een verloren stopantwoord wordt via status gereconcilieerd", async (t) => {
  const originalFetch = window.fetch;
  t.after(() => {
    window.fetch = originalFetch;
  });
  seedRecorderStatus({ active: true, rolling: false, recording_id: 91, sample_count: 12 });
  let requestCount = 0;
  window.fetch = async (_url, options = {}) => {
    requestCount += 1;
    if (options.method === "POST") throw new Error("antwoord verloren");
    return {
      ok: true,
      status: 200,
      json: async () => ({
        ok: true,
        available: true,
        active: false,
        rolling: false,
        recording_id: 91,
        sample_count: 12,
        csrf_token: "test-debug-csrf-token",
      }),
    };
  };

  await stopDebugRecording();

  assert.equal(requestCount, 2);
  assert.equal(state.debugRecordingActive, false);
  assert.equal(state.debugRecordingError, "");
  assert.match(state.debugRecordingNotice, /bevestiging was vertraagd/);
});

test("een verloren startantwoord wordt niet aan een opname in de andere modus toegeschreven", async (t) => {
  const originalFetch = window.fetch;
  const originalSetTimeout = window.setTimeout;
  const originalClearTimeout = window.clearTimeout;
  t.after(() => {
    window.fetch = originalFetch;
    window.setTimeout = originalSetTimeout;
    window.clearTimeout = originalClearTimeout;
    state.debugRecordingDevicePollTimer = null;
  });
  seedRecorderStatus({ active: false, rolling: false, recording_id: 91 });
  let requestedCount = 0;
  window.setTimeout = () => 123;
  window.clearTimeout = () => {};
  window.fetch = async (url, options = {}) => {
    if (options.method === "POST" && String(url).includes("/configure")) {
      const body = new URLSearchParams(options.body);
      requestedCount += String(body.get("entities") || "").split("\n").filter(Boolean).length;
      return {
        ok: true,
        status: 200,
        json: async () => ({
          configuration_pending: true,
          pending_requested_field_count: requestedCount,
        }),
      };
    }
    if (options.method === "POST") throw new Error("startantwoord verloren");
    return {
      ok: true,
      status: 200,
      json: async () => ({
        ok: true,
        available: true,
        active: true,
        mode: "rolling",
        rolling: true,
        recording_id: 92,
        sample_count: 1,
        csrf_token: "test-debug-csrf-token",
      }),
    };
  };

  await startDebugRecordingMode({ rolling: false, durationMinutes: 15 });

  assert.match(state.debugRecordingError, /kon niet worden gestart/);
  assert.doesNotMatch(state.debugRecordingNotice, /bevestiging was vertraagd/);
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
