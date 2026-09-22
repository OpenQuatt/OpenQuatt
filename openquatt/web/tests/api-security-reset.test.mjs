import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
const { state } = await import("../js/src/core/state.js");
const { refreshApiSecurityStatus, resetApiSecurity } = await import("../js/src/features/security-actions.js");

function setup(t, handler, confirm = true) {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  t.after(() => { globalThis.window = originalWindow; globalThis.fetch = originalFetch; });
  state.authStatus = { enabled: true, csrf_token: "test-token" };
  state.apiSecurityBusy = false;
  state.apiSecurityNotice = "";
  state.apiSecurityError = "";
  state.apiSecurityActionError = "";
  globalThis.window = { confirm: () => confirm, setTimeout: callback => callback() };
  globalThis.fetch = handler;
}

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

test("API reset rejection permits an explicit retry", async t => {
  setup(t, async () => ({ status: 403 }));
  await resetApiSecurity();
  assert.equal(state.apiSecurityBusy, false);
  assert.match(state.apiSecurityActionError, /afgewezen/);
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

test("API reset never retries an ambiguous accepted request", async t => {
  let requests = 0;
  setup(t, async () => { ++requests; throw Error("connection lost"); });
  await resetApiSecurity();
  await resetApiSecurity();
  assert.equal(requests, 1);
  assert.match(state.apiSecurityNotice, /niet automatisch herhaald/);
});
