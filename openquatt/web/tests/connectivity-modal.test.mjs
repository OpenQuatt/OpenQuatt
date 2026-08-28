import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  clearTimeout,
  location: { pathname: "/" },
  localStorage: { getItem: () => null },
  setTimeout,
};

const { state } = await import("../js/src/core/state.js");
const { getConnectivityModalRows, patchHeaderDom, renderSystemModal } = await import("../js/src/features/header-status.js");
const installationSource = await readFile(new URL("../js/src/settings/installation.js", import.meta.url), "utf8");

function textEntity(value, extra = {}) {
  return { state: value, value, ...extra };
}

function resetConnectivityState(connection) {
  state.entities = {
    connectionText: textEntity(connection),
    preferredConnection: textEntity(connection, { option: ["WiFi", "Ethernet"] }),
    wifiSignal: textEntity(-61, { uom: "dBm" }),
    wifiSsid: textEntity("OpenQuatt-test"),
  };
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.deviceReconnectMode = "";
  state.entitySyncFailureCount = 0;
  state.lastEntityResponseAt = Date.now();
  state.lastEntitySyncAt = state.lastEntityResponseAt;
  state.systemModal = "connectivity";
}

test("Ethernet verbergt verouderde WiFi-details in de connectiviteitsmodal", () => {
  resetConnectivityState("Ethernet");

  const rows = getConnectivityModalRows();
  assert.deepEqual(rows.slice(0, 2), [
    ["Netwerkstatus", "Verbonden"],
    ["Actieve verbinding", "Ethernet"],
  ]);
  assert.equal(rows.some(([label]) => label === "WiFi SSID" || label === "WiFi signaal"), false);

  const markup = renderSystemModal();
  assert.match(markup, /Voorkeursverbinding/);
  assert.match(markup, /data-oq-field="preferredConnection"/);
  assert.doesNotMatch(markup, /WiFi signaal/);
});

test("WiFi toont de bijbehorende SSID en signaalsterkte", () => {
  resetConnectivityState("WiFi");

  const rows = getConnectivityModalRows();
  assert.equal(rows.some(([label, value]) => label === "WiFi SSID" && value === "OpenQuatt-test"), true);
  assert.equal(rows.some(([label, value]) => label === "WiFi signaal" && value === "-61 dBm"), true);
});

test("een ontbrekende actieve verbinding toont geen verouderde WiFi-details", () => {
  resetConnectivityState("Not connected");

  const rows = getConnectivityModalRows();
  assert.equal(rows.some(([label, value]) => label === "Actieve verbinding" && value === "Niet verbonden"), true);
  assert.equal(rows.some(([label]) => label === "WiFi SSID" || label === "WiFi signaal"), false);
});

test("oudere WiFi-firmware zonder runtimeverbinding behoudt WiFi-details", () => {
  resetConnectivityState("WiFi");
  delete state.entities.connectionText;
  delete state.entities.preferredConnection;

  const rows = getConnectivityModalRows();
  assert.equal(rows.some(([label]) => label === "Actieve verbinding"), false);
  assert.equal(rows.some(([label]) => label === "WiFi signaal"), true);
  assert.doesNotMatch(renderSystemModal(), /Voorkeursverbinding/);
});

test("Diagnostiek bevat geen losse verbindingsvelden meer", () => {
  assert.doesNotMatch(installationSource, /dataValue: "ip"/);
  assert.doesNotMatch(installationSource, /dataValue: "activeConnection"/);
  assert.doesNotMatch(installationSource, /renderSettingsSelectField\(\s*"preferredConnection"/);
});

test("een open connectiviteitsmodal wordt volledig ververst bij een verbindingswijziging", () => {
  resetConnectivityState("Ethernet");
  state.root = {};
  assert.equal(patchHeaderDom(), false);
  state.root = null;
});
