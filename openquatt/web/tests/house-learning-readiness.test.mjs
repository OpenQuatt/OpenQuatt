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
  assert.match(html, /Leerdagen<\/dt><dd>9 <span>\/ 6 nodig/);
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
  assert.match(html, /Leerdagen<\/dt><dd>13 <span>\/ 6 nodig/);
  assert.match(html, /Controledagen<\/dt><dd>3 <span>\/ 3 nodig/);
  assert.match(html, /3,25 °C <span>\/ 6 °C nodig/);
  assert.doesNotMatch(html, /Nog [1-9]+ (?:volledige )?dagmetingen nodig/);
  assert.doesNotMatch(html, /Geleerde woninglijn beschikbaar/);
  setLocale("en", config);
  const en = renderHouseLearningChart(data, {}, {});
  assert.match(en, /Training days<\/dt><dd>13 <span>\/ 6 needed/);
  assert.match(en, /Checking days<\/dt><dd>3 <span>\/ 3 needed/);
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
  assert.match(renderHouseLearningReadiness(data, { h: 200, t0: 16 }), /Voorlopige woninglijn/);
  assert.match(renderHouseLearningReadiness(data, { h: 200, t0: 16, ready: true }), /Geleerde woninglijn beschikbaar/);
  setLocale("en", config);
  const html = renderHouseLearningReadiness(records([8, 8.2, 9, 9.2, 10, 10.2, 8, 9, 10]));
  assert.match(html, /2\.2 °C <span>\/ 6 °C needed/);
  assert.match(html, /3\.8 °C more spread needed/);
  assert.match(html, /Training days<\/dt><dd>6 <span>\/ 6 needed/);
  assert.doesNotMatch(html, /Nog geen|dagmetingen/);
});


test("historische onbekende kwaliteit levert geen fictieve controledagen of groene criteria", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale, config);
    for (const known of [0, 2, 5]) {
      const data = records([4, 4, 7, 7, 10, 10, -10, 18, 20]).map((record, index) => ({
        ...record, quality: {valid: true, heatCoverageBp:index<known ? 10000:null,
          temperatureCoverageBp:index<known ? 10000:null, heatUncertaintyMeanW:index<known ? 0:null,
          fitEligible:true,fitWeightBp:index<known ? 10000:5000},
      }));
      const html = renderHouseLearningChart(data, {}, {h:200,t0:16,referenceRoomC:20,batchStatus:"insufficient_data",ready:false});
      assert.match(html, new RegExp(`${locale === "nl" ? "Dagmetingen met bekende meetkwaliteit" : "Daily measurements with known quality"}: ${known}`));
      assert.doesNotMatch(html, /is-complete|✓|Leren: 6|Checking: 3/);
      assert.match(html, /9/); // Retained eligible measurements remain visible in the chart summary.
      assert.doesNotMatch(html, /undefined|NaN/);
    }
  }
});


test("legacykwaliteit behoudt de concrete afwijzing van de controller", () => {
  const data=records([4,4,7,7,10,10,-10,18,20]).map(record=>({...record,quality:{valid:true,
    heatCoverageBp:null,temperatureCoverageBp:null,heatUncertaintyMeanW:null,fitEligible:true,fitWeightBp:5000}}));
  for(const locale of ["nl","en"]) {
    setLocale(locale,config);
    const html=renderHouseLearningReadiness(data,{batchStatus:"invalid_active_model"});
    assert.match(html, locale==="nl" ? /De ingestelde woninglijn is ongeldig/ : /The configured heat demand curve is invalid/);
    assert.doesNotMatch(html,/is-complete|✓|Leren: 6|Checking: 3/);
  }
});

test("firmwaresplit toont de spreiding van leerdagen ondanks drie koude controledagen", () => {
  const data = records([...Array.from({ length: 18 }, (_, i) => 8 + (i % 7) * 0.6), 5.2, 3.15, 1.97])
    .map((record, index) => ({ ...record, quality: {
      valid: true, heatCoverageBp: index < 18 ? null : 10000,
      temperatureCoverageBp: index < 18 ? null : 10000, heatUncertaintyMeanW: index < 18 ? null : 0,
      fitEligible: true, fitWeightBp: index < 18 ? 5000 : 10000,
    } }));
  const learned = { h: 213, t0: 17.4, ready: false, batchStatus: "insufficient_spread",
    fit: { trainCount: 18, checkCount: 3, trainDays: 18, checkDays: 3,
      p10: 8.28, p90: 11.88, minC: 7.55, maxC: 12.34 } };
  const nl = renderHouseLearningChart(data, {}, learned);
  assert.match(nl, /Voorlopige woninglijn/);
  assert.match(nl, /Leerdagen<\/dt><dd>18 <span>\/ 6 nodig/);
  assert.match(nl, /Controledagen<\/dt><dd>3 <span>\/ 3 nodig/);
  assert.match(nl, /3,6 °C <span>\/ 6 °C nodig/);
  assert.match(nl, /Nog 2,4 °C spreiding nodig/);
  assert.match(nl, /Deze schatting stuurt de regeling niet aan/);
  assert.doesNotMatch(nl, /Geleerde woninglijn beschikbaar/);
  assert.equal((nl.match(/is-complete/g) || []).length, 2);
  setLocale("en", config);
  const en = renderHouseLearningChart(data, {}, learned);
  assert.match(en, /Training days<\/dt><dd>18 <span>\/ 6 needed/);
  assert.match(en, /3\.6 °C <span>\/ 6 °C needed/);
  assert.match(en, /2\.4 °C more spread needed/);
  assert.doesNotMatch(en, /undefined|NaN/);
});

test("voorlopige legacyfit benoemt ontbrekende onafhankelijke controledagen", () => {
  const data = records([4, 4, 7, 7, 10, 10]);
  const html = renderHouseLearningReadiness(data, { h: 200, t0: 16, ready: false,
    batchStatus: "insufficient_data", fit: {
      trainCount: 6, checkCount: 0, trainDays: 6, checkDays: 0, p10: 4, p90: 10, minC: 4, maxC: 10,
    } });
  assert.match(html, /Controledagen<\/dt><dd>0 <span>\/ 3 nodig/);
  assert.match(html, /Nog 3 controledagen nodig/);
  assert.doesNotMatch(html, /Geleerde woninglijn beschikbaar/);
});

test("twintig leerdagen zonder controledagen hebben elk een eigen teller en balk", () => {
  const data = records(Array.from({ length: 20 }, (_, index) => 4.4 + index * 0.4));
  const learned = { ready: false, batchStatus: "insufficient_data",
    fit: { trainCount: 20, checkCount: 0, trainDays: 20, checkDays: 0,
      p10: 4.96, p90: 11.84, minC: 4.4, maxC: 12.3 },
    preview: { h: 186.4, t0: 16.8, count: 20, days: 20, minC: 4.4, maxC: 12.3 } };
  const before = JSON.stringify({ data, learned });
  for (const locale of ["nl", "en"]) {
    setLocale(locale, config);
    const html = renderHouseLearningReadiness(data, learned);
    assert.match(html, locale === "nl"
      ? /Leerdagen<\/dt><dd>20 <span>\/ 6 nodig/
      : /Training days<\/dt><dd>20 <span>\/ 6 needed/);
    assert.match(html, locale === "nl"
      ? /Controledagen<\/dt><dd>0 <span>\/ 3 nodig/
      : /Checking days<\/dt><dd>0 <span>\/ 3 needed/);
    assert.match(html, locale === "nl" ? /Nog 3 controledagen nodig/ : /3 more checking days needed/);
    assert.match(html, locale === "nl" ? /6,88 °C/ : /6\.88 °C/);
    assert.equal((html.match(/is-complete/g) || []).length, 2);
    assert.doesNotMatch(html, /20 <span>\/ 9|Nog 3 dagmetingen nodig|undefined|NaN/);
    assert.match(html, locale === "nl" ? /stuurt de regeling niet aan/ : /does not control the heating/);
    assert.equal(JSON.stringify({ data, learned }), before);
  }
});

test("één ontbrekende controledag blijft enkelvoud", () => {
  const html = renderHouseLearningReadiness(records([4, 4, 7, 7, 10, 10, 7, 10]), {
    batchStatus: "insufficient_data", fit: { trainCount: 6, checkCount: 2, trainDays: 6, checkDays: 2,
      p10: 4, p90: 10, minC: 4, maxC: 10 } });
  assert.match(html, /Nog 1 controledag nodig/);
  assert.doesNotMatch(html, /Nog 1 controledagen/);
});

test("onvoldoende trainingsdagen gaan vóór een tekort aan controledagen", () => {
  const html = renderHouseLearningReadiness(records([4, 10]), { batchStatus: "insufficient_data", fit: {
    trainCount: 0, checkCount: 2, trainDays: 0, checkDays: 2,
    p10: null, p90: null, minC: null, maxC: null,
  } });
  assert.match(html, /Nog 7 volledige dagmetingen nodig/);
  assert.doesNotMatch(html, /Nog 1 bruikbare controledag/);
});

test("vijf voorbereide leerdagen tonen hun echte temperatuurspreiding", () => {
  const html = renderHouseLearningReadiness(records([4, 5, 7, 9, 10, 4, 7, 10]), {
    batchStatus: "insufficient_data", fit: {
      trainCount: 5, checkCount: 3, trainDays: 5, checkDays: 3,
      p10: 4, p90: 10, minC: 4, maxC: 10,
    },
  });
  assert.match(html, /Leerdagen<\/dt><dd>5 <span>\/ 6 nodig/);
  assert.match(html, /6 °C <span>\/ 6 °C nodig/);
  assert.doesNotMatch(html, /Nog geen trainingsdagen/);
  assert.match(html, /Nog 1 volledige dagmeting nodig/);
  assert.doesNotMatch(html, /Nog 1 (?:volledige )?dagmetingen/);
});

test("bewaarde dagen met onbekende kwaliteit tonen indicatieve balken zonder goedkeuring", () => {
  setLocale("nl", config);
  const data = records([4, 6, 8, 10]).map(record => ({ ...record, quality: { valid: true,
    heatCoverageBp: null, temperatureCoverageBp: null, heatUncertaintyMeanW: null, fitEligible: true, fitWeightBp: 5000 } }));
  const html = renderHouseLearningReadiness([], {}, data);
  assert.match(html, /Opgeslagen dagmetingen[\s\S]*4 <span>\/ 9 als minimum/);
  assert.match(html, /Spreiding van alle opgeslagen dagen[\s\S]*6 °C <span>\/ 6 °C richtwaarde/);
  assert.doesNotMatch(html, /is-complete|✓/);
  assert.match(renderHouseLearningReadiness([], {}, data.slice(0, 1)), /— <span>\/ 6 °C richtwaarde/);
});
