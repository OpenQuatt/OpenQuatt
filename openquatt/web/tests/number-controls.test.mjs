import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { renderNumberInputControl } = await import("../js/src/core/number-controls.js");
const { state } = await import("../js/src/core/state.js");
const options = { key: "example", value: 4, meta: { min: -30, max: 30, step: 1 }, controlClass: "oq-helper-control" };

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
