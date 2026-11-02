import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { setLocale } from "../js/src/i18n/index.js";

globalThis.__OQ_PREVIEW__ = false;
const { getHouseLearningReadiness, renderHouseLearningReadiness } = await import("../js/src/settings/house-learning-readiness.js");
const { normalizeHouseLearningExport, renderHouseLearningChart } = await import("../js/src/settings/house-learning-chart.js");

const records = (temperatures) => temperatures.map((outsideC, index) => ({
  startEpoch: 1700000000 + index * 86400, endEpoch: 1700086400 + index * 86400,
  outsideC, heatW: 1500, recordKind: 1, referenceRoomC: 20,
}));
const config = { persist: false, applyDocument: false, notify: false };
test.afterEach(() => setLocale("nl", config));

test("de getoonde standaardgrenzen blijven gelijk aan de firmware-fitconfiguratie", async () => {
  const firmware = await readFile(new URL("../../includes/learning/oq_ph_learning_fit.h", import.meta.url), "utf8");
  const ui = await readFile(new URL("../js/src/settings/house-learning-readiness.js", import.meta.url), "utf8");
  for (const [field, constant] of [["min_train_days", "TRAIN_DAYS"], ["min_train_segments", "TRAIN_DAYS"], ["min_holdout_days", "CHECK_DAYS"], ["min_holdout_segments", "CHECK_DAYS"], ["min_outside_p90_p10_k", "REQUIRED_SPREAD_C"], ["min_segments_per_temperature_bin", "MIN_PER_BIN"], ["max_temperature_bin_fraction", "MAX_BIN_FRACTION"]]) {
    const expected = Number(firmware.match(new RegExp(`\\b${field} = ([\\d.]+)`))?.[1]);
    const actual = Number(ui.match(new RegExp(`\\b${constant} = ([\\d.]+)`))?.[1]);
    assert.ok(Number.isFinite(expected) && Number.isFinite(actual));
    assert.equal(actual, expected, field);
  }
});

test("negen dagen: zes leren, laatste drie controleren, exact zes graden is voldoende", () => {
  const data = records([4, 4, 7, 7, 10, 10, -10, 18, 20]);
  const before = JSON.stringify(data);
  const progress = getHouseLearningReadiness(data);
  assert.equal(progress.count, 9);
  assert.equal(progress.trainCount, 6);
  assert.equal(progress.checkCount, 3);
  assert.equal(progress.spread, 6);
  assert.deepEqual(progress.bins, [2, 2, 2]);
  assert.equal(progress.missingDays, 0);
  assert.equal(progress.missingSpread, 0);
  assert.equal(progress.distributionReady, true);
  assert.equal(JSON.stringify(data), before);
});

test("twaalf dagen met vergelijkbaar weer melden temperatuurtekort en geen tekort aan dagen", () => {
  const data = records([8.4, 8.5, 9, 10, 11, 12.2, 12.3, 10, 11, 0, 0, 0]);
  const progress = getHouseLearningReadiness(data);
  assert.equal(progress.trainCount, 9);
  assert.ok(Math.abs(progress.spread - 3.9) < 1e-9);
  const html = renderHouseLearningReadiness(data);
  assert.match(html, /12 <span>\/ 9 nodig/);
  assert.match(html, /3,9 °C <span>\/ 6 °C nodig/);
  assert.match(html, /Nog 2,1 °C spreiding nodig/);
  assert.match(html, /meer verschil in buitentemperatuur nodig/);
  assert.doesNotMatch(html, /Nog [1-9]+ dagmetingen nodig/);
});

test("firmwaredagmetingen met UTC-afwijkingen tellen mee in aantal, leersplit en spreiding", () => {
  const temperatures = [11.1, 12.35, 11.1, 8.6, 11.85, 8.4, 9.6, 10.1,
    10.3, 10, 9.15, 11.7, 10.85, 10.65, 9.3, 8.3];
  const utcOffsets = [-1, 1, -1, -1, 1, 0, -1, 1, -1, 0, 1, 0, 1, 0, 0, 0];
  const data = normalizeHouseLearningExport({
    schema: 1, mode: "passive",
    record_columns: ["start_epoch_s", "end_epoch_s", "mean_outside_c", "mean_heat_w", "effective_outside_c", "record_kind"],
    records: temperatures.map((temperature, index) => {
      const start = 1700000000 + index * 86410;
      return [start, start + 86400 + utcOffsets[index], temperature, 1500, temperature, 1];
    }),
  });
  assert.equal(data.filter((record) => record.endEpoch - record.startEpoch === 86400).length, 6);
  const progress = getHouseLearningReadiness(data);
  assert.equal(progress.count, 16);
  assert.equal(progress.trainCount, 13);
  assert.equal(progress.checkCount, 3);
  assert.equal(progress.missingDays, 0);
  assert.equal(progress.p10, 8.6);
  assert.equal(progress.p90, 11.85);
  assert.ok(Math.abs(progress.spread - 3.25) < 1e-9);
  const html = renderHouseLearningChart(data, {}, {});
  assert.match(html, /16 <span>\/ 9 nodig/);
  assert.match(html, /Leren: 13 · Controleren: 3/);
  assert.match(html, /3,25 °C <span>\/ 6 °C nodig/);
  assert.doesNotMatch(html, /Nog [1-9]+ (?:volledige )?dagmetingen nodig/);
  assert.doesNotMatch(html, /Geleerde woninglijn beschikbaar/);
  setLocale("en", config);
  const en = renderHouseLearningChart(data, {}, {});
  assert.match(en, /16 <span>\/ 9 needed/);
  assert.match(en, /Training: 13 · Checking: 3/);
  assert.match(en, /3\.25 °C <span>\/ 6 °C needed/);
});

test("perioden zonder firmwaredagmarkering vullen de dagenteller niet", () => {
  const data = records([4, 5, 6, 7]);
  data.push({ ...data[0], recordKind: 0 },
    { ...data[0], recordKind: 0, endEpoch: data[0].startEpoch + 14400 },
    { ...data[0], recordKind: undefined }, { ...data[0], recordKind: 1, outsideC: NaN });
  const progress = getHouseLearningReadiness(data);
  assert.equal(progress.count, 4);
  assert.equal(progress.missingDays, 5);
  assert.match(renderHouseLearningReadiness(data), /Nog 5 volledige dagmetingen nodig/);
  const empty = getHouseLearningReadiness([]);
  assert.equal(empty.spread, null);
  assert.match(renderHouseLearningReadiness([]), /Nog niet te bepalen/);
  assert.doesNotMatch(renderHouseLearningReadiness([]), /0 °C <span>\/ 6 °C nodig/);
});

test("temperatuurverdeling en dominantie blijven vereist naast het totale bereik", () => {
  const sparse = getHouseLearningReadiness(records([0, 0, 0, 0, 3, 6, 0, 0, 0]));
  assert.equal(sparse.spread, 6);
  assert.equal(sparse.distributionReady, false);
  const dominant = getHouseLearningReadiness(records([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1.2, 1.2, 3, 3, 0, 0, 0]));
  assert.equal(dominant.distributionReady, false);
  assert.match(renderHouseLearningReadiness(records([0, 0, 0, 0, 3, 6, 0, 0, 0])), /minimaal twee per temperatuurgebied/);
});

test("percentielindex volgt firmware en onafhankelijke controledagen verbreden het leerbereik niet", () => {
  const data = records([-50, ...Array.from({ length: 19 }, (_, i) => i), 100, -100, 100, 100]);
  const progress = getHouseLearningReadiness(data);
  assert.equal(progress.p10, 1);
  assert.equal(progress.p90, 17);
  const shuffled = [...data].reverse();
  assert.deepEqual(getHouseLearningReadiness(shuffled), progress);
});

test("firmwareafwijzingen blijven leidend; voldoen aan telcriteria claimt geen gevalideerd advies", () => {
  const data = records([4, 4, 7, 7, 10, 10, 4, 7, 10]);
  assert.match(renderHouseLearningReadiness(data, { batchStatus: "mixed_context" }), /verschillende meetcontexten/);
  assert.match(renderHouseLearningReadiness(data, { batchStatus: "unstable_fit" }), /één leerdag wordt weggelaten/);
  assert.match(renderHouseLearningReadiness(data), /De controller moet de lijn ook kunnen berekenen/);
  assert.doesNotMatch(renderHouseLearningReadiness(data), /Geleerde woninglijn beschikbaar/);
  assert.match(renderHouseLearningReadiness(data, { batchStatus: "insufficient_data" }), /controller heeft nog onvoldoende bruikbare dagmetingen/);
  for (const batchStatus of ["insufficient_spread", "day_dominance"]) {
    assert.match(renderHouseLearningReadiness(data, { batchStatus }), /controller beoordeelt de temperatuurverdeling nog als onvoldoende/);
  }
  const hostile = renderHouseLearningReadiness(data, { batchStatus: "<script>alert(1)</script>" });
  assert.doesNotMatch(hostile, /<script>/);
  assert.doesNotThrow(() => renderHouseLearningReadiness(data, { batchStatus: "constructor" }));
});

test("voorlopige en goedgekeurde lijn zijn onderscheiden en vertalingen wisselen mee", () => {
  const data = records([4, 4, 7, 7, 10, 10, 4, 7, 10]);
  assert.match(renderHouseLearningReadiness(data, { h: 200, t0: 16 }), /Geleerde woninglijn is nog voorlopig/);
  assert.match(renderHouseLearningReadiness(data, { h: 200, t0: 16, ready: true }), /Geleerde woninglijn beschikbaar/);
  setLocale("en", config);
  const html = renderHouseLearningReadiness(records([8, 8.2, 9, 9.2, 10, 10.2, 8, 9, 10]));
  assert.match(html, /2\.2 °C <span>\/ 6 °C needed/);
  assert.match(html, /3\.8 °C more spread needed/);
  assert.match(html, /Training: 6 · Checking: 3/);
  assert.doesNotMatch(html, /Nog geen|dagmetingen/);
});
