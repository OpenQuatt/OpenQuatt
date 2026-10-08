import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { clearTimeout, setTimeout, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const { commitWebAuthChanges, commitDisableWebAuth, refreshAuthStatus } = await import("../js/src/features/security-actions.js");
const { renderLoginModal } = await import("../js/src/features/security-access.js");

function status(overrides = {}) {
  return { enabled: true, setup_window_active: false, username: "old-user", source: "runtime-credentials",
    csrf_token: "token", busy: false, pending_reboot: false, error: "", generation: 7, ...overrides };
}

function response(payload, code = 200) {
  return { ok: code >= 200 && code < 300, status: code, json: async () => payload };
}

function setup(t, fetchImplementation) {
  const originalFetch = globalThis.fetch;
  const originalState = { ...state };
  const originalTimeout = window.setTimeout;
  t.after(() => {
    globalThis.fetch = originalFetch;
    Object.assign(state, originalState);
    window.setTimeout = originalTimeout;
    setLocale("nl", { persist: false });
    setRenderCallback(null);
  });
  state.authStatus = status({ generation: 6 });
  state.authBusy = false;
  state.authWriteUncertain = false;
  state.authPendingGeneration = null;
  state.authError = "";
  state.authNotice = "";
  state.authDraftUsername = "new-user";
  state.authDraftCurrentPassword = "old-pass";
  state.authDraftNewPassword = "new-pass";
  state.authDraftConfirmPassword = "new-pass";
  state.systemModal = "login";
  window.setTimeout = (callback, delay) => {
    if (delay === 100) { queueMicrotask(callback); return 0; }
    return originalTimeout(callback, delay);
  };
  setRenderCallback(() => {});
  globalThis.fetch = fetchImplementation;
}

for (const disable of [false, true]) {
  test(`login ${disable ? "disable" : "change"} confirms persistence before claiming success`, async t => {
    let posts = 0;
    let reads = 0;
    setup(t, async (url, options = {}) => {
      if (options.method === "POST") {
        posts += 1;
        assert.equal(url, disable ? "/auth/disable" : "/auth/change");
        const body = new URLSearchParams(options.body);
        assert.equal(body.get("current_password"), "old-pass");
        if (!disable) assert.equal(body.get("new_username"), "new-user");
        return response({ accepted: true, generation: 7 }, 202);
      }
      reads += 1;
      assert.equal(state.authNotice.includes("opgeslagen"), false, "queued is not stored");
      assert.equal(state.authBusy, true);
      assert.doesNotMatch(renderLoginModal(), /<strong>Opgeslagen<\/strong>/);
      return response(status({ busy: reads < 2, pending_reboot: reads === 2 }));
    });
    const operation = disable ? commitDisableWebAuth() : commitWebAuthChanges();
    await commitWebAuthChanges(); // overlapping action must be ignored
    await operation;
    assert.equal(posts, 1);
    assert.equal(reads, 2);
    assert.equal(state.authStatus.username, "old-user");
    assert.equal(state.authStatus.enabled, true, "active access is immutable until reboot");
    assert.match(state.authNotice, /opgeslagen.*herstart/);
    assert.equal(state.authError, "");
    assert.equal(state.authDraftCurrentPassword, "");
    assert.equal(state.authDraftNewPassword, "");
    assert.match(renderLoginModal(), /<strong>Opgeslagen<\/strong>/);
    const saveButton = renderLoginModal().match(/<button[^>]*data-oq-action="save-web-auth"[^>]*>/)?.[0];
    assert.match(saveButton, /disabled/);
    await commitDisableWebAuth();
    assert.equal(posts, 1, "no second write while restart is pending");
  });
}

for (const failure of ["persist", "offline", "wrong-generation", "never-completes", "lost-ack", "reboot-same-generation", "reboot-cached-generation"]) {
  test(`login update ${failure} never becomes a false success or automatic retry`, async t => {
    let posts = 0;
    let reads = 0;
    setup(t, async (_url, options = {}) => {
      if (options.method === "POST") {
        posts += 1;
        if (failure === "lost-ack") throw new Error("connection lost");
        return response({ accepted: true, generation: 7 }, 202);
      }
      reads += 1;
      if (failure === "offline") throw new Error("connection lost");
      if (failure === "reboot-cached-generation") {
        // Background polling reached another boot's different job. The old
        // connection then drops before this operation's own read completes.
        state.authStatus = status({ csrf_token: "new-boot-token", pending_reboot: true });
        throw new Error("connection lost");
      }
      return response(status({ busy: failure === "never-completes", pending_reboot: failure === "wrong-generation",
        ...(failure === "reboot-same-generation" ? { csrf_token: "new-boot-token", pending_reboot: true } : {}),
        generation: failure === "wrong-generation" ? 6 : 7, error: failure === "persist" ? "persist_failed" : "" }));
    });
    await commitWebAuthChanges();
    assert.equal(posts, 1);
    assert.equal(state.authNotice, "");
    assert.notEqual(state.authError, "");
    assert.equal(state.authBusy, false);
    if (failure === "persist") {
      assert.match(state.authError, /niet betrouwbaar/);
      await refreshAuthStatus({ force: true });
      assert.match(state.authError, /niet betrouwbaar/, "a successful read preserves persistence failure");
      assert.equal(state.authWriteUncertain, false);
    } else {
      assert.match(state.authError, /niet bevestigd/);
      await commitDisableWebAuth();
      await commitWebAuthChanges();
      assert.equal(posts, 1, "uncertain outcome locks further writes until reconnect/reload");
      if (failure.startsWith("reboot-")) {
        globalThis.fetch = async () => response(status({ csrf_token: "new-boot-token", pending_reboot: true }));
        await refreshAuthStatus({ force: true });
        assert.equal(state.authWriteUncertain, true, "another boot's matching generation cannot clear uncertainty");
      }
    }
    if (failure === "never-completes") assert.equal(reads, 10);
  });
}

test("current password and the physical setup gate remain required", async t => {
  let requests = 0;
  setup(t, async () => { requests += 1; throw new Error("must not request"); });
  state.authDraftCurrentPassword = "";
  await commitWebAuthChanges();
  await commitDisableWebAuth();
  assert.equal(requests, 0);
  state.authStatus = status({ enabled: false });
  await commitWebAuthChanges();
  assert.equal(requests, 0);
  assert.match(state.authError, /herstelknop/);
});

test("a queued rejection permits explicit correction without resending by itself", async t => {
  let posts = 0;
  setup(t, async () => { posts += 1; return response({ error: "invalid_current_password" }, 403); });
  await commitWebAuthChanges();
  assert.equal(posts, 1);
  assert.equal(state.authWriteUncertain, false);
  assert.equal(state.authDraftNewPassword, "new-pass");
  assert.match(state.authError, /invalid_current_password/);
});

test("late background status cannot replace a confirmed job or discard new form drafts", async t => {
  let releaseOld;
  const oldRead = new Promise(resolve => { releaseOld = resolve; });
  let reads = 0;
  setup(t, async (_url, options = {}) => {
    if (options.method === "POST") return response({ accepted: true, generation: 7 }, 202);
    if (++reads === 1) {
      await oldRead;
      return response(status({ generation: 6, username: "stale-user" }));
    }
    return response(status({ pending_reboot: true }));
  });
  const background = refreshAuthStatus({ force: true });
  await commitWebAuthChanges();
  releaseOld();
  await background;
  assert.equal(state.authStatus.username, "old-user");
  assert.equal(state.authStatus.generation, 7);
  assert.equal(state.authStatus.pending_reboot, true);
  assert.equal(state.authDraftUsername, "new-user");
});

test("job-status refresh preserves drafts and login copy switches between NL and EN", async t => {
  setup(t, async () => response(status({ busy: true })));
  await refreshAuthStatus({ force: true });
  assert.equal(state.authDraftCurrentPassword, "old-pass");
  assert.equal(state.authDraftNewPassword, "new-pass");
  assert.match(renderLoginModal(), /na herstart actief/);
  setLocale("en", { persist: false });
  assert.match(renderLoginModal(), /takes effect after restart/);
  state.authStatus.pending_reboot = true;
  assert.match(renderLoginModal(), /Reopen the web app afterwards/);
});
