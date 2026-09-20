import assert from "node:assert/strict";
import test from "node:test";
import { getHouseLearningChartModel, normalizeHouseLearningExport, renderHouseLearningChart } from "../js/src/settings/house-learning-chart.js";

const payload = {
  schema: 1,
  mode: "passive",
  record_columns: ["start_epoch_s", "end_epoch_s", "mean_room_c", "mean_setpoint_c", "mean_outside_c", "mean_heat_w", "room_trend_k_per_h", "context_revision"],
  records: [[1700000000, 1700001800, 20.1, 20.5, 5.2, 2200, 0.01, 4], [1700003600, 1700005400, 20.2, 20.5, 8.1, 1650, 0.01, 4]],
};

test("batchexport gebruikt alleen de firmwarekolommen en verwerpt ontbrekende waarden", () => {
  assert.deepEqual(normalizeHouseLearningExport(payload), [{ startEpoch: 1700000000, endEpoch: 1700001800, outsideC: 5.2, heatW: 2200 }, { startEpoch: 1700003600, endEpoch: 1700005400, outsideC: 8.1, heatW: 1650 }]);
  assert.deepEqual(normalizeHouseLearningExport({ ...payload, records: [[1700000000, 1700001800, 20.1, 20.5, null, 2200, 0.01, 4]] }), []);
  assert.throws(() => normalizeHouseLearningExport({ ...payload, record_columns: [] }), /meetkolommen/);
});

test("ingestelde lijn volgt de structurele formule zonder intern nuldefault", () => {
  const model = getHouseLearningChartModel([], { coldC: -10, zeroC: 16, ratedW: 5200 }, {});
  assert.equal(model.configuredValid, true);
  assert.ok(model.axisMaxY >= 5200);
  assert.equal(getHouseLearningChartModel([], { coldC: null, zeroC: 16, ratedW: 5200 }, {}).configuredValid, false);
});

test("grafiek toont een blauwe configuratielijn zonder meetpunten en markeert een voorlopige fit", () => {
  const markup = renderHouseLearningChart([], { coldC: -10, zeroC: 16, ratedW: 5200 }, { h: 186, t0: 16.8, ready: false });
  assert.match(markup, /chart-line--configured/);
  assert.match(markup, /volledig een extrapolatie/);
  assert.match(markup, /voorlopig en nog niet gevalideerd/);
});

test("groene lijn wordt binnen het meetbereik doorgetrokken en daarbuiten gestreept", () => {
  const markup = renderHouseLearningChart(normalizeHouseLearningExport(payload), { coldC: -10, zeroC: 16, ratedW: 5200 }, { h: 186, t0: 16.8, ready: true });
  assert.match(markup, /chart-line--learned\"/);
  assert.match(markup, /chart-line--learned-dashed/);
  assert.match(markup, /Duur: 30 min/);
});


test("geleerde lijn raakt nul op T₀ en blijft daarboven nul", () => {
  const records = normalizeHouseLearningExport(payload);
  const config = { coldC: -10, zeroC: 16, ratedW: 5200 };
  const fit = { h: 200, t0: 12, ready: true };
  const model = getHouseLearningChartModel(records, config, fit);
  const markup = renderHouseLearningChart(records, config, fit);
  assert.ok(markup.includes(`L ${model.x(12).toFixed(1)} ${model.y(0).toFixed(1)} L ${model.x(model.maxX).toFixed(1)} ${model.y(0).toFixed(1)}`));
  assert.ok(model.axisMaxY >= 200 * (16 - model.minX));
});

test("één meetpunt verbergt een beschikbare fit niet; zonder fit geen groene lijn", () => {
  const records = normalizeHouseLearningExport(payload).slice(0, 1);
  const configured = { coldC: -10, zeroC: 16, ratedW: 5200 };
  assert.match(renderHouseLearningChart(records, configured, { h: 200, t0: 16 }), /<path[^>]+chart-line--learned-dashed/);
  assert.doesNotMatch(renderHouseLearningChart(records, configured, {}), /<path[^>]+chart-line--learned/);
  assert.doesNotMatch(renderHouseLearningChart([], configured, { h: 200, t0: 16 }), /<path[^>]+chart-line--learned"/);
});


test("buitenas blijft altijd -10 tot 20; meetpunten buiten beeld blijven buiten de plot", () => {
  const records = [{ startEpoch: 1700000000, endEpoch: 1700014400, outsideC: -15, heatW: 5000 }];
  const model = getHouseLearningChartModel(records, { coldC: -20, zeroC: 25, ratedW: 5200 }, { h: 200, t0: 24 });
  assert.equal(model.minX, -10);
  assert.equal(model.maxX, 20);
  const html = renderHouseLearningChart(records, {}, {});
  assert.doesNotMatch(html, /data-oq-house-learning-tip=/);
  assert.match(html, /buiten de getoonde as/);
});
