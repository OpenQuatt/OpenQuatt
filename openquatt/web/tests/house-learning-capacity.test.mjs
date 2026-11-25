import assert from "node:assert/strict";
import { test } from "node:test";
import { compareCapacityToDemand, getCapacitySegments, normalizeHouseCapacity } from "../js/src/settings/house-learning-capacity.js";
globalThis.__OQ_PREVIEW__ = false;
const { renderHouseLearningChart } = await import("../js/src/settings/house-learning-chart.js");

const profile = (points) => normalizeHouseCapacity({ flow_lph: 800, limit_w: 3400, cap_hz: 90, points });

test("capacity requires a bounded ordered controller profile and keeps zero distinct from missing", () => {
  assert.equal(profile([[-1000, 6000, 5800], [-1000, 7000, 6800]]), null);
  assert.equal(profile([[-1000, "6000", 5800], [-900, 7000, 6800]]), null);
  assert.equal(profile([[-1600, 0, 0], [-1500, 0, 0]]), null);
  assert.equal(profile(null), null);
  const data = profile([[-1000, 0, null], [-900, 0, 0], [-800, 0, 0]]);
  assert.deepEqual(getCapacitySegments(data, 35).flat().map(point => point.watts), [0, 0, 0]);
  assert.deepEqual(getCapacitySegments(data, 45).flat().map(point => point.outsideC), [-9, -8]);
  assert.deepEqual(getCapacitySegments(data, 55), []);
});

test("capacity does not bridge missing samples or abrupt model-cell boundaries", () => {
  const data = profile([[-600, 6000, 5800], [-501, 5900, 5700], [-500, 7000, 6800],
    [-400, null, 6900], [-300, 7000, 6800]]);
  const segments = getCapacitySegments(data, 35);
  assert.deepEqual(segments.map(points => points.map(point => point.outsideC)), [[-6, -5.01], [-5], [-3]]);
  assert.equal(compareCapacityToDemand(segments, () => 6500).crossing, null);
  assert.deepEqual(getCapacitySegments(data, 45).map(points => points.length), [2, 3]);
});

test("crossing and reserve stay inside both supported temperature ranges", () => {
  const data = profile([[-1100, 6500, 6400], [-1000, 7000, 6900], [-900, 7500, 7400], [-800, 8000, 7900]]);
  const segments = getCapacitySegments(data, 35);
  const comparison = compareCapacityToDemand(segments, () => 7000);
  assert.equal(comparison.crossing.outsideC, -10);
  assert.equal(comparison.reserves[0][0].outsideC, -10);
  assert.equal(compareCapacityToDemand(segments, () => 7000, [-9.5, -8]).crossing, null);
  assert.equal(compareCapacityToDemand(segments, () => 7000, [-10.75, -8]).crossing.outsideC, -10);
  assert.equal(compareCapacityToDemand(getCapacitySegments(data, 45), () => 7000).crossing.outsideC, -9.8);
  assert.equal(compareCapacityToDemand(segments, c => c === -10 ? 6500 : 7600).crossing, null);
});

test("crossing follows the drawn demand line at a fractional heating cutoff", () => {
  const segments = getCapacitySegments(profile([[500, 1000, 1000], [600, 1000, 1000]]), 35);
  const demand = temperature => Math.max(0, 9000 * (5.5 - temperature));
  const result = compareCapacityToDemand(segments, demand, undefined, 5.5);
  assert(Math.abs(result.crossing.outsideC - (5.5 - 1 / 9)) < 1e-6);
  assert(result.reserves[0].some(point => point.outsideC === 5.5));
});

test("hiding capacity restores the chart axes for the housing lines and measurements", () => {
  const configured = { coldC: -10, zeroC: 16, ratedW: 3200 };
  const data = profile([[-1500, 10000, 9900], [-1400, 11000, 10800]]);
  const shown = renderHouseLearningChart([], configured, {}, { profile: data });
  const hidden = renderHouseLearningChart([], configured, {}, { profile: data, hiddenSeries: ["capacity"] });
  assert.match(shown, /class="oq-house-learning-chart-axis">-15°<\/text>/);
  assert.match(shown, /class="oq-house-learning-chart-axis">12<\/text>/);
  assert.match(hidden, /class="oq-house-learning-chart-axis">-10°<\/text>/);
  assert.match(hidden, /class="oq-house-learning-chart-axis">4<\/text>/);
  assert.doesNotMatch(hidden, /class="oq-house-learning-chart-axis">(?:-15°|12)<\/text>/);
});

test("legend keeps all available controls while removing only the hidden series and their overlays", () => {
  const records = [{ startEpoch: 100, endEpoch: 86500, outsideC: -6, heatW: 4200 }];
  const configured = { coldC: -10, zeroC: 16, ratedW: 7000 };
  const learned = { preview: { h: 240, t0: 17, minC: -7, maxC: 1, count: 10, days: 10, cov00: 2000, cov01: 0, cov11: 0 } };
  const options = { profile: profile([[-1100, 6500, 6400], [-1000, 7000, 6900], [-900, 7500, 7400]]), supplyC: 45 };
  const render = hiddenSeries => renderHouseLearningChart(records, configured, learned, { ...options, hiddenSeries });
  const series = {
    configured: "chart-line--configured", records: "chart-dot", learned: "chart-line--learned-dashed",
    interval: "chart-interval", capacity: "chart-line--capacity",
  };
  const full = render([]);
  assert.match(full, /class="oq-house-learning-capacity-crossing"/);
  assert.match(full, /class="oq-house-learning-capacity-reserve"/);
  for (const [key, className] of Object.entries(series)) {
    assert.match(full, new RegExp(`data-oq-house-learning-series="${key}" aria-pressed="true"`));
    assert.match(full, new RegExp(`oq-house-learning-${className}"`));
    const hidden = render([key]);
    assert.match(hidden, new RegExp(`data-oq-house-learning-series="${key}" aria-pressed="false"`));
    assert.doesNotMatch(hidden, new RegExp(`oq-house-learning-${className}"`));
    assert.match(hidden, /data-oq-house-capacity-supply><option value="35".*<option value="45" selected/s);
    if (key === "configured" || key === "capacity") {
      assert.doesNotMatch(hidden, /class="oq-house-learning-capacity-(crossing|reserve)"/);
    }
  }
});
