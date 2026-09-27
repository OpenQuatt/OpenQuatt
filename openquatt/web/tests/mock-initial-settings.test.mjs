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
