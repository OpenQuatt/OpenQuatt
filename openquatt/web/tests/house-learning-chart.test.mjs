import assert from "node:assert/strict";
import test from "node:test";
import { getHouseLearningChartModel, normalizeHouseLearningExport, renderHouseLearningChart } from "../js/src/settings/house-learning-chart.js";
import { setLocale } from "../js/src/i18n/index.js";

const payload = {
  schema: 1,
  mode: "passive",
  record_columns: ["start_epoch_s", "end_epoch_s", "mean_room_c", "mean_setpoint_c", "mean_outside_c", "mean_heat_w", "room_trend_k_per_h", "context_revision"],
  records: [[1700000000, 1700014400, 20.1, 20.5, 5.2, 2200, 0.01, 4], [1700020000, 1700034400, 20.2, 20.5, 8.1, 1650, 0.01, 4]],
};

test.afterEach(() => setLocale("nl", { persist: false, applyDocument: false, notify: false }));

test("batchexport gebruikt alleen de firmwarekolommen en verwerpt ontbrekende waarden", () => {
  assert.deepEqual(normalizeHouseLearningExport(payload), [{ startEpoch: 1700000000, endEpoch: 1700014400, outsideC: 5.2, heatW: 2200, recordKind: 0 }, { startEpoch: 1700020000, endEpoch: 1700034400, outsideC: 8.1, heatW: 1650, recordKind: 0 }]);
  assert.deepEqual(normalizeHouseLearningExport({ ...payload, records: [[1700000000, 1700014400, 20.1, 20.5, null, 2200, 0.01, 4]] }), []);
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
  assert.match(markup, /Duur: 4 u 0 min/);
});

test("grafiektekst en getallen volgen de actieve locale", () => {
  const records = normalizeHouseLearningExport(payload);
  setLocale("nl", { persist: false, applyDocument: false, notify: false });
  const nl = renderHouseLearningChart(records, { coldC: -10, zeroC: 16, ratedW: 5200 }, { h: 186, t0: 16.8, ready: true });
  assert.match(nl, /Duur: 4 u 0 min/);
  assert.match(nl, /Buiten: 5,2 °C/);

  setLocale("en", { persist: false, applyDocument: false, notify: false });
  const en = renderHouseLearningChart(records, { coldC: -10, zeroC: 16, ratedW: 5200 }, { h: 186, t0: 16.8, ready: true });
  assert.match(en, /Duration: 4 hr 0 min/);
  assert.match(en, /Outside: 5\.2 °C/);
  assert.match(en, /Configured heat demand curve/);
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
  const withoutFit = renderHouseLearningChart(records, configured, {});
  assert.doesNotMatch(withoutFit, /<path[^>]+chart-line--learned|chart-swatch--learned/);
  assert.match(withoutFit, /chart-swatch--configured/);
  assert.match(withoutFit, /chart-swatch--records/);
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


test("lege grafiek verklaart waarom 30-minutenperioden geen meetpunten opleveren", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale, { persist: false, applyDocument: false, notify: false });
    for (const configured of [{}, { coldC: -10, zeroC: 16, ratedW: 5200 }]) {
      const markup = renderHouseLearningChart([], configured, {});
      assert.match(markup, locale === "nl" ? /dagmetingen van 24 uur/ : /24-hour daily measurements/);
      assert.match(markup, locale === "nl" ? /Opwarmen en afkoelen \(30 minuten\)/ : /Heating and cooling \(30 minutes\)/);
      assert.doesNotMatch(markup, /data-oq-house-learning-tip=|batch|chart-swatch--learned|chart-swatch--records/);
    }
  }
});


test("beschikbare woninglijnschatting blijft zichtbaar bij ontbrekende Power House-instellingen", () => {
  const markup = renderHouseLearningChart([], {}, { h: 186, t0: 16.8, ready: false });
  assert.match(markup, /<path[^>]+chart-line--learned-dashed/);
  assert.match(markup, /Power House-lijn is onvolledig/);
  assert.match(markup, /volledig een extrapolatie/);
  assert.match(markup, /voorlopig en nog niet gevalideerd/);
  assert.doesNotMatch(markup, /Nog geen grafiek beschikbaar|chart-swatch--configured|chart-swatch--records|De blauwe lijn toont/);
  assert.match(markup, /chart-swatch--learned/);
});


const dailyPayload = {
  schema: 1, mode: "passive", reference_room_c: 20,
  record_columns: [...payload.record_columns, "effective_outside_c", "record_kind"],
  records: [
    [1700000000, 1700014400, 20.1, 20.5, -6, 4400, 0.01, 4, -6.1, 0],
    [1700020000, 1700106400, 21, 21, 5, 2300, 0.1, 4, 4, 1],
    [1700106400, 1700192800, 19, 19.5, 10, 1600, -0.1, 4, 11, 1],
  ],
};

test("nieuwe export plot effectieve buitenwaarde, oude export behoudt werkelijke buitenwaarde", () => {
  const records = normalizeHouseLearningExport(dailyPayload);
  assert.equal(records[1].outsideC, 4);
  assert.equal(records[1].meanOutsideC, 5);
  assert.equal(records[1].recordKind, 1);
  const config = { coldC: -10, zeroC: 16, ratedW: 5200 };
  const model = getHouseLearningChartModel(records, config, {});
  const html = renderHouseLearningChart(records, config, { referenceRoomC: 20 });
  assert.match(html, new RegExp(`cx="${model.x(4).toFixed(1)}"`));
  assert.match(html, /Duur: 24 u 0 min/);
  assert.match(html, /Buiten: 5 °C/);
  assert.match(html, /omgerekend naar 20 °C in huis: 4 °C/);
  assert.match(html, /Oude 4-uursmeting/);
  assert.equal(normalizeHouseLearningExport(payload)[0].outsideC, 5.2);

  setLocale("en", { persist: false, applyDocument: false, notify: false });
  const en = renderHouseLearningChart(records, config, { referenceRoomC: 20 });
  assert.match(en, /Daily measurement \(24 hours\)/);
  assert.match(en, /Outside, converted to 20 °C indoors: 4 °C/);
  assert.match(en, /Old 4-hour measurement/);
});

test("legacy meetpunten blijven zichtbaar maar verbreden het dagfitbereik niet", () => {
  const records = normalizeHouseLearningExport(dailyPayload);
  const fit = { h: 200, t0: 16, referenceRoomC: 20 };
  const model = getHouseLearningChartModel(records, {}, fit);
  const html = renderHouseLearningChart(records, {}, fit);
  const range = html.match(/<rect x="([^"]+)"[^>]*width="([^"]+)"/);
  assert.ok(range);
  assert.equal(Number(range[1]), model.x(4));
  assert.equal(Number(range[2]), model.x(11) - model.x(4));
  assert.match(html, /1 oude 4-uursmetingen/);
  assert.match(html, /2 dagmetingen/);
  assert.equal((html.match(/data-oq-house-learning-tip=/g) || []).length, 3);

  const legacyOnly = renderHouseLearningChart(records.slice(0, 1), {}, fit);
  assert.doesNotMatch(legacyOnly, /<rect |class="oq-house-learning-chart-line oq-house-learning-chart-line--learned"/);
  assert.match(legacyOnly, /volledig een extrapolatie/);
});

test("nieuwe meetpunten met ongeldige effectieve buitenwaarde of onbekende soort worden niet verzonnen", () => {
  for (const value of [null, "4", NaN, Infinity]) {
    const row = [...dailyPayload.records[1]];
    row[8] = value;
    assert.deepEqual(normalizeHouseLearningExport({ ...dailyPayload, records: [row] }), []);
  }
  for (const kind of [null, "1", 2, -1]) {
    const row = [...dailyPayload.records[1]];
    row[9] = kind;
    assert.deepEqual(normalizeHouseLearningExport({ ...dailyPayload, records: [row] }), []);
  }
  const columns = dailyPayload.record_columns.filter((column) => column !== "effective_outside_c");
  const row = [...dailyPayload.records[1]];
  row.splice(8, 1);
  assert.deepEqual(normalizeHouseLearningExport({ ...dailyPayload, record_columns: columns, records: [row] }), []);
});


test("signed dagwarmte blijft zichtbaar onder nul; negatieve legacywarmte blijft ongeldig", () => {
  const negativeDay = [...dailyPayload.records[1]];
  negativeDay[5] = -12.5;
  const negativeLegacy = [...dailyPayload.records[0]];
  negativeLegacy[5] = -12.5;
  const records = normalizeHouseLearningExport({ ...dailyPayload, records: [negativeDay, negativeLegacy] });
  assert.equal(records.length, 1);
  assert.equal(records[0].heatW, -12.5);
  const model = getHouseLearningChartModel(records, { coldC: -10, zeroC: 16, ratedW: 5200 }, {});
  assert.ok(model.axisMinY < 0);
  assert.ok(model.y(-12.5) > model.y(0));
  assert.ok(model.y(-12.5) < model.y(model.axisMinY));
  assert.equal(model.minX, -10);
  assert.equal(model.maxX, 20);
  const html = renderHouseLearningChart(records, { coldC: -10, zeroC: 16, ratedW: 5200 }, {});
  assert.match(html, new RegExp(`cy="${model.y(-12.5).toFixed(1)}"`));
  assert.match(html, /Netto warmte: -13 W/);
  assert.equal((html.match(/data-oq-house-learning-tip=/g) || []).length, 1);
  assert.match(html, />-1<|>-2</);

  const oldRow = [...payload.records[0]];
  oldRow[5] = -12.5;
  assert.deepEqual(normalizeHouseLearningExport({ ...payload, records: [oldRow] }), []);
  const positive = getHouseLearningChartModel(normalizeHouseLearningExport(payload), {}, {});
  assert.equal(positive.axisMinY, 0);
});

test("grote signed waarden houden de y-as begrensd in aantal rasterlijnen", () => {
  const records = [{ startEpoch: 1, endEpoch: 86401, outsideC: 5, heatW: -25000, recordKind: 1 }];
  const model = getHouseLearningChartModel(records, {}, {});
  assert.ok((model.axisMaxY - model.axisMinY) / model.yStep <= 10);
  assert.ok(model.axisMinY <= -25000);
  assert.ok(Number.isFinite(model.y(-25000)));
});
