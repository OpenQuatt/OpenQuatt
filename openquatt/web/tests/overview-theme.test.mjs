import assert from "node:assert/strict";
import test from "node:test";

const storage = new Map();
const colorSchemeMedia = {
  matches: true,
  listeners: [],
  addEventListener(type, listener) {
    if (type === "change") {
      this.listeners.push(listener);
    }
  },
  emit(matches) {
    this.matches = matches;
    this.listeners.forEach((listener) => listener({ matches }));
  },
};

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  clearTimeout,
  location: { pathname: "/" },
  localStorage: {
    getItem: (key) => (storage.has(key) ? storage.get(key) : null),
    setItem: (key, value) => storage.set(key, String(value)),
  },
  matchMedia: (query) => (query === "(prefers-color-scheme: dark)" ? colorSchemeMedia : { matches: false }),
  setTimeout,
};

const { getStoredOverviewThemePreference, resolveOverviewTheme, state } = await import("../js/src/core/state.js");
const { bindColorSchemePreference, setOverviewTheme } = await import("../js/src/core/runtime.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
const { getHeaderRenderSignature, renderHeaderStatus } = await import("../js/src/features/header-status.js");

test("theme preference defaults to automatic and keeps stored light/dark choices", () => {
  storage.clear();
  assert.equal(getStoredOverviewThemePreference(), "auto");
  storage.set("oq-overview-theme", "dark");
  assert.equal(getStoredOverviewThemePreference(), "dark");
  storage.set("oq-overview-theme", "light");
  assert.equal(getStoredOverviewThemePreference(), "light");
  storage.set("oq-overview-theme", "auto");
  assert.equal(getStoredOverviewThemePreference(), "auto");
  storage.set("oq-overview-theme", "unexpected");
  assert.equal(getStoredOverviewThemePreference(), "auto");
});

test("automatic theme follows the device color scheme and explicit choices ignore it", (t) => {
  let renders = 0;
  setRenderCallback(() => {
    renders += 1;
  });
  t.after(() => setRenderCallback(null));

  colorSchemeMedia.matches = true;
  assert.equal(resolveOverviewTheme("auto"), "dark");
  assert.equal(resolveOverviewTheme("light"), "light");

  setOverviewTheme("auto");
  bindColorSchemePreference();
  assert.equal(storage.get("oq-overview-theme"), "auto");
  assert.equal(state.overviewTheme, "dark");

  colorSchemeMedia.emit(false);
  assert.equal(state.overviewTheme, "light");
  assert.equal(renders, 0);

  colorSchemeMedia.emit(false);
  assert.equal(renders, 0);

  setOverviewTheme("dark");
  assert.equal(storage.get("oq-overview-theme"), "dark");
  colorSchemeMedia.emit(true);
  colorSchemeMedia.emit(false);
  assert.equal(state.overviewTheme, "dark");
  assert.equal(state.overviewThemePreference, "dark");
  assert.equal(renders, 0);
});

test("interface panel offers automatic, light and dark theme choices", () => {
  state.interfacePanelOpen = true;
  state.entities = {};
  setOverviewTheme("auto");

  const html = renderHeaderStatus();
  const themeChips = [...html.matchAll(/<button class="([^"]*)"[^>]*data-overview-theme="([^"]+)"/g)]
    .map(([, className, theme]) => ({ theme, active: className.split(" ").includes("is-active") }));

  assert.deepEqual(themeChips, [
    { theme: "auto", active: true },
    { theme: "light", active: false },
    { theme: "dark", active: false },
  ]);
  assert.doesNotMatch(html, /toggle-overview-theme/);
});


function createThemeElement(classes = []) {
  const tokens = new Set(classes);
  return {
    classList: {
      add: (...values) => values.forEach((value) => tokens.add(value)),
      remove: (...values) => values.forEach((value) => tokens.delete(value)),
      contains: (value) => tokens.has(value),
      toggle: (value, enabled) => enabled ? tokens.add(value) : tokens.delete(value),
    },
  };
}

test("binding automatic theme reconciles a system change since initial state loading", () => {
  colorSchemeMedia.matches = false;
  setOverviewTheme("auto");
  state.overviewThemeMedia = null;
  colorSchemeMedia.matches = true;
  bindColorSchemePreference();
  assert.equal(state.overviewTheme, "dark");

  setOverviewTheme("light");
  state.overviewThemeMedia = null;
  bindColorSchemePreference();
  assert.equal(state.overviewTheme, "light");
});

test("automatic theme patches all existing surfaces without replacing active controls", (t) => {
  const previousDocument = globalThis.document;
  const previousRoot = state.root;
  const shell = createThemeElement();
  const boards = [createThemeElement(["oq-overview-board--light"]), createThemeElement(["oq-overview-board--light"])];
  const backdrop = createThemeElement();
  const surfaces = new Map([
    [".oq-helper-shell", [shell]],
    [".oq-overview-board", boards],
    [".oq-helper-modal-backdrop", [backdrop]],
  ]);
  const controls = ["SELECT", "INPUT", "TEXTAREA", "BUTTON"].map((tagName) => ({ tagName, value: "draft" }));
  const root = {
    scrollTop: 123,
    querySelectorAll: (selector) => surfaces.get(selector) || [],
    set innerHTML(_value) { assert.fail("Theme change must not replace the DOM"); },
  };
  globalThis.document = { documentElement: createThemeElement(), body: createThemeElement() };
  state.root = root;
  state.nativeOpen = false;
  let renders = 0;
  setRenderCallback(() => { renders += 1; });
  t.after(() => {
    globalThis.document = previousDocument;
    state.root = previousRoot;
    setRenderCallback(null);
  });

  colorSchemeMedia.matches = false;
  setOverviewTheme("auto");
  const headerSignature = getHeaderRenderSignature();
  for (const control of controls) {
    document.activeElement = control;
    colorSchemeMedia.emit(true);
    assert.equal(state.overviewTheme, "dark");
    assert.equal(shell.classList.contains("oq-helper-shell--dark"), true);
    assert.equal(backdrop.classList.contains("oq-helper-modal-backdrop--dark"), true);
    assert.equal(document.body.classList.contains("oq-page-dark"), true);
    for (const board of boards) {
      assert.equal(board.classList.contains("oq-overview-board--dark"), true);
      assert.equal(board.classList.contains("oq-overview-board--light"), false);
    }
    assert.equal(document.activeElement, control);
    assert.equal(control.value, "draft");
    assert.equal(root.scrollTop, 123);
    assert.equal(getHeaderRenderSignature(), headerSignature);

    colorSchemeMedia.emit(false);
    assert.equal(shell.classList.contains("oq-helper-shell--dark"), false);
    assert.equal(backdrop.classList.contains("oq-helper-modal-backdrop--dark"), false);
    assert.equal(document.documentElement.classList.contains("oq-page-light"), true);
    for (const board of boards) {
      assert.equal(board.classList.contains("oq-overview-board--dark"), false);
      assert.equal(board.classList.contains("oq-overview-board--light"), true);
    }
  }
  assert.equal(renders, 0);
});
