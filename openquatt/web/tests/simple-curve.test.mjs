import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };

const { CURVE_POINTS } = await import("../js/src/core/config.js");
const { INITIAL_SETTINGS_READY_KEY_MAP, SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");
const { state } = await import("../js/src/core/state.js");
const { applySimpleCurvePoints, generateSimpleCurve, getSimpleCurveDraft, updateSimpleCurveDraft } = await import("../js/src/core/simple-curve.js");
const { renderCurveGraph, renderSettingsCurveInputs, renderSimpleCurvePreview } = await import("../js/src/settings/heating.js");

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
  assert.match(renderSimpleCurvePreview(), /<small>0°C<\/small><strong>40\.0°<\/strong>/);
  const markup = renderSettingsCurveInputs();
  assert.match(markup, /data-oq-action="apply-simple-curve"/);
  assert.match(markup, /oq-simple-curve-workspace/);
  assert.doesNotMatch(markup, /maxWater/);
  assert.match(markup, /data-oq-settings-advanced="curve-points"/);
  for (const point of CURVE_POINTS) assert.match(markup, new RegExp(point.key));
});

test("voorbeeld toont de installatiegrens zonder hogere opgeslagen curvepunten te verbergen", () => {
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.heating.includes("maxWater"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.heating.includes("maxWater"));
  state.simpleCurveDraft = { slope: 15, level: 40 };
  state.entities = { maxWater: { value: 60 } };
  const preview = renderSimpleCurvePreview();
  assert.match(preview, /<small>-20°C<\/small><strong>60\.0°<\/strong><small>onbegrensd 70\.0°<\/small>/);
  assert.match(preview, /stroke-dasharray="4 4"/);
  assert.match(preview, />70°C<\/text>/);
  assert.doesNotMatch(preview, /<text[^>]*>−20°C<\/text>/);
  assert.match(renderSettingsCurveInputs(), /Begrensd op 60 °C uit Installatie/);
  assert.equal(generateSimpleCurve(15, 40)[0].value, 70);
  state.simpleCurveDraft = { slope: 5, level: 54 };
  const line = renderSimpleCurvePreview().match(/<polyline points="([^"]+)" class="oq-simple-curve-line" \/>/)?.[1];
  assert.ok(line);
  const positions = line.split(" ").map((position) => position.split(",").map(Number));
  assert.equal(positions.length, 7);
  assert.equal(positions[0][1], positions[1][1]);
  assert.ok(positions[0][0] < positions[1][0] && positions[1][0] < positions[2][0]);
  state.entities.curveM20 = { value: 64 };
  const manual = renderCurveGraph();
  assert.match(manual, /class="oq-simple-curve-chart oq-helper-curve-svg"/);
  assert.match(manual, /data-curve-key="curveM20"/);
  assert.match(manual, /begrensd 60\.0°/);
  assert.doesNotMatch(preview, /is-zero/);
  state.simpleCurveDraft = null;
});

test("de gebouwde firmwarebundel bevat de diagnostische vertalingen", () => {
  const bundle = readFileSync(new URL("../js/openquatt-app.js", import.meta.url), "utf8");
  for (const label of ["Basisdoel", "Externe modifier", "Kamercorrectie", "Effectief doel"]) {
    assert.ok(bundle.includes(label), `${label} ontbreekt in de compacte bundel`);
  }
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
