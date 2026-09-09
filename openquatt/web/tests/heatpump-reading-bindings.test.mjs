import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { HP_PANEL_CONFIGS } = await import("../js/src/core/config.js");
const { getHeatPumpRuntimeModel, patchHeatPumpPanel, renderHeatPumpSchematic } = await import("../js/src/views/heatpump.js");

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
