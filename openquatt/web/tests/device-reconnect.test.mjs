import assert from "node:assert/strict";
import test from "node:test";

const timers = [];
let reloadCount = 0;
const originalFetch = globalThis.fetch;

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  clearTimeout(timer) {
    timer.cancelled = true;
  },
  location: {
    href: "http://openquatt.local/?view=settings",
    reload() {
      reloadCount += 1;
    },
  },
  localStorage: { getItem: () => null },
  matchMedia: () => ({ matches: false }),
  setTimeout(callback, delay) {
    const timer = { callback, cancelled: false, delay };
    timers.push(timer);
    return timer;
  },
};

const { state } = await import("../js/src/core/state.js");
const { noteEntityRefreshFailure, noteEntityRefreshSuccess } = await import("../js/src/core/entity-sync.js");
const { triggerNamedButton } = await import("../js/src/core/entity-write-actions.js");
const { requestFirmwareOta } = await import("../js/src/features/firmware-actions.js");
const {
  OTA_REFRESH_DELAY_MS,
  armOtaRefresh,
  awaitOtaEvidence,
  beginDeviceReconnect,
  clearOtaRefresh,
  clearRestartRefresh,
  clearDeviceReconnect,
  scheduleOtaRefresh,
} = await import("../js/src/core/device-reconnect.js");

test.afterEach(() => {
  clearDeviceReconnect();
  clearOtaRefresh();
  clearRestartRefresh();
  state.entities = {};
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  timers.length = 0;
  reloadCount = 0;
  globalThis.fetch = originalFetch;
});

test("a reboot unit change overrides stale uptime metadata", async () => {
  state.entities = {
    firmwareUpdateStatus: { state: "Idle" },
    projectVersionText: { state: "v0.42.0" },
    uptime: { state: "1.00 h", value: 1 },
  };
  globalThis.fetch = async () => {
    throw new TypeError("Failed to fetch");
  };

  await assert.rejects(requestFirmwareOta("/ota", { method: "POST" }), /Failed to fetch/);

  state.entities.firmwareUpdateStatus = { state: "Idle" };
  state.entities.uptime = { state: "4 s", value: 4, uom: "h" };
  noteEntityRefreshSuccess();

  const refreshTimer = timers.find((timer) => timer.delay === OTA_REFRESH_DELAY_MS && !timer.cancelled);
  assert.ok(refreshTimer);
  await refreshTimer.callback();

  assert.equal(reloadCount, 1);
  assert.equal(state.ota.on, false);
});

test("an unreachable device recovery does not prove that OTA started", async () => {
  state.entities = {
    firmwareUpdateStatus: { state: "Idle" },
    projectVersionText: { state: "v0.42.0" },
    uptime: { state: "0.02 h", value: 60 / 3600 },
  };
  globalThis.fetch = async () => {
    throw new TypeError("Failed to fetch");
  };

  await assert.rejects(requestFirmwareOta("/ota", { method: "POST" }), /Failed to fetch/);
  noteEntityRefreshFailure("Failed to fetch");

  state.entities.firmwareUpdateStatus = { state: "Idle" };
  state.entities.uptime = { state: "60 s", value: 60, uom: "h" };
  noteEntityRefreshSuccess();

  assert.equal(state.ota.id.delay, 300000);
  assert.equal(state.ota.wait, true);
  assert.equal(reloadCount, 0);

  const evidenceTimer = timers.find((timer) => timer.delay === 300000 && !timer.cancelled);
  assert.ok(evidenceTimer);
  evidenceTimer.callback();

  assert.equal(state.ota.on, false);
  assert.equal(reloadCount, 0);
});

test("missing baseline uptime accepts a boot inferred after the OTA request", async () => {
  state.entities = {
    projectVersionText: { state: "v0.42.0" },
    uptime: { state: "NA", value: null },
  };
  globalThis.fetch = async () => {
    throw new TypeError("Failed to fetch");
  };

  await assert.rejects(requestFirmwareOta("/ota", { method: "POST" }), /Failed to fetch/);

  state.ota.base[2] = performance.now() - 120000;
  state.entities.uptime = { state: "60 s", value: 60 };
  noteEntityRefreshSuccess();

  assert.equal(state.ota.id.delay, OTA_REFRESH_DELAY_MS);
  assert.equal(state.ota.wait, false);
});

test("an accepted low-uptime OTA reloads after an observed outage", async () => {
  state.entities = {
    projectVersionText: { state: "v0.42.0" },
    uptime: { state: "0.02 h", value: 60 / 3600 },
  };
  globalThis.fetch = async () => ({ ok: true });

  await requestFirmwareOta("/ota", { method: "POST" });
  awaitOtaEvidence();
  noteEntityRefreshFailure("Failed to fetch");

  state.entities.uptime = { state: "60 s", value: 60, uom: "h" };
  noteEntityRefreshSuccess();

  assert.equal(state.ota.id.delay, OTA_REFRESH_DELAY_MS);
  assert.equal(state.ota.wait, false);
});

test("a rounded uptime state does not fake a post-request reboot", async () => {
  state.entities = {
    projectVersionText: { state: "v0.42.0" },
    uptime: { state: "NA", value: null },
  };
  globalThis.fetch = async () => {
    throw new TypeError("Failed to fetch");
  };

  await assert.rejects(requestFirmwareOta("/ota", { method: "POST" }), /Failed to fetch/);

  state.ota.base[2] = performance.now() - 5000;
  state.entities.uptime = { state: "0.00 h", value: 6 / 3600, uom: "h" };
  noteEntityRefreshSuccess();

  assert.equal(state.ota.id.delay, 300000);
  assert.equal(state.ota.wait, true);
});

test("a changed firmware version confirms an ambiguously acknowledged OTA", async () => {
  state.entities = {
    projectVersionText: { state: "v0.42.0" },
    uptime: { state: 3600 },
  };
  globalThis.fetch = async () => {
    throw new TypeError("Failed to fetch");
  };

  await assert.rejects(requestFirmwareOta("/ota", { method: "POST" }), /Failed to fetch/);

  state.entities.projectVersionText = { state: "v0.43.0" };
  noteEntityRefreshSuccess();

  assert.equal(state.ota.id.delay, OTA_REFRESH_DELAY_MS);
  assert.equal(state.ota.wait, false);
});

test("an accepted OTA does not reload from a proactive reconnect phase", async () => {
  state.entities.uptime = { state: "0.02 h", value: 60 / 3600 };
  globalThis.fetch = async () => ({ ok: true });

  await requestFirmwareOta("/ota", { method: "POST" });
  awaitOtaEvidence();
  beginDeviceReconnect("ota");
  noteEntityRefreshSuccess();

  assert.equal(state.ota.on, true);
  assert.equal(state.ota.id.delay, 300000);
  assert.equal(state.ota.wait, true);
});

test("an OTA entity poll does not reload before install completion", () => {
  armOtaRefresh();

  noteEntityRefreshSuccess();

  assert.equal(state.ota.on, true);
  assert.equal(state.ota.id, null);
});

test("an accepted restart reloads immediately when data returns after the outage", async () => {
  state.entities.uptime = { state: "1.00 h", value: 1 };
  globalThis.fetch = async () => ({ ok: true });

  await triggerNamedButton("restartAction", {
    successNotice: "OpenQuatt wordt opnieuw opgestart.",
    errorPrefix: "Herstart mislukt",
    reconnectMode: "restart",
  });

  assert.equal(state.restartRefresh.on, true);
  assert.equal(state.restartRefresh.ok, 1);
  assert.equal(state.restartRefresh.wait, true);

  noteEntityRefreshSuccess();

  assert.equal(state.restartRefresh.wait, true);
  assert.equal(state.deviceReconnectRecoveryStartedAt, 0);
  assert.equal(reloadCount, 0);

  noteEntityRefreshFailure("Failed to fetch");
  noteEntityRefreshSuccess();

  const refreshTimer = state.restartRefresh.id;
  assert.ok(refreshTimer);
  assert.equal(refreshTimer.delay, 0);
  noteEntityRefreshSuccess();
  assert.equal(state.restartRefresh.id, refreshTimer);
  assert.equal(state.deviceReconnectRecoveryStartedAt, 0);
  refreshTimer.callback();

  assert.equal(reloadCount, 1);
  assert.equal(state.restartRefresh.on, false);
});

test("a transient post-reboot entity error keeps the restart modal open until reload", async () => {
  state.entities.uptime = { state: "1.00 h", value: 1 };
  globalThis.fetch = async () => ({ ok: true });

  await triggerNamedButton("restartAction", {
    successNotice: "OpenQuatt wordt opnieuw opgestart.",
    errorPrefix: "Herstart mislukt",
    reconnectMode: "restart",
  });

  noteEntityRefreshFailure("Uptime HTTP 503");

  assert.equal(state.deviceReconnectMode, "restart");
  assert.equal(state.deviceReconnectRecoveryStartedAt, 0);
  assert.equal(state.restartRefresh.wait, true);

  state.entities.uptime = { state: "4 s", value: 4, uom: "h" };
  noteEntityRefreshSuccess();

  const refreshTimer = state.restartRefresh.id;
  assert.ok(refreshTimer);
  assert.equal(refreshTimer.delay, 0);
  assert.equal(state.deviceReconnectMode, "restart");
  assert.equal(state.deviceReconnectRecoveryStartedAt, 0);
  refreshTimer.callback();

  assert.equal(reloadCount, 1);
});

test("a lost restart acknowledgement reloads immediately when data returns", async () => {
  state.entities.uptime = { state: "1.00 h", value: 1 };
  globalThis.fetch = async () => {
    throw new TypeError("Failed to fetch");
  };

  await triggerNamedButton("restartAction", {
    successNotice: "OpenQuatt wordt opnieuw opgestart.",
    errorPrefix: "Herstart mislukt",
    reconnectMode: "restart",
  });

  assert.equal(state.restartRefresh.on, true);
  assert.equal(state.restartRefresh.ok, 2);
  assert.equal(state.restartRefresh.wait, true);
  assert.equal(state.deviceReconnectMode, "restart");
  assert.equal(state.controlError, "");

  noteEntityRefreshSuccess();

  const refreshTimer = state.restartRefresh.id;
  assert.ok(refreshTimer);
  assert.equal(refreshTimer.delay, 0);
  assert.equal(state.deviceReconnectRecoveryStartedAt, 0);
  refreshTimer.callback();

  assert.equal(reloadCount, 1);
  assert.equal(state.restartRefresh.on, false);
});

test("an explicit restart rejection cancels its pending page reload", async () => {
  globalThis.fetch = async () => ({ ok: false, status: 503 });

  await triggerNamedButton("restartAction", {
    errorPrefix: "Herstart mislukt",
    reconnectMode: "restart",
  });

  assert.equal(state.restartRefresh.on, false);
  assert.equal(state.deviceReconnectMode, "");
  assert.match(state.controlError, /HTTP 503/);
  assert.equal(reloadCount, 0);
});

test("a rejected OTA cancels its pending page reload", async () => {
  armOtaRefresh();

  scheduleOtaRefresh();
  const refreshTimer = timers[0];
  clearOtaRefresh();
  await refreshTimer.callback();

  assert.equal(refreshTimer.cancelled, true);
  assert.equal(reloadCount, 0);
});

test("an OTA refresh reloads even when explicit cache refresh fails", async () => {
  const requestedUrls = [];
  globalThis.fetch = async (url) => {
    requestedUrls.push(url);
    throw new TypeError("Failed to fetch");
  };

  armOtaRefresh();
  scheduleOtaRefresh();
  const refreshTimer = timers[0];
  await refreshTimer.callback();

  assert.deepEqual(requestedUrls, [
    "http://openquatt.local/?view=settings",
    "/0.css",
    "/0.js",
  ]);
  assert.equal(reloadCount, 1);
  assert.equal(state.ota.on, false);
});

test("cancelling OTA during cache refresh prevents a stale scheduled reload", async () => {
  const fetchResolvers = [];
  globalThis.fetch = () => new Promise((resolve) => {
    fetchResolvers.push(resolve);
  });

  armOtaRefresh();
  scheduleOtaRefresh();
  const refreshTimer = timers[0];
  const refreshPromise = refreshTimer.callback();
  clearOtaRefresh();
  fetchResolvers.forEach((resolve) => resolve({ ok: true, arrayBuffer: async () => new ArrayBuffer(0) }));
  await refreshPromise;

  assert.equal(reloadCount, 0);
  assert.equal(state.ota.on, false);
});

test("a lost OTA acknowledgement enters reconnect recovery", async () => {
  globalThis.fetch = async () => {
    throw new TypeError("Failed to fetch");
  };

  await assert.rejects(requestFirmwareOta("/ota", { method: "POST" }), /Failed to fetch/);

  assert.equal(state.ota.on, true);
  assert.equal(state.ota.wait, true);
  assert.equal(state.deviceReconnectMode, "ota");
});

test("an explicit OTA rejection clears its pending refresh", async () => {
  globalThis.fetch = async () => ({ ok: false, status: 503 });

  await assert.rejects(requestFirmwareOta("/ota", { method: "POST" }), /HTTP 503/);

  assert.equal(state.ota.on, false);
  assert.equal(state.ota.ok, 0);
  assert.equal(state.deviceReconnectMode, "");
});
