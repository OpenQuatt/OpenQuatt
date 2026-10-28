// Delta T flow control settings (oq_flow_delta_t.yaml).
import assert from "node:assert/strict";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const bundle = await build({
  bundle: true,
  define: { __OQ_PREVIEW__: "false" },
  format: "esm",
  platform: "node",
  stdin: {
    contents: `
      export { state } from "../js/src/core/state.js";
      export { FLOW_SETTING_KEYS, SETTINGS_BACKUP_SECTIONS } from "../js/src/core/config.js";
      export { isDeltaTFlowMode, isManualFlowMode } from "../js/src/core/domain-helpers.js";
      export { renderFlowSettingsFields } from "../js/src/settings/heating.js";
    `,
    resolveDir: fileURLToPath(new URL(".", import.meta.url)),
  },
  plugins: [{
    name: "flow-delta-t-test-assets",
    setup(pluginBuild) {
      pluginBuild.onResolve({ filter: /^virtual:embedded-assets$/ }, () => ({ path: "assets", namespace: "flow-delta-t-test-assets" }));
      pluginBuild.onLoad({ filter: /.*/, namespace: "flow-delta-t-test-assets" }, () => ({
        contents: 'export const HP_GENERATION_IMAGE_V1 = ""; export const HP_GENERATION_IMAGE_V2 = ""; export const LOGO_MARKUP = "";',
      }));
    },
  }],
  write: false,
});
const { state, FLOW_SETTING_KEYS, SETTINGS_BACKUP_SECTIONS, isDeltaTFlowMode, isManualFlowMode, renderFlowSettingsFields } = await import(`data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString("base64")}`);
const initial = structuredClone(state);

const DELTA_T_NUMBERS = [
  "flowDeltaTTarget",
  "flowDeltaTMinFlow",
  "flowDeltaTMaxFlow",
  "flowDeltaTStepInterval",
  "flowDeltaTMaxStep",
  "flowDeltaTDeadband",
];

function seed(mode) {
  Object.assign(state, structuredClone(initial));
  state.loadingEntities = false;
  state.appView = "settings";
  state.settingsGroup = "installation";
  state.entities.flowControlMode = { value: mode, options: ["Flow Setpoint", "Manual PWM", "Delta T"] };
  state.entities.flowSetpoint = { value: 800, min_value: 0, max_value: 1500, step: 10, uom: "L/h" };
  state.entities.coolingFlowSetpoint = { value: 800, min_value: 0, max_value: 1500, step: 10, uom: "L/h" };
  state.entities.manualIpwm = { value: 400, min_value: 50, max_value: 850, step: 1 };
  state.entities.flowDeltaTTarget = { value: 3, min_value: 2, max_value: 8, step: 0.5, uom: "K" };
  state.entities.flowDeltaTMinFlow = { value: 500, min_value: 400, max_value: 1500, step: 10, uom: "L/h" };
  state.entities.flowDeltaTMaxFlow = { value: 1200, min_value: 400, max_value: 1500, step: 10, uom: "L/h" };
  state.entities.flowDeltaTStepInterval = { value: 5, min_value: 1, max_value: 30, step: 1, uom: "min" };
  state.entities.flowDeltaTMaxStep = { value: 50, min_value: 10, max_value: 200, step: 10, uom: "L/h" };
  state.entities.flowDeltaTDeadband = { value: 0.3, min_value: 0.1, max_value: 1, step: 0.1, uom: "K" };
  state.entities.flowDeltaTStatus = { value: "Adjusting - rise 2.4 K, 1 unit, 640 L/h" };
}

test("Delta T is recognised as its own flow mode, not as manual", () => {
  assert.equal(isDeltaTFlowMode("Delta T"), true);
  assert.equal(isDeltaTFlowMode("Flow Setpoint"), false);
  assert.equal(isDeltaTFlowMode("Manual PWM"), false);
  assert.equal(isManualFlowMode("Delta T"), false);
});

test("Delta T mode shows its target, band and status next to the start flow", () => {
  seed("Delta T");
  const html = renderFlowSettingsFields();
  for (const key of [...DELTA_T_NUMBERS, "flowSetpoint", "coolingFlowSetpoint"]) {
    assert.ok(html.includes(key), `expected ${key} in the Delta T settings`);
  }
  assert.ok(html.includes("Adjusting - rise 2.4 K"), "status text should be shown");
  assert.ok(!html.includes("manualIpwm"), "manual pump speed belongs to Manual PWM only");
});

test("fixed flow and manual modes do not show Delta T settings", () => {
  for (const mode of ["Flow Setpoint", "Manual PWM"]) {
    seed(mode);
    const html = renderFlowSettingsFields();
    for (const key of [...DELTA_T_NUMBERS, "flowDeltaTStatus"]) {
      assert.ok(!html.includes(key), `${key} must not appear in ${mode}`);
    }
  }
});

test("Delta T settings are synced and backed up, the status only synced", () => {
  for (const key of [...DELTA_T_NUMBERS, "flowDeltaTStatus"]) {
    assert.ok(FLOW_SETTING_KEYS.includes(key), `${key} should be refreshed with the flow settings`);
  }
  const backedUp = SETTINGS_BACKUP_SECTIONS.flatMap((section) => section.keys);
  for (const key of DELTA_T_NUMBERS) {
    assert.ok(backedUp.includes(key), `${key} should survive a settings backup and restore`);
  }
  assert.ok(!backedUp.includes("flowDeltaTStatus"), "a read-only status is not a setting");
});
