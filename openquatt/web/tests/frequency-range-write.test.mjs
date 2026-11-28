import assert from "node:assert/strict";
import test from "node:test";
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
      export { handleInput, handleChange, handlePointerUp } from "../js/src/core/entity-actions.js";
      export { refreshEntities, startEntityWriteAction } from "../js/src/core/entity-sync.js";
    `,
  },
  plugins: [{ name: "test-assets", setup(plugin) {
    plugin.onResolve({ filter: /^virtual:embedded-assets$/ }, () => ({ path: "assets", namespace: "test-assets" }));
    plugin.onLoad({ filter: /.*/, namespace: "test-assets" }, () => ({
      contents: 'export const HP_GENERATION_IMAGE_V1 = "", HP_GENERATION_IMAGE_V2 = "", LOGO_MARKUP = "";',
    }));
  } }],
});
const { state, handleInput, handleChange, handlePointerUp, refreshEntities, startEntityWriteAction } = await import(`data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString("base64")}`);

function numberEntity(value) {
  return { value, state: String(value), min_value: 0, max_value: 120, step: 1, uom: "Hz" };
}

function resetSettingsState(entities) {
  state.entities = entities;
  state.drafts = {};
  state.inputDrafts = {};
  state.appView = "overview";
  state.loadingEntities = false;
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
}

function frequencyRangeSlider(hp) {
  const label = {};
  const control = {
    querySelector: (selector) => selector.includes('"min"') ? min : selector.includes('"max"') ? max : label,
    classList: { toggle() {}, remove() {} },
    style: { setProperty() {} },
  };
  const slider = (role, suffix) => ({
    type: "range",
    min: "0",
    max: "110",
    value: "0",
    dataset: { oqField: `${hp}Exclude${suffix}`, oqRangeRole: role },
    closest: (selector) => selector.includes("dual-range") ? control : null,
  });
  const min = slider("min", "MinHz");
  const max = slider("max", "MaxHz");
  return { min, max, label };
}


function mockDevice(ctx, hp, initial, onWrite = ({ key, value, stored }) => { stored[key] = value; }) {
  const keys = [hp + "ExcludeMinHz", hp + "ExcludeMaxHz", hp + "ExcludeScope"];
  const stored = Object.fromEntries(keys.map((key, index) => [key, initial[index] ?? "Always"]));
  const extraEntities = {};
  const entities = () => ({
    ...Object.fromEntries(keys.map((key, index) => [
      key, index === 2 ? { value: stored[key] } : numberEntity(stored[key]),
    ])),
    ...extraEntities,
  });
  resetSettingsState(entities());
  let startedRead;
  let blockedRead;
  let shouldBlockRead;
  const writes = [];
  ctx.mock.method(globalThis, "fetch", async (url) => {
    if (url === "/auth/status") return { ok: true, json: async () => ({}) };
    if (url === "/openquatt/entities") {
      const snapshot = { entities: entities(), missing: [] };
      if (blockedRead && shouldBlockRead()) {
        startedRead();
        await blockedRead;
      }
      return { ok: true, json: async () => snapshot };
    }
    const decoded = decodeURIComponent(url);
    const match = decoded.match(/^\/number\/HP[12] - Excluded frequency (minimum|maximum)\/set\?value=(\d+)$/);
    assert.ok(match, decoded);
    const key = keys[match[1] === "minimum" ? 0 : 1];
    const value = Number(match[2]);
    writes.push([key, value]);
    const status = await onWrite({ key, value, stored }) ?? 200;
    return { ok: status === 200, status };
  });
  return {
    keys,
    stored,
    extraEntities,
    writes,
    holdRead(started, until, filter = () => !state.savingFrequencyRange) {
      startedRead = started;
      blockedRead = until;
      shouldBlockRead = filter;
    },
  };
}

function move(slider, hz) {
  slider.value = String(hz);
  handleInput({ target: slider });
  return handleChange({ target: slider });
}

for (const { hp, first, hz, text, secondHz } of [
  { hp: "hp1", first: "max", hz: 60, text: "Tot 60 Hz", secondHz: 42 },
  { hp: "hp2", first: "min", hz: 42, text: "Vanaf 42 Hz", secondHz: 60 },
]) {
  test(hp + " behoudt de eerste grens en schrijft daarna alleen de gekozen tweede grens", async (ctx) => {
    const device = mockDevice(ctx, hp, [0, 0, "Quiet hours"]);
    const sliders = frequencyRangeSlider(hp);
    const firstSlider = sliders[first];
    const secondSlider = sliders[first === "min" ? "max" : "min"];
    assert.equal(await move(firstSlider, hz), true);
    assert.equal(secondSlider.value, "0");
    assert.equal(sliders.label.textContent, text);
    assert.deepEqual(device.writes, [[firstSlider.dataset.oqField, hz]]);
    assert.equal(state.entities[firstSlider.dataset.oqField].value, hz);
    assert.equal(await move(secondSlider, secondHz), true);
    assert.deepEqual(device.writes, [[firstSlider.dataset.oqField, hz], [secondSlider.dataset.oqField, secondHz]]);
    assert.deepEqual(device.keys.map((key) => state.entities[key].value), [42, 60, "Quiet hours"]);
    assert.deepEqual(state.drafts, {});
  });
}

test("de tweede grens wacht op readback en een andere instelling annuleert de bevestiging niet", async (ctx) => {
  let writeStarted;
  let releaseWrite;
  const started = new Promise((resolve) => { writeStarted = resolve; });
  const blocked = new Promise((resolve) => { releaseWrite = resolve; });
  const device = mockDevice(ctx, "hp1", [0, 0], async ({ key, value, stored }) => {
    writeStarted();
    await blocked;
    stored[key] = value;
  });
  let readStarted;
  let releaseRead;
  const oldReadStarted = new Promise((resolve) => { readStarted = resolve; });
  device.holdRead(readStarted, new Promise((resolve) => { releaseRead = resolve; }));
  const pendingRefresh = refreshEntities(device.keys);
  await oldReadStarted;
  const { min, max } = frequencyRangeSlider("hp1");
  const saving = move(max, 60);
  try {
    await started;
    assert.equal(move(min, 40), undefined);
    assert.deepEqual(device.writes, [[device.keys[1], 60]]);
    assert.equal(state.entities[device.keys[1]].value, 0);
    startEntityWriteAction("save-silentMaxHz");
  } finally {
    releaseWrite();
    assert.equal(await saving, true);
    releaseRead();
    await pendingRefresh;
  }
  assert.equal(state.entities[device.keys[1]].value, 60);
  assert.equal(state.busyAction, "save-silentMaxHz");
  assert.equal(state.savingFrequencyRange, "");
  assert.deepEqual(state.drafts, {});
});

test("een gewijzigde bovengrens of uitsluitingsscope vraagt om een nieuwe keuze", async (ctx) => {
  const device = mockDevice(ctx, "hp1", [0, 60, "Off"]);
  const { min } = frequencyRangeSlider("hp1");
  device.stored[device.keys[1]] = 70;
  device.stored[device.keys[2]] = "Always";
  assert.equal(await move(min, 42), false);
  assert.match(state.controlError, /ondertussen gewijzigd/);
  assert.deepEqual(device.writes, []);
  assert.deepEqual(device.keys.map((key) => state.entities[key].value), [0, 70, "Always"]);
});

test("een andere instelling tijdens de preflight vereist een nieuwe keuze", async (ctx) => {
  const device = mockDevice(ctx, "hp1", [0, 45]);
  let readStarted;
  let releaseRead;
  const started = new Promise((resolve) => { readStarted = resolve; });
  device.holdRead(readStarted, new Promise((resolve) => { releaseRead = resolve; }), () => true);
  const { min, max } = frequencyRangeSlider("hp1");
  max.value = "45";
  const pending = move(min, 45);
  await started;
  startEntityWriteAction("save-dayMaxHz");
  releaseRead();
  assert.equal(await pending, false);
  assert.match(state.controlError, /ondertussen gewijzigd/);
  assert.deepEqual(device.writes, []);
});

test("een HTTP-bevestiging zonder opgeslagen grens leidt tot een fout", async (ctx) => {
  const device = mockDevice(ctx, "hp1", [0, 0], () => 200);
  const { max } = frequencyRangeSlider("hp1");
  assert.equal(await move(max, 45), false);
  assert.match(state.controlError, /opgeslagen keuze niet bevestigd/);
  assert.equal(state.controlNotice, "");
  assert.equal(state.entities[device.keys[1]].value, 0);
});

test("een verloren ontvangstbevestiging laat de uitgelezen grens intact", async (ctx) => {
  const device = mockDevice(ctx, "hp1", [0, 0], ({ key, value, stored }) => {
    stored[key] = value;
    return 500;
  });
  const { max } = frequencyRangeSlider("hp1");
  assert.equal(await move(max, 45), true);
  assert.equal(state.entities[device.keys[1]].value, 45);
  assert.equal(device.writes.length, 1);
});

test("een afgebroken sleepbeweging bewaart uitsluitend bestaande waarden", (ctx) => {
  const device = mockDevice(ctx, "hp1", [0, 0]);
  const { max } = frequencyRangeSlider("hp1");
  max.value = "45";
  handleInput({ target: max });
  handlePointerUp({ type: "pointercancel", target: max });
  assert.deepEqual(device.writes, []);
  assert.deepEqual(state.drafts, {});
  assert.deepEqual(state.inputDrafts, {});
});

test("de tweede grens behoudt de bevestiging voor de laatste compressorstand", async (ctx) => {
  const device = mockDevice(ctx, "hp1", [0, 45]);
  device.extraEntities.hp1AutomaticFrequencies = { value: "45,50|45,50" };
  device.extraEntities.dayMaxHz = numberEntity(45);
  state.entities.dayMaxHz = numberEntity(90);
  const confirm = ctx.mock.fn(() => false);
  window.confirm = confirm;
  try {
    const { min, max } = frequencyRangeSlider("hp1");
    max.value = "45";
    assert.equal(await move(min, 45), false);
    assert.equal(confirm.mock.callCount(), 1);
    assert.match(confirm.mock.calls[0].arguments[0], /verwarmen \(overdag\)/);
    assert.deepEqual(device.writes, []);
    assert.deepEqual(state.drafts, {});
  } finally {
    delete window.confirm;
  }
});
