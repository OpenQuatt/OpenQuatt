import assert from "node:assert/strict";
import test, { beforeEach } from "node:test";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/", search: "", hash: "" },
  history: { replaceState() {}, pushState() {} },
  setTimeout, clearTimeout, localStorage: { getItem: () => null, setItem() {} },
};
globalThis.document = { activeElement: null };

const bundle = await build({
  bundle: true, write: false, format: "esm", platform: "node", define: { __OQ_PREVIEW__: "false" },
  stdin: {
    resolveDir: fileURLToPath(new URL(".", import.meta.url)),
    contents: `
      export { state } from "../js/src/core/state.js";
      export { APP_NAV_ITEMS } from "../js/src/core/config.js";
      export { renderAppNav } from "../js/src/core/app-shared.js";
      export { setAppView } from "../js/src/core/navigation.js";
      export { renderChartsView } from "../js/src/views/shell.js";
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
const { state, APP_NAV_ITEMS, renderAppNav, setAppView, renderChartsView, setLocale } =
  await import(`data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString("base64")}`);

const chartsTab = (markup) => markup.split("</button>").find((button) => button.includes("<span>Grafieken</span>"))
  ?.match(/data-view-id="([a-z]+)"/)?.[1];

beforeEach(() => {
  setLocale("nl");
  Object.assign(state, { appView: "overview", appViewGroupLast: {}, entities: {}, drafts: {} });
});

test("Results and Diagnose share one Charts tab in the top navigation", () => {
  assert.deepEqual(APP_NAV_ITEMS.map((item) => item.id), ["overview", "energy", "charts", "learning", "control", "settings"]);
  assert.deepEqual(APP_NAV_ITEMS.find((item) => item.id === "charts").views, ["results", "diagnosis"]);
  const markup = renderAppNav();
  assert.doesNotMatch(markup, /<span>Resultaten<\/span>|<span>Diagnose<\/span>/);
  assert.equal(chartsTab(markup), "results");
});

test("the Charts tab reopens the view used last", () => {
  setAppView("diagnosis");
  assert.equal(state.appViewGroupLast.charts, "diagnosis");
  assert.match(renderAppNav(), /class="oq-helper-app-tab is-active"[^>]*data-view-id="diagnosis"/);
  setAppView("overview");
  assert.equal(chartsTab(renderAppNav()), "diagnosis");

  // Without trend history Diagnose disappears, so the tab falls back to Results.
  state.entities.trendHistoryEnabled = { value: false, state: false };
  assert.equal(chartsTab(renderAppNav()), "results");
});

test("the side navigation lists Diagnose only while it is available or open", () => {
  state.appView = "results";
  let markup = renderChartsView();
  assert.match(markup, /data-view-id="results" aria-pressed="true"/);
  assert.match(markup, /data-view-id="diagnosis" aria-pressed="false"/);

  state.entities.trendHistoryEnabled = { value: false, state: false };
  markup = renderChartsView();
  assert.doesNotMatch(markup, /data-view-id="diagnosis"/);

  state.appView = "diagnosis";
  assert.match(renderChartsView(), /data-view-id="diagnosis" aria-pressed="true"/);
});
