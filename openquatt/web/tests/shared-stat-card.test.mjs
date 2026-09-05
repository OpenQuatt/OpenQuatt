import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { renderStatCard } from "../js/src/views/stat-card.js";

globalThis.__OQ_PREVIEW__ = false;
globalThis.localStorage = { getItem: () => null };
const { renderOverviewMetricCard, renderOverviewStatCardMarkup } = await import("../js/src/views/overview.js");
const { renderEnergyHistoryStat } = await import("../js/src/views/energy.js");
const { getBoilerPanelModel, renderBoilerCompactPanel, patchBoilerPanelRuntime } = await import("../js/src/views/heatpump.js");
const { state } = await import("../js/src/core/state.js");

test("stat cards escape all text and value attributes; empty notes add no element", () => {
  const html = renderStatCard({ label: '<Label>', value: '<img src=x onerror="alert(1)">', note: "A & B", valueData: { "oq-bind": 'value" onclick="alert(1)' } });
  assert.match(html, /&lt;Label&gt;/);
  assert.match(html, /&lt;img src=x onerror=&quot;alert\(1\)&quot;&gt;/);
  assert.match(html, /A &amp; B/);
  assert.match(html, /data-oq-bind="value&quot; onclick=&quot;alert\(1\)"/);
  assert.doesNotMatch(html, /<img| onclick="/);
  assert.doesNotMatch(renderStatCard({ label: "Zero", value: 0 }), /oq-stat-note/);
  assert.match(renderStatCard({ label: "Zero", value: 0 }), />0<\/strong>/);
  assert.throws(() => renderStatCard({ valueData: { 'bind" onclick': "bad" } }), /Invalid stat data attribute/);
});

test("shared cards retain overview label colors and metric accents while energy stays neutral", () => {
  const card = { label: "Elektrisch", value: "450 W", note: "Nu" };
  const expected = renderStatCard(card);
  for (const tone of ["blue", "orange", "green", "sky"]) {
    assert.equal(renderOverviewStatCardMarkup({ ...card, tone }), renderStatCard({ ...card, tone }));
    assert.match(renderOverviewStatCardMarkup({ ...card, tone }), new RegExp(`oq-stat--${tone}`));
  }
  assert.equal(renderOverviewMetricCard(card.label, card.value, "blue", card.note), renderStatCard({ ...card, tone: "blue", accent: true }));
  assert.equal(renderEnergyHistoryStat(card.label, card.value, card.note), expected);
  const status = renderOverviewStatCardMarkup({ ...card, value: "Wachten op voldoende warmtevraag", status: true, tone: "orange" });
  assert.match(status, /class="oq-stat oq-stat--status oq-stat--orange"/);
  assert.doesNotMatch(status, /warning|success/);
  assert.doesNotMatch(renderStatCard({ ...card, tone: '\" onclick=\"bad' }), /onclick/);
});

test("shared styles inherit theme tokens, retain the exact overview tones and wrap long values", async () => {
  const css = await readFile(new URL("../css/src/06-shared-stats.css", import.meta.url), "utf8");
  for (const token of ["card", "line", "ink", "soft", "radius-md"]) {
    assert.ok(css.includes(`var(--oq-helper-${token})`));
  }
  assert.match(css, /\.oq-stat-value\s*\{[^}]*overflow-wrap:\s*anywhere/);
  for (const [tone, color] of Object.entries({ blue: "#2563eb", orange: "#ea580c", green: "#16a34a", sky: "#0284c7" })) {
    assert.match(css, new RegExp(`\\.oq-stat--${tone}\\s*\\{[^}]*--oq-stat-tone:\\s*${color}`));
  }
  assert.doesNotMatch(css, /box-shadow/);
});

test("both compact boiler transports keep all three stat value bindings live", () => {
  const previousEntities = state.entities;
  try {
    for (const connection of ["OpenTherm", "Relais"]) {
      state.entities = { boilerConnection: { value: connection } };
      const model = getBoilerPanelModel();
      assert.equal(model.opentherm, connection === "OpenTherm");
      const html = renderBoilerCompactPanel(model);
      const fields = [
        ['data-oq-boiler-heat-value=""', "heatText", "2200 W"],
        ['data-oq-bind="boiler-return-value"', "returnTempText", "31.1 °C"],
        ['data-oq-bind="boiler-supply-value"', "supplyTempText", "42.3 °C"],
      ];
      const nodes = new Map();
      for (const [attribute, field] of fields) {
        assert.ok(html.includes(`<strong class="oq-stat-value" ${attribute}>${model[field]}</strong>`));
        nodes.set(`[${attribute === 'data-oq-boiler-heat-value=""' ? "data-oq-boiler-heat-value" : attribute}]`, { textContent: model[field] });
      }
      const panel = { querySelector: selector => nodes.get(selector) || null, querySelectorAll: () => [] };
      const updated = { ...model, ...Object.fromEntries(fields.map(([, field, value]) => [field, value])) };
      patchBoilerPanelRuntime(panel, updated);
      for (const [, , value] of fields) assert.ok([...nodes.values()].some(node => node.textContent === value));
      assert.equal(nodes.size, 3);
    }
  } finally {
    state.entities = previousEntities;
  }
});
