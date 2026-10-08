import assert from "node:assert/strict";
import test from "node:test";

const session = new Map();
globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null }, sessionStorage: {
  getItem: (key) => session.get(key), setItem: (key, value) => session.set(key, value),
}, setTimeout, clearTimeout };
const { state } = await import("../js/src/core/state.js");
const { ENTITY_DEFS } = await import("../js/src/core/config.js");
const { shouldOfferPerformanceTelemetryPrompt, savePerformanceTelemetryPromptChoice } = await import("../js/src/core/performance-telemetry-domain.js");
const { deferPerformanceTelemetryPrompt, syncPerformanceTelemetryPrompt, renderPerformanceTelemetryPrompt, handlePerformanceTelemetryPromptAction, handlePerformanceTelemetryPromptKeyDown } = await import("../js/src/features/performance-telemetry-prompt.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
const { setLocale } = await import("../js/src/i18n/index.js");

function reset() {
  session.clear();
  Object.assign(state, { complete: true, loadingEntities: false, nativeOpen: false, systemModal: "", updateModalOpen: false,
    deviceReconnectMode: "", busyAction: "", quickStartModalOpen: true, quickStartModalMode: "", drafts: {},
    performanceTelemetryPromptDeferred: false, performanceTelemetryPromptBusy: false, performanceTelemetryPromptError: "",
    entities: { performanceTelemetryEnabled: { value: false }, performanceTelemetryPromptHandled: { value: false },
      performanceTelemetryChoiceConfigured: { value: true } }, ota: {}, restartRefresh: {} });
  setRenderCallback(null);
}

test("offer only to existing installations with known OFF and unhandled invitation", () => {
  const valid = { setupComplete: true, enabled: false, handled: false };
  assert.equal(shouldOfferPerformanceTelemetryPrompt(valid), true);
  for (const invalid of [{ setupComplete: false }, { setupComplete: null }, { enabled: true }, { enabled: "ON" },
    { enabled: "unknown" }, { enabled: undefined }, { handled: true }, { handled: undefined }, { handled: "unknown" }, { deferred: true }, { blocked: true }]) {
    assert.equal(shouldOfferPerformanceTelemetryPrompt({ ...valid, ...invalid }), false, JSON.stringify(invalid));
  }
  assert.deepEqual(ENTITY_DEFS.performanceTelemetryPromptHandled, {
    domain: "binary_sensor", name: "Performance model validation prompt handled", optional: true,
  });
});

test("legacy configured OFF can be invited, active sharing never is; other dialogs have priority", () => {
  reset(); syncPerformanceTelemetryPrompt(); assert.equal(state.systemModal, "performance-telemetry-prompt");
  state.entities.performanceTelemetryEnabled.value = "true";
  syncPerformanceTelemetryPrompt(); assert.equal(state.systemModal, "");
  for (const blocker of [{ complete: false }, { loadingEntities: true }, { nativeOpen: true }, { updateModalOpen: true },
    { deviceReconnectMode: "restart" }, { busyAction: "other" }, { quickStartModalMode: "generation" }, { systemModal: "login" }]) {
    reset(); Object.assign(state, blocker); syncPerformanceTelemetryPrompt();
    assert.notEqual(state.systemModal, "performance-telemetry-prompt");
  }
  reset(); delete state.entities.performanceTelemetryPromptHandled;
  syncPerformanceTelemetryPrompt(); assert.equal(state.systemModal, "");
});

test("Later and Escape write only to session storage, including reload and storage failure", () => {
  reset(); syncPerformanceTelemetryPrompt();
  deferPerformanceTelemetryPrompt(); assert.equal(state.systemModal, "");
  state.performanceTelemetryPromptDeferred = false; // Reload: session storage survives.
  syncPerformanceTelemetryPrompt(); assert.equal(state.systemModal, "");
  reset(); syncPerformanceTelemetryPrompt();
  let prevented = false;
  assert.equal(handlePerformanceTelemetryPromptKeyDown({ key: "Escape", preventDefault: () => { prevented = true; } }), true);
  assert.equal(prevented, true); assert.equal(state.systemModal, "");
  reset(); syncPerformanceTelemetryPrompt();
  const setter = window.sessionStorage.setItem;
  window.sessionStorage.setItem = () => { throw new Error("blocked"); };
  deferPerformanceTelemetryPrompt(); syncPerformanceTelemetryPrompt();
  assert.equal(state.systemModal, "");
  window.sessionStorage.setItem = setter;
});

test("confirmation needs permission, configured choice and handled status from fresh reads", async () => {
  for (const enabled of [true, false]) {
    let writes = 0;
    assert.equal(await savePerformanceTelemetryPromptChoice({ expectedEnabled: enabled,
      write: async () => { writes++; }, read: async () => ({ enabled, choice: true, handled: true }), wait: async () => {} }), true);
    assert.equal(writes, 1);
    for (const status of [{ enabled, choice: true, handled: false }, { enabled, choice: false, handled: true }, { enabled: !enabled, choice: true, handled: true }]) {
      assert.equal(await savePerformanceTelemetryPromptChoice({ expectedEnabled: enabled, write: async () => {}, read: async () => status, wait: async () => {} }), false);
    }
  }
});

test("lost POST acknowledgement reconciles without another write; offline reads stay unconfirmed", async () => {
  let writes = 0, reads = 0;
  const confirmed = await savePerformanceTelemetryPromptChoice({ expectedEnabled: true,
    write: async () => { writes++; throw new Error("ack lost"); },
    read: async () => { if (++reads < 2) throw new Error("offline"); return { enabled: true, choice: true, handled: true }; }, wait: async () => {} });
  assert.equal(confirmed, true); assert.equal(writes, 1); assert.equal(reads, 2);
  assert.equal(await savePerformanceTelemetryPromptChoice({ expectedEnabled: false,
    write: async () => {}, read: async () => { throw new Error("offline"); }, wait: async () => {} }), false);
});

test("actual action ignores double clicks and close/Escape during saving; closes only after confirmed GETs", async () => {
  reset(); syncPerformanceTelemetryPrompt();
  let release;
  const post = new Promise((resolve) => { release = resolve; });
  let posts = 0;
  globalThis.fetch = async (url, options) => {
    if (options.method === "POST") { posts++; await post; return { ok: true }; }
    return { ok: true, json: async () => ({ value: true, state: true }) };
  };
  const rendered = [];
  setRenderCallback(() => rendered.push(state.systemModal));
  assert.equal(handlePerformanceTelemetryPromptAction("enable-performance-telemetry-prompt"), true);
  handlePerformanceTelemetryPromptAction("decline-performance-telemetry-prompt");
  deferPerformanceTelemetryPrompt();
  assert.equal(state.systemModal, "performance-telemetry-prompt"); assert.equal(posts, 1);
  assert.equal(state.performanceTelemetryPromptBusy, true);
  release();
  for (let n = 0; n < 10 && state.performanceTelemetryPromptBusy; n++) await new Promise((done) => setImmediate(done));
  assert.equal(state.performanceTelemetryPromptBusy, false); assert.equal(state.systemModal, "");
  assert.equal(rendered[0], "performance-telemetry-prompt");
  setRenderCallback(null);
});

test("NL and EN invitation retain voluntary choice, disclosure, visible close and all three buttons", () => {
  reset();
  for (const locale of ["nl", "en"]) {
    setLocale(locale, { persist: false });
    const html = renderPerformanceTelemetryPrompt();
    assert.match(html, /data-oq-action="enable-performance-telemetry-prompt"/);
    assert.match(html, /data-oq-action="decline-performance-telemetry-prompt"/);
    assert.match(html, /data-oq-action="defer-performance-telemetry-prompt"/);
    assert.match(html, /<details/); assert.match(html, /aria-modal="true"/);
    assert.match(html, locale === "nl" ? /Meedoen is vrijwillig/ : /Taking part is optional/);
  }
  setLocale("nl", { persist: false });
});


test("active invitation suspends for updates or Quick Start without recording a refusal", () => {
  reset(); syncPerformanceTelemetryPrompt(); state.updateModalOpen = true;
  syncPerformanceTelemetryPrompt(); assert.equal(state.systemModal, "");
  state.updateModalOpen = false; syncPerformanceTelemetryPrompt(); assert.equal(state.systemModal, "performance-telemetry-prompt");
  state.quickStartModalMode = "generation"; syncPerformanceTelemetryPrompt(); assert.equal(state.systemModal, "");
  assert.equal(state.performanceTelemetryPromptDeferred, false);
});

test("a late bulk poll cannot overwrite consent or reopen the invitation after saving", async () => {
  reset();
  const { refreshEntities } = await import("../js/src/core/entity-sync.js");
  let release;
  const response = new Promise((resolve) => { release = resolve; });
  globalThis.fetch = async () => ({ ok: true, json: async () => { await response; return { entities: {
    performanceTelemetryEnabled: { value: false }, performanceTelemetryChoiceConfigured: { value: true }, performanceTelemetryPromptHandled: { value: false },
  } }; } });
  const poll = refreshEntities(["performanceTelemetryEnabled", "performanceTelemetryChoiceConfigured", "performanceTelemetryPromptHandled"]);
  state.performanceTelemetryPromptWriteRevision += 2;
  state.entities.performanceTelemetryEnabled.value = true;
  state.entities.performanceTelemetryPromptHandled.value = true;
  release(); await poll;
  assert.equal(state.entities.performanceTelemetryEnabled.value, true);
  assert.equal(state.entities.performanceTelemetryPromptHandled.value, true);
  syncPerformanceTelemetryPrompt(); assert.equal(state.systemModal, "");
});
