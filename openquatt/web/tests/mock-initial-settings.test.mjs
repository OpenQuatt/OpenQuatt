import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import vm from "node:vm";
import test from "node:test";
import { ENTITY_DEFS } from "../js/src/core/config.js";
globalThis.__OQ_PREVIEW__ = false;
globalThis.localStorage = { getItem: () => null };
const { state } = await import("../js/src/core/state.js");
const { getInitialSettingsReadyKeys, isInitialSettingsReady } = await import("../js/src/core/entity-sync.js");

test("default preview supplies every installation readiness value", async () => {
  const window = {
    crypto: globalThis.crypto,
    location: { href: "http://localhost/dev.html?view=settings&section=installation", search: "?view=settings&section=installation" },
    addEventListener() {}, dispatchEvent() {}, setInterval() {}, setTimeout() {},
    localStorage: { getItem() { return null; }, setItem() {} },
  };
  const context = { window, document: { querySelector() { return null; }, querySelectorAll() { return []; } }, URL, URLSearchParams, CustomEvent: class {}, console };
  for (const file of ["mock-scenarios.js", "mock-incident-scenarios.js", "mock-entity-defs.js", "mock-fixtures.js", "mock-device.js"]) {
    vm.runInNewContext(await readFile(new URL(`../js/${file}`, import.meta.url), "utf8"), context, { filename: file });
  }
  const previous = { appView: state.appView, settingsGroup: state.settingsGroup, entities: state.entities };
  try {
    state.appView = "settings";
    state.settingsGroup = "installation";
    const keys = getInitialSettingsReadyKeys();
    const body = new URLSearchParams({ detail: "all", entities: keys.map((key) => [key, ENTITY_DEFS[key].domain, ENTITY_DEFS[key].name].join("\t")).join("\n") });
    const response = await window.fetch("/openquatt/entities", { method: "POST", body: body.toString() });
    const payload = await response.json();
    state.entities = payload.entities;
    assert.deepEqual(Array.from(payload.missing), []);
    assert.equal(state.entities.otbConnectionState.value, "ot_verified");
    assert.equal(isInitialSettingsReady(), true, "preview must not exhaust the five-second readiness fallback");
  } finally {
    Object.assign(state, previous);
  }
});

async function loadPreviewMock(search) {
  const window = {
    crypto: globalThis.crypto,
    location: { href: `http://localhost/dev.html${search}`, search },
    addEventListener() {}, dispatchEvent() {}, setInterval() {}, setTimeout() {},
    localStorage: { getItem() { return null; }, setItem() {} },
  };
  const context = { window, document: { querySelector() { return null; }, querySelectorAll() { return []; } }, URL, URLSearchParams, TextEncoder, CustomEvent: class {}, console };
  for (const file of ["mock-scenarios.js", "mock-incident-scenarios.js", "mock-entity-defs.js", "mock-fixtures.js", "mock-device.js"]) {
    vm.runInNewContext(await readFile(new URL(`../js/${file}`, import.meta.url), "utf8"), context, { filename: file });
  }
  return window;
}

async function readPerformancePromptHandled(window) {
  const definition = ENTITY_DEFS.performanceTelemetryPromptHandled;
  const body = new URLSearchParams({ detail: "all", entities: ["performanceTelemetryPromptHandled", definition.domain, definition.name].join("\t") });
  const response = await window.fetch("/openquatt/entities", { method: "POST", body: body.toString() });
  return (await response.json()).entities.performanceTelemetryPromptHandled.value;
}

test("demo does not invite visitors to share performance data unless requested", async () => {
  assert.equal(await readPerformancePromptHandled(await loadPreviewMock("?view=overview")), true);
  assert.equal(await readPerformancePromptHandled(await loadPreviewMock("?view=overview&performancePrompt=1")), false);
});

test("BLE mock checks CSRF and revision, keeps room selection independent and enforces humidity", async () => {
  const window = await loadPreviewMock("?view=settings");
  const readSelectors = async () => {
    const keys = ["roomTempSource", "coolingDewPointSource"];
    const body = new URLSearchParams({ detail: "all", entities: keys.map(key =>
      [key, ENTITY_DEFS[key].domain, ENTITY_DEFS[key].name].join("\t")).join("\n") });
    return (await (await window.fetch("/openquatt/entities", { method: "POST", body })).json()).entities;
  };
  const selectors = await readSelectors();
  assert.ok(selectors.roomTempSource.option.includes("BLE"));
  assert.ok(selectors.coolingDewPointSource.option.includes("BLE"));
  const status = async () => (await window.fetch("/openquatt/ble/status")).json();
  const initial = await status();
  let revision = initial.revision;
  const post = (path, values) => window.fetch("/openquatt/ble/" + path, { method: "POST",
    body: new URLSearchParams({ csrf_token: initial.csrf_token, revision, ...values }) });
  assert.equal((await post("save", { action: "enable", enabled: 1, csrf_token: "" })).status, 403);
  assert.equal((await post("save", { action: "enable", enabled: 1, revision: 0 })).status, 409);
  revision = (await (await post("save", { action: "enable", enabled: 1 })).json()).revision;
  assert.equal((await post("scan", { active: 1 })).status, 202);
  const devices = (await status()).discovered;
  const save = (device, useDew) => post("save", { action: "upsert", mac: device.mac, protocol: device.protocol,
    name: device.name, use_dew_point: useDew });
  const thermometer = devices.find(device => device.humidity_pct == null);
  assert.equal((await save(thermometer, 1)).status, 400);
  revision = (await (await save(thermometer, 0)).json()).revision;
  revision = (await (await post("save", { action: "room", mac: thermometer.mac })).json()).revision;
  for (const sensor of devices.filter(device => device.humidity_pct != null)) {
    revision = (await (await save(sensor, 1)).json()).revision;
  }
  const saved = await status();
  assert.equal(saved.room_mac, thermometer.mac);
  assert.equal(saved.required_count, 2);
  assert.equal(saved.dew_point_c, 13);
  assert.deepEqual(await readSelectors(), selectors);
  revision = (await (await post("save", { action: "enable", enabled: 0 })).json()).revision;
  assert.equal((await status()).dew_valid, false);
  assert.equal((await status()).dew_required, true);
  await post("save", { action: "remove", mac: thermometer.mac });
  assert.equal((await status()).room_mac, "");
});
