import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { FAST_OVERVIEW_KEYS, HP_PANEL_CONFIGS } = await import("../js/src/core/config.js");
const { getHeatPumpRuntimeModel, patchHeatPumpPanel, renderHeatPumpPanel, renderHeatPumpSchematic } = await import("../js/src/views/heatpump.js");
const { setLocale } = await import("../js/src/i18n/index.js");

test("schematic readings and live patches agree without replacing the board", () => {
  state.entities = {};
  const { title, keys, accent } = HP_PANEL_CONFIGS[0];
  const runtime = getHeatPumpRuntimeModel(title, keys, accent);
  const readings = [
    ["flow", "flowText", "Flow"],
    ["discharge-pressure", "dischargePressureText", "Persdruk"],
    ["discharge-temp", "dischargeTempText", "Perstemperatuur"],
    ["suction-pressure", "suctionPressureText", "Zuigdruk"],
    ["suction-temp", "suctionTempText", "Zuigtemperatuur"],
    ["inner-coil-temp", "innerCoilTempText", "Inner coil temperatuur"],
    ["evaporator-temp", "evaporatorCoilTempText", "Verdampertemperatuur"],
    ["outside-temp", "outsideTempText", "Buitentemperatuur"],
    ["fan-speed", "fanRpmText", "Ventilatorsnelheid"],
    ["supply", "waterOutText", "Aanvoer temperatuur"],
    ["return", "waterInText", "Retour temperatuur"],
  ];
  const nodes = new Map();
  for (const [bind, field] of readings) {
    runtime.schematic[field] = `initial-${bind}`;
    for (const suffix of ["value", "reading"]) {
      const attributes = {};
      nodes.set(`${bind}-${suffix}`, {
        textContent: "initial", getAttribute: name => attributes[name],
        setAttribute: (name, value) => { attributes[name] = value; },
      });
    }
  }
  const board = {
    className: "",
    querySelector: selector => nodes.get(selector.match(/^\[data-oq-bind="([^"]+)"\]$/)?.[1]) || null,
  };
  const panel = { querySelector: selector => selector === "[data-oq-hp-board]" ? board : null };
  const html = renderHeatPumpSchematic(runtime.schematic);
  for (const [bind, field, label] of readings) {
    assert.ok(html.includes(`aria-label="${label} initial-${bind}"`));
    runtime.schematic[field] = `updated-${bind}`;
  }
  patchHeatPumpPanel(panel, title, keys, accent, null, runtime);
  for (const [bind, , label] of readings) {
    assert.equal(nodes.get(`${bind}-value`).textContent, `updated-${bind}`);
    assert.equal(nodes.get(`${bind}-reading`).getAttribute("aria-label"), `${label} updated-${bind}`);
  }
});

test("compressor demand is distinct from measured frequency and updates in the schematic", () => {
  const { title, keys, accent } = HP_PANEL_CONFIGS[0];
  state.entities = {
    [keys.freq]: { value: 79, uom: "Hz" },
    hp1CompressorFrequencyDemand: { value: 72, uom: "Hz" },
  };
  const runtime = getHeatPumpRuntimeModel(title, keys, accent);
  const html = renderHeatPumpSchematic(runtime.schematic);
  assert.match(html, /data-oq-bind="compressor-freq">79 Hz<\/text>/);
  assert.match(html, /data-oq-bind="compressor-freq-demand">Gevraagd 72 Hz<\/text>/);
  assert.match(html, /aria-label="Compressorfrequentie 79 Hz, Gevraagd 72 Hz"/);

  const detail = { textContent: "Gevraagd 72 Hz" };
  const trigger = {
    label: "Compressorfrequentie 79 Hz, Gevraagd 72 Hz",
    getAttribute() { return this.label; },
    setAttribute(_name, value) { this.label = value; },
  };
  const nodes = {
    '[data-oq-bind="compressor-freq-demand"]': detail,
    '[data-oq-bind="compressor-freq-trigger"]': trigger,
  };
  const board = { className: "", querySelector: selector => nodes[selector] || null };
  const panel = { querySelector: selector => selector === "[data-oq-hp-board]" ? board : null };
  state.entities.hp1CompressorFrequencyDemand = { value: 64, uom: "Hz" };
  patchHeatPumpPanel(panel, title, keys, accent);
  assert.equal(detail.textContent, "Gevraagd 64 Hz");
  assert.equal(trigger.label, "Compressorfrequentie 79 Hz, Gevraagd 64 Hz");
});

test("compact panels show demand for their own unit and distinguish zero from missing data", () => {
  state.hpVisualMode = "compact";
  state.entities = {
    hp1Power: { value: 1318, uom: "W" },
    hp2Power: { value: 0, uom: "W" },
    hp1Freq: { value: 79, uom: "Hz" },
    hp2Freq: { value: 0, uom: "Hz" },
    hp1CompressorFrequencyDemand: { value: 72, uom: "Hz" },
    hp2CompressorFrequencyDemand: { value: 0, uom: "Hz" },
  };
  const [hp1, hp2] = HP_PANEL_CONFIGS.map(({ title, keys, accent }) => renderHeatPumpPanel(title, keys, accent));
  assert.match(hp1, /<strong>79 Hz<\/strong>\s*<small>Gevraagd 72 Hz<\/small>/);
  assert.match(hp2, /<strong>0 Hz<\/strong>\s*<small>Gevraagd 0 Hz<\/small>/);
  assert.ok(FAST_OVERVIEW_KEYS.includes("hp1CompressorFrequencyDemand"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("hp2CompressorFrequencyDemand"));

  delete state.entities.hp2CompressorFrequencyDemand;
  const { title, keys, accent } = HP_PANEL_CONFIGS[1];
  assert.match(renderHeatPumpPanel(title, keys, accent), /<small>Gevraagd —<\/small>/);
});

test("both layouts estimate current from each unit's power and the schematic updates in place", () => {
  const previousEntities = state.entities;
  const previousVisualMode = state.hpVisualMode;
  try {
    setLocale("nl");
    state.entities = { hp1Power: { value: 1588, uom: "W" }, hp2Power: { value: 1372, uom: "W" } };
    for (const mode of ["compact", "schematic"]) {
      state.hpVisualMode = mode;
      const [hp1, hp2] = HP_PANEL_CONFIGS.map(({ title, keys, accent }) => renderHeatPumpPanel(title, keys, accent));
      assert.match(hp1, /1588 W <small[^>]*>\(~6,9A\)<\/small>/);
      assert.match(hp2, /1372 W <small[^>]*>\(~6,0A\)<\/small>/);
    }

    const { title, keys, accent } = HP_PANEL_CONFIGS[1];
    const power = { innerHTML: "1372 W <small>(~6,0A)</small>" };
    const board = { className: "", querySelector: selector => selector === '[data-oq-bind="footer-power"]' ? power : null };
    const panel = { querySelector: selector => selector === "[data-oq-hp-board]" ? board : null };

    state.entities.hp2Power.value = "unavailable";
    patchHeatPumpPanel(panel, title, keys, accent);
    assert.equal(power.innerHTML, "—");
    state.entities.hp2Power.value = 0;
    patchHeatPumpPanel(panel, title, keys, accent);
    assert.equal(power.innerHTML, "0 W <small>(~0,0A)</small>");
    setLocale("en");
    state.entities.hp2Power.value = 1372;
    patchHeatPumpPanel(panel, title, keys, accent);
    assert.equal(power.innerHTML, "1372 W <small>(~6.0A)</small>");
  } finally {
    state.entities = previousEntities;
    state.hpVisualMode = previousVisualMode;
    setLocale("nl");
  }
});

test("defrost takes priority over heating in both layouts and clears in the live schematic", () => {
  const { title, keys, accent } = HP_PANEL_CONFIGS[1];
  state.entities = {
    [keys.mode]: { value: "Heating" },
    [keys.defrost]: { value: true },
    [keys.fourWay]: { value: true },
    [keys.freq]: { value: 72 },
    [keys.power]: { value: 1330 },
    [keys.heat]: { value: 3796 },
    [keys.cop]: { value: 2.9 },
  };
  state.hpVisualMode = "compact";
  const compact = renderHeatPumpPanel(title, keys, accent);
  assert.match(compact, /data-oq-bind="panel-status">Ontdooien<\/span>/);
  assert.match(compact, /<strong class="oq-stat-value">n\.v\.t\.<\/strong>/);
  state.hpVisualMode = "schematic";
  const schematic = renderHeatPumpPanel(title, keys, accent);
  assert.match(schematic, /data-oq-bind="footer-mode">Ontdooien<\/strong>/);
  assert.match(schematic, /data-oq-bind="footer-efficiency">n\.v\.t\.<\/strong>/);

  state.entities[keys.defrost] = { value: false };
  assert.equal(getHeatPumpRuntimeModel(title, keys, accent).schematic.reverseCycle, true);

  const mode = { textContent: "Ontdooien" };
  const cop = { textContent: "n.v.t." };
  const nodes = {
    '[data-oq-bind="footer-mode"]': mode,
    '[data-oq-bind="footer-efficiency"]': cop,
  };
  const board = { className: "", querySelector: selector => nodes[selector] || null };
  const panel = { querySelector: selector => selector === "[data-oq-hp-board]" ? board : null };
  state.entities[keys.fourWay] = { value: false };
  patchHeatPumpPanel(panel, title, keys, accent);
  assert.equal(mode.textContent, "Verwarmen");
  assert.equal(cop.textContent, "2.9");
  assert.doesNotMatch(board.className, /is-defrost|is-reversed/);
});
