import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { renderNumberInputControl } = await import("../js/src/core/number-controls.js");
const { state } = await import("../js/src/core/state.js");
const { renderSettingsMiniNumberField } = await import("../js/src/settings/controls.js");
const options = { key: "example", value: 4, meta: { min: -30, max: 30, step: 1 }, controlClass: "oq-helper-control" };

test("compact alarm limit inputs reserve suffix space only when a unit exists", () => {
  state.loadingEntities = false;
  for (const [key, value, max] of [["compressorStarts2hWarningLimit", 6, 20], ["compressorStarts72hWarningLimit", 40, 120]]) {
    state.entities[key] = { value, min_value: 1, max_value: max, step: 1 };
    const html = renderSettingsMiniNumberField(key, key, "", { compact: true });
    assert.doesNotMatch(html, /oq-helper-control--suffix|oq-helper-unit-chip|disabled/);
    assert.match(html, new RegExp(`value="${value}"`));
    assert.match(html, new RegExp(`data-oq-field="${key}"`));
    delete state.entities[key];
  }
  state.entities.phDemandRiseTime = { value: 10, uom: "min" };
  const html = renderSettingsMiniNumberField("phDemandRiseTime", "Rise", "", { compact: true });
  assert.match(html, /oq-helper-control--suffix/);
  assert.match(html, /oq-helper-unit-chip">min<\/span>/);
  delete state.entities.phDemandRiseTime;
});

test("entity number controls keep their key, bounds and loading gate", () => {
  for (const loading of [false, true]) {
    state.loadingEntities = loading;
    const html = renderNumberInputControl(options);
    assert.match(html, /<label class="oq-helper-control">/);
    assert.match(html, /data-oq-field="example"/);
    assert.match(html, /min="-30"\s+max="30"\s+step="1"/);
    assert.equal(/\bdisabled\b/.test(html), loading);
  }
});

test("service number controls keep custom gates and never opt into entity writes", () => {
  state.loadingEntities = true;
  for (const disabled of [false, true]) {
    const html = renderNumberInputControl({
      ...options, key: undefined, controlTag: "span", disabled,
      value: '\"><img src=x>', inputAttributes: 'data-oq-odu-settings-hp="1"',
    });
    assert.match(html, /<span class="oq-helper-control">/);
    assert.doesNotMatch(html, /<label|data-oq-field|<img/);
    assert.match(html, /&quot;&gt;&lt;img/);
    assert.equal(/\bdisabled\b/.test(html), disabled);
  }
});
