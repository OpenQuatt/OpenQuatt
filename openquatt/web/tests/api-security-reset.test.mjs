import assert from "node:assert/strict";
import test from "node:test";
import { setLocale } from "../js/src/i18n/index.js";

globalThis.__OQ_PREVIEW__ = false;
const { state } = await import("../js/src/core/state.js");
const { refreshApiSecurityStatus, resetApiSecurity, resetWifi } = await import("../js/src/features/security-actions.js");

function setup(t, handler, confirm = true) {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  t.after(() => { globalThis.window = originalWindow; globalThis.fetch = originalFetch; });
  state.authStatus = { enabled: true, csrf_token: "test-token" };
  state.apiSecurityBusy = false;
  state.wifiResetBusy = false;
  state.wifiResetAvailable = true;
  state.wifiResetError = "";
  state.wifiResetActionError = "";
  state.wifiResetNotice = "";
  state.apiSecurityNotice = "";
  state.apiSecurityError = "";
  state.apiSecurityActionError = "";
  globalThis.window = { confirm: () => confirm, setTimeout: callback => callback() };
  globalThis.fetch = handler;
}

test("English reset confirmation and failure retain the firmware request contract", async t => {
  let confirmation = "";
  setup(t, async (url, options) => {
    assert.equal(url, "/wifi/reset");
    assert.equal(options.body.get("confirm"), "RESET_WIFI");
    assert.equal(options.body.get("csrf_token"), "test-token");
    return { status: 403 };
  });
  window.confirm = text => { confirmation = text; return true; };
  setLocale("en", { persist: false });
  try {
    await resetWifi();
    assert.match(confirmation, /Clear Wi-Fi credentials and restart/);
    assert.equal(state.wifiResetActionError, "Reset was rejected. HTTP 403. You can try again.");
    assert.equal(state.wifiResetBusy, false);
  } finally {
    setLocale("nl", { persist: false });
  }
});

test("API reset requires configured auth and explicit confirmation", async t => {
  setup(t, () => { throw Error("must not fetch"); }, false);
  await resetApiSecurity();
  assert.equal(state.apiSecurityBusy, false);
  state.authStatus.enabled = false;
  window.confirm = () => true;
  await resetApiSecurity();
  assert.equal(state.apiSecurityBusy, false);
});

test("API reset sends one confirmed request and reports persistence failure", async t => {
  const requests = [];
  setup(t, async (url, options) => {
    requests.push(url);
    if (options.method === "POST") {
      assert.equal(options.body.get("confirm"), "RESET_API_SECURITY");
      assert.equal(options.body.get("csrf_token"), "test-token");
      return { status: 202 };
    }
    return { json: async () => ({ error: "persist_failed" }) };
  });
  await resetApiSecurity();
  assert.deepEqual(requests, ["/api-security/reset", "/recovery/status"]);
  assert.equal(state.apiSecurityBusy, false);
  assert.match(state.apiSecurityActionError, /niet herstart/);
});

test("API reset never retries an ambiguous accepted request", async t => {
  let requests = 0;
  setup(t, async () => { ++requests; throw Error("connection lost"); });
  await resetApiSecurity();
  await resetApiSecurity();
  assert.equal(requests, 1);
  assert.match(state.apiSecurityNotice, /niet automatisch herhaald/);
});

test("Wi-Fi reset requires capability, login and confirmation", async t => {
  setup(t, () => { throw Error("must not fetch"); });
  state.wifiResetAvailable = false;
  await resetWifi();
  state.wifiResetAvailable = true;
  state.authStatus.enabled = false;
  await resetWifi();
  state.authStatus.enabled = true;
  window.confirm = () => false;
  await resetWifi();
  assert.equal(state.wifiResetBusy, false);
});

test("Wi-Fi reset uses its own confirmation and reports checked failure", async t => {
  const requests = [];
  setup(t, async (url, options) => {
    requests.push(url);
    if (options.method === "POST") {
      assert.equal(options.body.get("confirm"), "RESET_WIFI");
      assert.equal(options.body.get("csrf_token"), "test-token");
      return { status: 202 };
    }
    return { json: async () => ({ error: "persist_failed" }) };
  });
  await resetWifi();
  assert.deepEqual(requests, ["/wifi/reset", "/recovery/status"]);
  assert.equal(state.wifiResetBusy, false);
  assert.match(state.wifiResetActionError, /niet herstart/);
});

test("a definitive reset rejection permits an explicit retry", async t => {
  setup(t, async () => ({ status: 403 }));
  await resetApiSecurity();
  assert.equal(state.apiSecurityBusy, false);
  assert.match(state.apiSecurityActionError, /afgewezen/);
  await resetWifi();
  assert.equal(state.wifiResetBusy, false);
  assert.match(state.wifiResetActionError, /afgewezen/);
});

test("API reset failure survives a successful status refresh", async t => {
  setup(t, async () => ({
    ok: true,
    json: async () => ({ transport_active: true, key_present: true, provisioning_pending: false, provisioning_closed: false }),
  }));
  state.apiSecurityActionError = "Reset mislukt; er is niet herstart. Je kunt opnieuw proberen.";
  await refreshApiSecurityStatus({ force: true });
  assert.match(state.apiSecurityActionError, /niet herstart/);
});

test("Wi-Fi reset never retries an ambiguous request or overlaps API reset", async t => {
  let requests = 0;
  setup(t, async () => { ++requests; throw Error("connection lost"); });
  await resetWifi();
  await resetWifi();
  await resetApiSecurity();
  assert.equal(requests, 1);
  assert.match(state.wifiResetNotice, /niet automatisch herhaald/);
});
