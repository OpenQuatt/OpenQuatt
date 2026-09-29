import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };

const { CURVE_POINTS } = await import("../js/src/core/config.js");
const { state } = await import("../js/src/core/state.js");
const { applySimpleCurvePoints, generateSimpleCurve, getSimpleCurveDraft, updateSimpleCurveDraft } = await import("../js/src/core/simple-curve.js");
const { renderSettingsCurveInputs, renderSimpleCurvePreview } = await import("../js/src/settings/heating.js");

test("Simple genereert precies de zes canonieke curvepunten met begrenzing", () => {
  const points = generateSimpleCurve(5, 40);
  assert.deepEqual(points.map(({ key }) => key), CURVE_POINTS.map(({ key }) => key));
  assert.deepEqual(points.map(({ value }) => value), [50, 45, 40, 37.5, 35, 32.5]);
  assert.equal(generateSimpleCurve(15, 20)[0].value, 50);
  assert.equal(generateSimpleCurve(15, 20)[5].value, 20);
  assert.equal(generateSimpleCurve(Number.NaN, 40), null);
});

test("Simple toont live preview en Advanced houdt de zes handmatige velden", () => {
  state.simpleCurveDraft = null;
  state.entities = Object.fromEntries(CURVE_POINTS.map((point, index) => [point.key, { value: [55, 50, 45, 42.5, 40, 37.5][index] }]));
  assert.deepEqual(getSimpleCurveDraft(), { slope: 5, level: 45 });
  assert.equal(updateSimpleCurveDraft("level", "40"), true);
  assert.match(renderSimpleCurvePreview(), /0°C → 40\.0 °C/);
  const markup = renderSettingsCurveInputs();
  assert.match(markup, /data-oq-action="apply-simple-curve"/);
  assert.match(markup, /data-oq-settings-advanced="curve-points"/);
  for (const point of CURVE_POINTS) assert.match(markup, new RegExp(point.key));
});

test("Simple herstelt alle punten na een deels geaccepteerde maar onbevestigde schrijfopdracht", async () => {
  const originals = [55, 50, 45, 42.5, 40, 37.5];
  const remote = new Map(CURVE_POINTS.map((point, index) => [point.key, originals[index]]));
  const writes = [];
  const result = await applySimpleCurvePoints(generateSimpleCurve(5, 40), originals, async (key, value) => {
    writes.push([key, value]);
    remote.set(key, value);
    return writes.length !== 2;
  }, (key) => remote.get(key));
  assert.deepEqual(result, { applied: false, restored: true });
  assert.deepEqual(CURVE_POINTS.map((point) => remote.get(point.key)), originals);
  assert.equal(writes.length, 8);
});

test("Simple meldt onzeker herstel als een terugschrijfopdracht faalt", async () => {
  const originals = [55, 50, 45, 42.5, 40, 37.5];
  const remote = new Map(CURVE_POINTS.map((point, index) => [point.key, originals[index]]));
  let writes = 0;
  const result = await applySimpleCurvePoints(generateSimpleCurve(5, 40), originals, async (key, value) => {
    writes += 1;
    if (writes === 3) return false;
    remote.set(key, value);
    return writes !== 2;
  }, (key) => remote.get(key));
  assert.deepEqual(result, { applied: false, restored: false });
});
