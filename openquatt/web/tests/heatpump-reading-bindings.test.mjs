import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { FAST_OVERVIEW_KEYS, HP_PANEL_CONFIGS } = await import("../js/src/core/config.js");
const { getHeatPumpRuntimeModel, patchHeatPumpPanel, renderHeatPumpPanel, renderHeatPumpSchematic } = await import("../js/src/views/heatpump.js");

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
