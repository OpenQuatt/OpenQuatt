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
      export { isHeatPumpAlertActive } from "../js/src/views/heatpump.js";
      export { getHeaderAlertModel, renderHeaderAlert } from "../js/src/features/header-status.js";
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
const { state, isHeatPumpAlertActive, getHeaderAlertModel, renderHeaderAlert, setLocale } =
  await import(`data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString("base64")}`);

const faultSnapshot = {
  valid: true,
  heatPumps: [
    { subject: "hp1", faultActive: true, linkState: "healthy", runState: "stopped", incidents: [] },
    { subject: "hp2", faultActive: false, linkState: "healthy", runState: "running", availableForStart: true, incidents: [] },
  ],
};

beforeEach(() => {
  setLocale("nl");
  Object.assign(state, { entities: {}, drafts: {}, incidentMonitoringSnapshot: null, incidentMonitoringError: "" });
});

test("a heat pump is marked for failure codes or a fault in installation monitoring", () => {
  assert.equal(isHeatPumpAlertActive("HP1", { warningActive: true }), true);
  assert.equal(isHeatPumpAlertActive("HP1", { warningActive: false }), false);

  state.incidentMonitoringSnapshot = faultSnapshot;
  assert.equal(isHeatPumpAlertActive("HP1", { warningActive: false }), true);
  assert.equal(isHeatPumpAlertActive("HP2", { warningActive: false }), false);

  state.incidentMonitoringError = "HTTP 503";
  assert.equal(isHeatPumpAlertActive("HP1", { warningActive: false }), false, "a stale snapshot is not trusted");
});

test("the header slot stays empty without problems and links to monitoring with one", () => {
  assert.equal(getHeaderAlertModel(), null);
  assert.match(renderHeaderAlert(), /data-oq-header-alert[^>]*><\/div>$/);

  state.entities.lowflowFaultActive = { value: true, state: true };
  const model = getHeaderAlertModel();
  assert.ok(model);
  assert.equal(model.fault, false);
  const markup = renderHeaderAlert(model);
  assert.match(markup, /data-oq-action="open-installation-monitoring"/);
  assert.doesNotMatch(markup, /oq-helper-alert--fault/);

  const faultMarkup = renderHeaderAlert({ fault: true, title: "Storing actief", detail: "Warmtepomp 1: <druk>" });
  assert.match(faultMarkup, /oq-helper-alert--fault/);
  assert.match(faultMarkup, /Warmtepomp 1: &lt;druk&gt;/);
});
