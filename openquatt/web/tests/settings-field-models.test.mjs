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
      export { SETTINGS_GROUPS } from "../js/src/core/config.js";
      export { getSelectEntityOptions, getSettingsChoiceModel, getSettingsSelectModel, getSettingsSwitchModel } from "../js/src/settings/field-models.js";
      export { renderSettingsChoiceOption, renderSettingsCompactSwitchControl, renderSettingsSelectField, renderSettingsSwitchField, renderSettingsOptionCardsField, patchSettingsSelectControl } from "../js/src/settings/controls.js";
      export { renderSettingsStorageSelectRow } from "../js/src/settings/storage.js";
      export { renderHeatingStrategyExplainCards } from "../js/src/settings/heating.js";
      export { patchSettingsDom } from "../js/src/settings/core.js";
      export { commitSelect } from "../js/src/core/entity-write-actions.js";
      export { setRenderCallback } from "../js/src/core/render-scheduler.js";
    `,
    resolveDir: fileURLToPath(new URL(".", import.meta.url)),
  },
  plugins: [{
    name: "field-test-assets",
    setup(pluginBuild) {
      pluginBuild.onResolve({ filter: /^virtual:embedded-assets$/ }, () => ({ path: "assets", namespace: "field-test-assets" }));
      pluginBuild.onLoad({ filter: /.*/, namespace: "field-test-assets" }, () => ({
        contents: 'export const HP_GENERATION_IMAGE_V1 = ""; export const HP_GENERATION_IMAGE_V2 = ""; export const LOGO_MARKUP = "";',
      }));
    },
  }],
  write: false,
});
const { state, SETTINGS_GROUPS, getSelectEntityOptions, getSettingsChoiceModel, getSettingsSelectModel, getSettingsSwitchModel, renderSettingsChoiceOption, renderSettingsCompactSwitchControl, renderSettingsSelectField, renderSettingsSwitchField, renderSettingsOptionCardsField, patchSettingsSelectControl, renderSettingsStorageSelectRow, renderHeatingStrategyExplainCards, patchSettingsDom, commitSelect, setRenderCallback } = await import(`data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString("base64")}`);
const initial = structuredClone(state);

test.beforeEach(() => {
  setRenderCallback(null);
  globalThis.document = { activeElement: null };
  Object.assign(state, structuredClone(initial));
  state.loadingEntities = false;
  state.appView = "settings";
  state.settingsGroup = "installation";
});

function selectNode(key, values) {
  const select = node({ oqField: key, oqSelectModel: "true" });
  select.options = values.map(value => ({ value, textContent: value }));
  select.optionWrites = 0;
  Object.defineProperty(select, "innerHTML", {
    set(html) {
      select.optionWrites++;
      select.options = [...html.matchAll(/<option value="([^"]*)"[^>]*>([^<]*)<\/option>/g)].map(([, value, textContent]) => ({ value, textContent }));
    },
  });
  return select;
}

function node(dataset = {}) {
  const classes = new Set();
  const attributes = {};
  return {
    dataset, disabled: false, value: "", textContent: "",
    classList: { toggle: (name, active) => active ? classes.add(name) : classes.delete(name), contains: name => classes.has(name) },
    setAttribute: (name, value) => { attributes[name] = value; },
    getAttribute: name => attributes[name],
    closest: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
  };
}

function mount({ selects = [], choices = [], switches = [], pills = [] } = {}) {
  const card = node({ oqSettingsField: "strategy" });
  card.querySelectorAll = selector => selector === "select[data-oq-field]" ? selects : [];
  const groups = SETTINGS_GROUPS.map(({ id }) => node({ groupId: id }));
  const nav = { querySelectorAll: () => groups };
  const stack = node();
  const collections = {
    "[data-oq-settings-field]": [card],
    "[data-select-key]": choices,
    '[data-oq-action="toggle-overview-control"][data-control-key]': switches,
    "[data-oq-switch-pill]": pills,
  };
  stack.querySelectorAll = selector => collections[selector] || [];
  state.root = { querySelector: selector => selector === ".oq-settings-group-nav" ? nav : selector === ".oq-settings-group-stack" ? stack : null };
}

test("select fields retain firmware options and hide missing entities", () => {
  assert.equal(getSettingsSelectModel("missing").available, false);
  assert.equal(renderSettingsSelectField("missing", "Absent", ""), "");
  assert.equal(renderSettingsSwitchField("missing", "Absent", ""), "");
  state.entities.missing = null;
  assert.equal(renderSettingsSelectField("missing", "Absent", ""), "");
  assert.deepEqual(getSelectEntityOptions(), []);
  assert.deepEqual(getSelectEntityOptions({ option: "invalid", options: ["Fallback"] }), ["Fallback"]);
  state.entities.strategy = { value: "B", option: ["A", "B"], options: ["Ignored"] };
  const model = getSettingsSelectModel("strategy");
  assert.deepEqual(model, { available: true, value: "B", options: ["A", "B"], busy: false });
  assert.match(renderSettingsSelectField("strategy", "Strategy", ""), /<option value="B" selected>/);
  state.entities.strategy = { value: "B" };
  assert.deepEqual(getSettingsSelectModel("strategy").options, []);
  assert.doesNotMatch(renderSettingsSelectField("strategy", "Strategy", ""), /<option /);
});

test("select and choice models share loading and coupled strategy busy rules", () => {
  state.entities.strategy = { value: "A", options: ["A"] };
  for (const busyAction of ["save-strategy", "save-heatingEnableSource", "save-other", ""]) {
    for (const loading of [false, true]) {
      state.busyAction = busyAction;
      state.loadingEntities = loading;
      const expected = loading || ["save-strategy", "save-heatingEnableSource"].includes(busyAction);
      assert.equal(getSettingsSelectModel("strategy").busy, expected);
      assert.equal(getSettingsChoiceModel("strategy", "A").busy, expected);
      assert.equal(/<select[^>]*\bdisabled\b/.test(renderSettingsSelectField("strategy", "Strategy", "")), expected);
      assert.equal(/<button\b[^>]*\bdisabled\b/.test(renderSettingsChoiceOption({ key: "strategy", option: "A" })), expected);
    }
  }
});

test("explicit choice/switch values and stricter disabled gates survive rendering", () => {
  state.entities.strategy = { value: "server" };
  const choice = getSettingsChoiceModel("strategy", "draft", { currentValue: "draft", busy: true });
  assert.equal(choice.active, true);
  assert.equal(choice.busy, true);
  const choiceHtml = renderSettingsChoiceOption({ key: "strategy", option: "draft", currentValue: "draft", busy: true });
  assert.match(choiceHtml, /aria-pressed="true"/);
  assert.match(choiceHtml, /<button\b[^>]*\bdisabled\b/);
  const switchHtml = renderSettingsCompactSwitchControl("enabled", "Warmte & water", true, true, "Actief", "Gestopt");
  assert.match(switchHtml, /aria-checked="true"/);
  assert.match(switchHtml, /aria-label="Warmte &amp; water: Actief"/);
  assert.match(switchHtml, /data-control-state="off"/);
  assert.match(switchHtml, /<button\b[^>]*\bdisabled\b/);
  state.loadingEntities = true;
  assert.equal(getSettingsChoiceModel("strategy", "draft", { busy: false }).busy, false);
  assert.equal(getSettingsSwitchModel("enabled", { busy: false }).busy, false);
});

test("live field patches reuse models without replacing nodes or overwriting select gates/options", () => {
  state.entities.strategy = { value: "server", options: ["server", "draft"] };
  state.drafts.strategy = "draft";
  state.entities.enabled = { value: true };
  state.busyAction = "save-heatingEnableSource";
  const select = node({ oqField: "strategy" });
  select.disabled = true;
  select.options = ["original options"];
  const choice = node({ selectKey: "strategy", selectOption: "draft", oqSelectModel: "true" });
  const customChoice = node({ selectKey: "customSafety", selectOption: "draft" });
  customChoice.disabled = true;
  const control = node({ controlKey: "enabled", switchTitle: "Warmte", onLabel: "Actief", offLabel: "Gestopt" });
  const pill = node({ oqSwitchPill: "enabled", onLabel: "Actief", offLabel: "Gestopt" });
  mount({ selects: [select], choices: [choice, customChoice], switches: [control], pills: [pill] });
  assert.equal(patchSettingsDom(), true);
  assert.equal(select.value, "draft");
  assert.equal(select.disabled, true);
  assert.deepEqual(select.options, ["original options"]);
  assert.equal(choice.getAttribute("aria-pressed"), "true");
  assert.equal(choice.disabled, true);
  assert.equal(customChoice.disabled, true);
  assert.equal(control.getAttribute("aria-label"), "Warmte: Actief");
  assert.equal(control.dataset.controlState, "off");
  assert.equal(pill.textContent, "Actief");
  state.busyAction = "switch-enabled";
  state.entities.enabled.value = false;
  assert.equal(patchSettingsDom(), true);
  assert.equal(choice.disabled, false);
  assert.equal(control.disabled, true);
  assert.equal(control.getAttribute("aria-checked"), "false");
  assert.equal(control.getAttribute("aria-label"), "Warmte: Gestopt");
  assert.equal(pill.textContent, "Gestopt");
});

test("all shared field models retain draft precedence and switch labels", () => {
  state.entities.strategy = { value: "server" };
  state.entities.enabled = { value: true };
  state.drafts.strategy = "draft";
  state.drafts.enabled = false;
  assert.equal(getSettingsSelectModel("strategy").value, "draft");
  assert.equal(getSettingsChoiceModel("strategy", "draft").active, true);
  assert.deepEqual(getSettingsSwitchModel("enabled", { title: "Warmte", onLabel: "Actief", offLabel: "Gestopt" }), {
    enabled: false, busy: false, label: "Gestopt", nextState: "on", ariaLabel: "Warmte: Gestopt",
  });
});

test("focused integrations and service retain their existing full-render fallback", () => {
  const select = node({ oqField: "strategy" });
  select.value = "typing";
  mount({ selects: [select] });
  state.settingsGroup = "integrations";
  state.focusedField = "strategy";
  assert.equal(patchSettingsDom(), false);
  assert.equal(select.value, "typing");
  state.settingsGroup = "service";
  state.focusedField = "";
  assert.equal(patchSettingsDom(), false);
  assert.equal(select.value, "typing");
});

test("dropdowns, option cards and storage rows render one field snapshot", () => {
  state.entities.coolingWithoutDewPointMode = { value: "A", options: ["A", "B"] };
  state.drafts.coolingWithoutDewPointMode = "B";
  state.busyAction = "save-coolingWithoutDewPointMode";
  const model = getSettingsSelectModel("coolingWithoutDewPointMode");
  const cards = renderSettingsOptionCardsField("coolingWithoutDewPointMode", "Beveiliging", "", { A: "Eerste", B: "Tweede" });
  assert.match(cards, /data-select-option="B"[^>]*aria-pressed="true"[^>]*disabled/);
  assert.equal((cards.match(/data-oq-select-model="true"/g) || []).length, 2);
  assert.match(renderSettingsStorageSelectRow("coolingWithoutDewPointMode", "Bewaren", ""), /<select[^>]*disabled[^>]*>.*<option value="B" selected>/s);
  state.entities.coolingWithoutDewPointMode.value = "changed after snapshot";
  state.busyAction = "";
  assert.deepEqual(getSettingsChoiceModel("coolingWithoutDewPointMode", "B", { model }), { active: true, busy: true });
});

test("managed fields follow loading, saving, rollback and missing-entity states", () => {
  const key = "coolingWithoutDewPointMode";
  const select = selectNode(key, ["A", "B"]);
  const choice = node({ selectKey: key, selectOption: "B", oqSelectModel: "true" });
  const shell = node();
  choice.closest = () => shell;
  mount({ selects: [select], choices: [choice] });
  for (const [available, value, busyAction, loading, disabled] of [
    [true, "A", "", true, true],
    [true, "B", `save-${key}`, false, true],
    [true, "A", "", false, false],
    [false, "", "", false, true],
    [true, "B", "save-other", false, false],
  ]) {
    state.entities[key] = available ? { value, options: ["A", "B"] } : null;
    state.busyAction = busyAction;
    state.loadingEntities = loading;
    assert.equal(patchSettingsDom(), true);
    assert.equal(select.value, value);
    assert.equal(select.disabled, disabled);
    assert.equal(choice.disabled, disabled);
    assert.equal(choice.getAttribute("aria-pressed"), value === "B" ? "true" : "false");
    assert.equal(shell.classList.contains("is-active"), value === "B");
  }
});

test("live option changes preserve the select node and only rewrite changed options", () => {
  state.entities.flowControlMode = { value: "A", option: ["A", "B"] };
  const select = selectNode("flowControlMode", ["A", "B"]);
  const originalOption = select.options[0];
  patchSettingsSelectControl(select, getSettingsSelectModel("flowControlMode"));
  assert.equal(select.options[0], originalOption);
  assert.equal(select.optionWrites, 0);
  state.entities.flowControlMode = { value: "C", options: ["B", "C"] };
  patchSettingsSelectControl(select, getSettingsSelectModel("flowControlMode"));
  assert.equal(select.optionWrites, 1);
  assert.deepEqual(select.options.map(option => option.value), ["B", "C"]);
  assert.equal(select.value, "C");
  patchSettingsSelectControl(select, getSettingsSelectModel("flowControlMode"));
  assert.equal(select.optionWrites, 1);
});

test("focused native selects defer option and value updates until focus leaves", () => {
  const select = selectNode("flowControlMode", ["A", "B"]);
  select.value = "A";
  document.activeElement = select;
  state.entities.flowControlMode = { value: "C", options: ["B", "C"] };
  patchSettingsSelectControl(select, getSettingsSelectModel("flowControlMode"));
  assert.equal(select.value, "A");
  assert.equal(select.optionWrites, 0);
  document.activeElement = null;
  patchSettingsSelectControl(select, getSettingsSelectModel("flowControlMode"));
  assert.equal(select.value, "C");
  assert.equal(select.optionWrites, 1);
});

test("custom choice gates are not enrolled in automatic busy updates", () => {
  state.entities.strategy = { value: "A", option: ["A"] };
  const html = renderSettingsChoiceOption({ key: "strategy", option: "A", busy: true });
  assert.doesNotMatch(html, /data-oq-select-model/);
  const custom = node({ selectKey: "strategy", selectOption: "A" });
  custom.disabled = true;
  mount({ choices: [custom] });
  patchSettingsDom();
  assert.equal(custom.disabled, true);
  assert.equal(custom.getAttribute("aria-pressed"), "true");
});

test("unknown strategy is not presented as an active Power House selection", () => {
  state.entities.strategy = { value: "Unknown" };
  const html = renderHeatingStrategyExplainCards();
  assert.doesNotMatch(html, /aria-pressed="true"/);
  assert.equal((html.match(/data-oq-select-model="true"/g) || []).length, 2);
});

test("firmware option text is escaped in both dropdown and choice rendering", () => {
  const option = '\"><img src=x onerror=alert(1)>';
  state.entities.strategy = { value: option, options: [option] };
  for (const html of [renderSettingsSelectField("strategy", "Strategy", ""), renderSettingsOptionCardsField("strategy", "Strategy", "", {})]) {
    assert.doesNotMatch(html, /<img/);
    assert.match(html, /&quot;&gt;&lt;img/);
  }
});

test("a failed select request restores both controls and releases their busy state", async (t) => {
  const key = "flowControlMode";
  state.entities[key] = { value: "Automatic", option: ["Automatic", "Manual"] };
  const select = selectNode(key, ["Automatic", "Manual"]);
  const choice = node({ selectKey: key, selectOption: "Manual", oqSelectModel: "true" });
  mount({ selects: [select], choices: [choice] });
  setRenderCallback(patchSettingsDom);
  let rejectRequest;
  t.mock.method(globalThis, "fetch", () => new Promise((resolve, reject) => { rejectRequest = reject; }));
  const result = commitSelect(key, "Manual");
  assert.equal(select.value, "Manual");
  assert.equal(select.disabled, true);
  assert.equal(choice.getAttribute("aria-pressed"), "true");
  assert.equal(choice.disabled, true);
  rejectRequest(new Error("connection lost"));
  assert.equal(await result, false);
  assert.equal(select.value, "Automatic");
  assert.equal(select.disabled, false);
  assert.equal(choice.getAttribute("aria-pressed"), "false");
  assert.equal(choice.disabled, false);
  assert.match(state.controlError, /connection lost/);
});
