import assert from "node:assert/strict";
import { test } from "node:test";
import { compareCapacityToDemand, getCapacitySegments, normalizeHouseCapacity } from "../js/src/settings/house-learning-capacity.js";

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
