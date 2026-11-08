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
      export { commitNumber, commitSwitch, undoSettingsChange } from "../js/src/core/entity-write-actions.js";
      export { setRenderCallback } from "../js/src/core/render-scheduler.js";
      export { setLocale } from "../js/src/i18n/index.js";
    `,
  },
  plugins: [{ name: "test-assets", setup(plugin) {
    plugin.onResolve({ filter: /^virtual:embedded-assets$/ }, () => ({ path: "assets", namespace: "test-assets" }));
    plugin.onLoad({ filter: /.*/, namespace: "test-assets" }, () => ({
      contents: 'export const HP_GENERATION_IMAGE_V1 = "", HP_GENERATION_IMAGE_V2 = "", LOGO_MARKUP = "";',
    }));
  } }],
});
const { state, commitNumber, commitSwitch, undoSettingsChange, setRenderCallback, setLocale } =
  await import(`data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString("base64")}`);

const response = (body) => ({ ok: true, status: 200, json: async () => body });
let writes = [];

beforeEach(() => {
  setLocale("nl");
  Object.assign(state, {
    entities: {
      coolingDemandMax: { value: 4, state: 4 },
      statusLedsEnabled: { value: true, state: true },
      electricalCurrentLimit: { value: 16, state: 16 },
    },
    drafts: {}, inputDrafts: {}, loadingEntities: false, busyAction: "", controlNotice: "", controlError: "",
    settingsUndo: null, appView: "settings", settingsGroup: "cooling", root: null,
  });
  writes = [];
  // Only entity writes count; bulk refreshes and change-log traffic are ignored here.
  globalThis.fetch = async (url, options = {}) => {
    const target = String(url);
    if (/\/auth\/status|\/openquatt\/change-log/.test(target)) return response({});
    if (options.method === "POST" && /\/set\?|\/turn_(on|off)$/.test(target)) writes.push(target);
    return response({});
  };
  setRenderCallback(() => {});
});

test("a number change offers its previous value, and undo writes it back", async () => {
  assert.equal(await commitNumber("coolingDemandMax", 7), true);
  const undo = state.settingsUndo;
  assert.equal(undo.key, "coolingDemandMax");
  assert.equal(undo.value, 4);
  assert.notEqual(undo.from, undo.to);
  assert.equal(undo.notice, state.controlNotice);

  writes = [];
  assert.equal(await undoSettingsChange(), true);
  assert.equal(writes.length, 1);
  assert.match(writes[0], /value=4$/);
  assert.match(state.controlNotice, /teruggezet naar/);
  assert.equal(state.settingsUndo, null, "an undo does not offer another undo");
});

test("a switch change can be undone", async () => {
  await commitSwitch("statusLedsEnabled", false);
  assert.equal(state.settingsUndo?.value, true);
  writes = [];
  assert.equal(await undoSettingsChange(), true);
  assert.equal(writes.length, 1);
  assert.match(writes[0], /turn_on$/);
});

test("undo is not offered for writes with their own confirmation", async () => {
  assert.equal(await commitNumber("electricalCurrentLimit", 14), true);
  assert.equal(state.settingsUndo, null);
});

test("undo only applies while its own notice is on screen", async () => {
  assert.equal(await commitNumber("coolingDemandMax", 7), true);
  state.controlNotice = "Iets anders bijgewerkt.";
  writes = [];
  assert.equal(await undoSettingsChange(), false);
  assert.deepEqual(writes, []);
});

test("a toast can still undo its own change after a newer notice", async () => {
  assert.equal(await commitNumber("coolingDemandMax", 7), true);
  const undo = state.settingsUndo;
  state.controlNotice = "Iets anders bijgewerkt.";
  writes = [];
  assert.equal(await undoSettingsChange(undo), true);
  assert.equal(writes.length, 1);
  assert.match(writes[0], /value=4$/);
});
