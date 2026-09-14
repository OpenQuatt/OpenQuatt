import assert from "node:assert/strict";
import test, { beforeEach } from "node:test";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, setTimeout, clearTimeout, localStorage: { getItem: () => null } };
globalThis.document = { activeElement: null };

const bundle = await build({
  bundle: true, write: false, format: "esm", platform: "node", define: { __OQ_PREVIEW__: "false" },
  stdin: {
    resolveDir: fileURLToPath(new URL(".", import.meta.url)),
    contents: `
      export { state } from "../js/src/core/state.js";
      export { SETTINGS_GROUPS } from "../js/src/core/config.js";
      export { handleInput, handleChange, handleKeyDown } from "../js/src/core/entity-actions.js";
      export { commitTime } from "../js/src/core/entity-write-actions.js";
      export { refreshEntities } from "../js/src/core/entity-sync.js";
      export { deferTimeInputRender, finishTimeInput, handleTimeInputFocus } from "../js/src/core/time-input.js";
      export { setRenderCallback } from "../js/src/core/render-scheduler.js";
      export { patchSettingsDom } from "../js/src/settings/core.js";
      export { renderSettingsTimeField } from "../js/src/settings/controls.js";
    `,
  },
  plugins: [{ name: "test-assets", setup(plugin) {
    plugin.onResolve({ filter: /^virtual:embedded-assets$/ }, () => ({ path: "assets", namespace: "test-assets" }));
    plugin.onLoad({ filter: /.*/, namespace: "test-assets" }, () => ({
      contents: 'export const HP_GENERATION_IMAGE_V1 = "", HP_GENERATION_IMAGE_V2 = "", LOGO_MARKUP = "";',
    }));
  } }],
});
const { state, SETTINGS_GROUPS, handleInput, handleChange, handleKeyDown, commitTime, refreshEntities,
  deferTimeInputRender, finishTimeInput, handleTimeInputFocus, setRenderCallback,
  patchSettingsDom, renderSettingsTimeField } = await import(`data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString("base64")}`);
const key = "coolingScheduleStartTime";
const tick = () => new Promise((resolve) => setTimeout(resolve, 0));
const response = (body) => ({ ok: true, json: async () => body });

function inputFor(field = key, value = "00:00") {
  return { type: "time", dataset: { oqField: field }, value, isConnected: true, disabled: false };
}

function focus(input) {
  document.activeElement = input;
  handleTimeInputFocus({ type: "focusin", target: input });
}

function type(input, value) {
  input.value = value;
  handleInput({ target: input });
  handleChange({ target: input });
}

function blur(input) {
  document.activeElement = null;
  handleTimeInputFocus({ type: "focusout", target: input });
}

beforeEach(() => {
  Object.assign(state, {
    entities: { [key]: { value: "00:00:00", state: "00:00:00" } },
    drafts: {}, inputDrafts: {}, savingTimeFields: new Set(), timeWriteRevision: 0,
    loadingEntities: false, busyAction: "", controlNotice: "", controlError: "",
    appView: "settings", settingsGroup: "cooling", systemModal: "",
    nativeOpen: false, deviceReconnectMode: "", updateModalOpen: false, quickStartModalOpen: false,
    optionalMissingEntities: {},
  });
  document.activeElement = null;
  setRenderCallback(() => {});
});

test("partial hours/minutes survive background settings patches without posting", async () => {
  const input = inputFor();
  const writes = [];
  globalThis.fetch = async (...args) => { writes.push(args); return response({}); };
  focus(input);
  type(input, ""); // Native time inputs return an empty value while a segment is incomplete.
  let assignments = 0;
  Object.defineProperty(input, "value", { get: () => "", set: () => { assignments += 1; } });
  const card = {
    dataset: { oqSettingsField: key }, querySelector: () => null,
    querySelectorAll: (selector) => selector === "input[data-oq-field]" ? [input] : [],
  };
  const nav = { querySelectorAll: () => SETTINGS_GROUPS.map(({ id }) => ({
    dataset: { groupId: id }, classList: { toggle() {} }, setAttribute() {},
  })) };
  const stack = {
    querySelector: () => null,
    querySelectorAll: (selector) => selector === "[data-oq-settings-field]" ? [card] : [],
  };
  state.root = { querySelector: (selector) => selector === ".oq-settings-group-nav" ? nav : stack };
  patchSettingsDom();
  assert.equal(assignments, 0);
  assert.equal(deferTimeInputRender(), true);
  assert.deepEqual(writes, []);
  assert.match(renderSettingsTimeField(key, "Start koelvenster", ""), /value=""/);
  blur(input);
  await tick();
  assert.deepEqual(writes, []);
  assert.match(state.controlError, /HH:MM/);
});

test("only the completed time is saved on blur; confirmation replaces the draft", async () => {
  const input = inputFor();
  const calls = [];
  globalThis.fetch = async (url, options) => {
    calls.push({ url, options });
    return response(options?.method === "POST" ? {} : { state: "09:35:00" });
  };
  focus(input);
  type(input, "09:00");
  type(input, "09:35");
  assert.equal(calls.length, 0);
  blur(input);
  await tick();
  assert.equal(calls.length, 2);
  assert.match(calls[0].url, /value=09%3A35%3A00$/);
  assert.equal(calls[1].options.cache, "no-store");
  assert.equal(state.entities[key].value, "09:35:00");
  assert.equal(Object.hasOwn(state.inputDrafts, key), false);
  assert.match(renderSettingsTimeField(key, "Start koelvenster", ""), /value="09:35"/);
  focus(input);
  blur(input);
  await tick();
  assert.equal(calls.length, 2, "unchanged focus/blur does not write again");
});

test("Enter finishes editing and an in-flight save cannot be submitted twice", async () => {
  const input = inputFor();
  let release;
  let posts = 0;
  globalThis.fetch = async (_url, options) => {
    if (options?.method === "POST") {
      posts += 1;
      await new Promise((resolve) => { release = resolve; });
    }
    return response({ state: "12:45:00" });
  };
  focus(input);
  type(input, "12:45");
  input.blur = () => blur(input);
  let prevented = false;
  handleKeyDown({ key: "Enter", target: input, preventDefault: () => { prevented = true; } });
  await tick();
  assert.equal(prevented, true);
  assert.equal(input.disabled, true);
  finishTimeInput(input);
  assert.equal(posts, 1);
  assert.match(renderSettingsTimeField(key, "Start koelvenster", ""), /value="12:45" disabled/);
  release();
  await tick();
  assert.equal(state.savingTimeFields.size, 0);
});

test("a delayed confirmation does not interrupt editing the neighbouring time", async () => {
  let release;
  globalThis.fetch = async (_url, options) => {
    if (options?.method === "POST") await new Promise((resolve) => { release = resolve; });
    return response({ state: "08:15:00" });
  };
  const first = inputFor();
  focus(first);
  type(first, "08:15");
  blur(first);
  const next = inputFor("coolingScheduleEndTime");
  focus(next);
  type(next, "");
  const deferred = [];
  setRenderCallback(() => deferred.push(deferTimeInputRender()));
  await tick();
  release();
  await tick();
  assert.ok(deferred.length >= 2);
  assert.ok(deferred.every(Boolean));
  assert.equal(document.activeElement, next);
  assert.equal(state.inputDrafts.coolingScheduleEndTime, "");
  state.systemModal = "other";
  assert.equal(deferTimeInputRender(), false, "navigation/modal changes are not blocked");
  state.systemModal = "";
  focus(next);
  state.deviceReconnectMode = "reconnect";
  assert.equal(deferTimeInputRender(), false, "connection warnings are not blocked");
  blur(next);
  await tick();
});

test("failed or stale confirmation retains the attempted value and permits retry", async () => {
  for (const mode of ["post-failed", "read-failed", "stale"]) {
    globalThis.fetch = async (_url, options) => {
      if ((mode === "post-failed" && options?.method === "POST")
          || (mode === "read-failed" && !options?.method)) throw new Error("offline");
      return response({ state: "00:00:00" });
    };
    assert.equal(await commitTime(key, "07:20"), false);
    assert.equal(state.inputDrafts[key], "07:20");
    assert.equal(state.savingTimeFields.size, 0);
    assert.equal(state.controlNotice, "");
    assert.match(state.controlError, /kon niet worden bijgewerkt/);
  }
  globalThis.fetch = async () => response({ state: "07:20:00" });
  assert.equal(await commitTime(key, "07:20"), true);
  assert.equal(state.controlError, "");
  assert.equal(state.entities[key].value, "07:20:00");
});

test("polls started before or during a save cannot overwrite the confirmed time afterwards", async () => {
  for (const startDuringSave of [false, true]) {
    let releasePoll;
    let pollStarted;
    let releaseSave;
    const started = new Promise((resolve) => { pollStarted = resolve; });
    globalThis.fetch = async (url, options) => {
      if (String(url).includes("/entities")) {
        pollStarted();
        await new Promise((resolve) => { releasePoll = resolve; });
        return response({ entities: { [key]: { state: "00:00:00", value: "00:00:00" } } });
      }
      if (startDuringSave && options?.method === "POST") {
        await new Promise((resolve) => { releaseSave = resolve; });
      }
      return response({ state: "10:25:00" });
    };
    const earlySave = startDuringSave ? commitTime(key, "10:25") : null;
    const poll = refreshEntities([key]);
    await started;
    if (earlySave) releaseSave();
    assert.equal(await (earlySave || commitTime(key, "10:25")), true);
    releasePoll();
    await poll;
    assert.equal(state.entities[key].value, "10:25:00");
  }
});

test("a stalled acknowledgement times out and releases the field for retry", async () => {
  const originalSetTimeout = window.setTimeout;
  window.setTimeout = (callback, delay) => setTimeout(callback, delay === 8000 ? 1 : delay);
  globalThis.fetch = async (_url, options) => new Promise((_resolve, reject) => {
    options.signal.addEventListener("abort", () => reject(new Error("aborted")), { once: true });
  });
  try {
    assert.equal(await commitTime(key, "06:30"), false);
    assert.equal(state.savingTimeFields.size, 0);
    assert.equal(state.inputDrafts[key], "06:30");
    assert.match(state.controlError, /timed out/);
  } finally {
    window.setTimeout = originalSetTimeout;
  }
});
