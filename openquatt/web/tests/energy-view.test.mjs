import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.localStorage = { getItem: () => null };

const { state } = await import("../js/src/core/state.js");
const {
  createEnergyHistoryBucket,
  getEnergyHistoryBucketTooltip,
  getEnergyHistoryHeatingInputWh,
  getEnergyHistoryEfficiencyStat,
  getEnergyHistoryPeriodControlModel,
  getEnergyHistoryRecords,
  getEnergyHistoryRenderSignature,
  getEnergyHistorySummary,
  getEnergyHistorySummaryRecords,
  mergeEnergyHistoryRecordIntoBucket,
  parseEnergyHistoryHourLine,
  renderOverviewEnergyColumn,
  renderEnergyHistoryPeriodControl,
} = await import("../js/src/views/energy.js");
const { getLocale, setLocale } = await import("../js/src/i18n/index.js");
const { OVERVIEW_ENERGY_COLUMN_CONFIGS } = await import("../js/src/core/config.js");
const { getEnergyViewEntityKeys } = await import("../js/src/core/entity-sync.js");

test("next period is disabled at the latest period and enabled for earlier periods", () => {
  const previousSelection = state.energyHistoryPeriodSelection;
  const previousLocale = getLocale();
  const records = [{ dateKey: 20240101 }, { dateKey: 20261011 }];

  try {
    for (const locale of ["nl", "en"]) {
      setLocale(locale, { persist: false, notify: false });
      for (const view of ["day", "week", "month", "year"]) {
        state.energyHistoryPeriodSelection = {};
        const latest = getEnergyHistoryPeriodControlModel(records, view);
        const selections = [latest.maxValue, latest.minValue, latest.maxValue];
        for (const selectedValue of selections) {
          state.energyHistoryPeriodSelection = { [view]: selectedValue };
          const model = getEnergyHistoryPeriodControlModel(records, view);
          const markup = renderEnergyHistoryPeriodControl(model);
          const nextButton = markup.match(/<button\b[^>]*data-energy-history-direction="1"[^>]*>/)?.[0];
          assert.ok(nextButton, `${locale}/${view}: next button exists`);
          assert.equal(/\bdisabled\b/.test(nextButton), selectedValue === latest.maxValue,
            `${locale}/${view}/${selectedValue}: next button state`);
        }
      }
    }
  } finally {
    state.energyHistoryPeriodSelection = previousSelection;
    setLocale(previousLocale, { persist: false, notify: false });
  }
});

test("Cumulatief bevat de energieteller-reset en hydrateert de knop", () => {
  const previousEntities = state.entities;
  state.entities = {
    heatingElectricalEnergyCumulative: { value: 469.5, state: "469.5", uom: "kWh" },
    resetCumulativeEnergyCounters: { value: "", state: "" },
  };

  try {
    const cumulativeColumn = OVERVIEW_ENERGY_COLUMN_CONFIGS.find((column) => column.labelKey === "energy.colCumulative");
    const markup = renderOverviewEnergyColumn(cumulativeColumn);
    assert.match(markup, /data-oq-action="open-energy-counter-reset-confirm"/);
    assert.match(markup, /Tellers resetten/);
    assert.ok(getEnergyViewEntityKeys().includes("resetCumulativeEnergyCounters"));
  } finally {
    state.entities = previousEntities;
  }
});

test("energy history tooltip exposes the inputs used by COP and EER", () => {
  const tooltip = getEnergyHistoryBucketTooltip({
    label: "Zo",
    dateKey: 20260712,
    electricalInputWh: 4879,
    heatingInputWh: 0,
    coolingInputWh: 4252,
    heatpumpHeatOutputWh: 0,
    heatpumpCoolingOutputWh: 21025,
    boilerHeatOutputWh: 0,
  });

  assert.doesNotMatch(tooltip, /Elektrisch totaal|Elektrisch voor COP/);
  assert.match(tooltip, /Elektrisch verwarmen: 627 Wh/);
  assert.match(tooltip, /Elektrisch koelen: 4,3 kWh/);
  assert.match(tooltip, /EER koelen: 4,94/);
  assert.doesNotMatch(tooltip, /Output \/ elektrisch/);
});

test("stored heating COP includes pump and standby electricity in both locales", () => {
  const previousLocale = getLocale();
  const record = parseEnergyHistoryHourLine("@hour|1|20261019|6|310|275|0|2320|0|0|2320");
  const original = { ...record };
  try {
    for (const locale of ["nl", "en"]) {
      setLocale(locale, { persist: false, notify: false });
      const tooltip = getEnergyHistoryBucketTooltip(record);
      const summary = getEnergyHistorySummary([record]);
      assert.equal(getEnergyHistoryHeatingInputWh(record), 310);
      assert.equal(summary.heatingInputWh, 310);
      assert.equal(getEnergyHistoryEfficiencyStat(summary).value, locale === "nl" ? "7,48" : "7.48");
      assert.match(tooltip, locale === "nl"
        ? /Elektrisch verwarmen: 310 Wh/
        : /Electricity used for heating: 310 Wh/);
      assert.match(tooltip, locale === "nl" ? /COP verwarmen: 7,48/ : /COP heating: 7.48/);
    }
    assert.deepEqual(record, original);
  } finally {
    setLocale(previousLocale, { persist: false, notify: false });
  }
});

test("mixed periods exclude cooling input and weight COP by all remaining electricity", () => {
  const records = [
    { electricalInputWh: 310, heatingInputWh: 275, coolingInputWh: 0, heatpumpHeatOutputWh: 2320 },
    { electricalInputWh: 1035, heatingInputWh: 0, coolingInputWh: 1000, heatpumpHeatOutputWh: 0, heatpumpCoolingOutputWh: 4000 },
    { electricalInputWh: 35, heatingInputWh: 0, coolingInputWh: 0, heatpumpHeatOutputWh: 0 },
  ];
  const summary = getEnergyHistorySummary(records);
  assert.equal(summary.heatingInputWh, 380);
  assert.deepEqual(getEnergyHistoryEfficiencyStat(summary), { label: "COP / EER", value: "6,11 / 4,00" });
  const bucket = createEnergyHistoryBucket({ dateKey: 20261019 });
  records.forEach((record) => mergeEnergyHistoryRecordIntoBucket(bucket, record));
  assert.equal(getEnergyHistoryHeatingInputWh(bucket), 380);
  assert.match(getEnergyHistoryBucketTooltip(bucket), /COP verwarmen: 6,11/);
  assert.equal(getEnergyHistorySummary([bucket]).heatingInputWh, 380);
});

test("missing or inconsistent COP input remains unavailable after aggregation", () => {
  for (const record of [
    { electricalInputWh: 310 },
    { electricalInputWh: null, coolingInputWh: 0 },
    { electricalInputWh: 310, coolingInputWh: null },
    { electricalInputWh: 310, coolingInputWh: NaN },
    { electricalInputWh: Infinity, coolingInputWh: 0 },
    { electricalInputWh: -1, coolingInputWh: 0 },
    { electricalInputWh: 310, coolingInputWh: -1 },
    { electricalInputWh: 310, coolingInputWh: 311 },
  ]) {
    assert.ok(Number.isNaN(getEnergyHistoryHeatingInputWh(record)));
    const bucket = createEnergyHistoryBucket({ dateKey: 20261019 });
    mergeEnergyHistoryRecordIntoBucket(bucket, { ...record, heatpumpHeatOutputWh: 2320 });
    // A later valid record must not conceal a missing or negative denominator.
    mergeEnergyHistoryRecordIntoBucket(bucket, { electricalInputWh: 1000, coolingInputWh: 0 });
    assert.ok(Number.isNaN(getEnergyHistoryHeatingInputWh(bucket)));
    assert.equal(getEnergyHistoryEfficiencyStat(getEnergyHistorySummary([bucket])).value, "—");
  }
  const coolingOnly = { electricalInputWh: 1000, coolingInputWh: 1000, heatpumpHeatOutputWh: 0 };
  assert.equal(getEnergyHistoryHeatingInputWh(coolingOnly), 0);
  assert.equal(getEnergyHistoryEfficiencyStat(getEnergyHistorySummary([coolingOnly])).value, "—");
});

test("day summary uses the authoritative day record instead of partial hour buckets", () => {
  const dayRecord = { dateKey: 20260712, electricalInputWh: 4879, heatpumpCoolingOutputWh: 21025 };
  const hourBuckets = [{ dateKey: 20260712, electricalInputWh: 4400, heatpumpCoolingOutputWh: 19000 }];

  assert.deepEqual(getEnergyHistorySummaryRecords([dayRecord], hourBuckets, "day", "20260712"), [dayRecord]);
  assert.deepEqual(getEnergyHistorySummaryRecords([dayRecord], hourBuckets, "week", "20260706"), hourBuckets);
});

test("unknown cooling electricity cannot inflate the EER of a mixed history period", () => {
  const records = [
    { electricalInputWh: 1000, coolingInputWh: null, heatpumpCoolingOutputWh: 4000 },
    { electricalInputWh: 1000, coolingInputWh: 1000, heatpumpCoolingOutputWh: 4000 },
  ];
  const bucket = createEnergyHistoryBucket({ dateKey: 20261019 });
  records.forEach((record) => mergeEnergyHistoryRecordIntoBucket(bucket, record));
  assert.ok(Number.isNaN(bucket.coolingInputWh));
  assert.equal(getEnergyHistoryEfficiencyStat(getEnergyHistorySummary(records)).value, "—");
  assert.equal(getEnergyHistoryEfficiencyStat(getEnergyHistorySummary([bucket])).value, "—");
  assert.match(getEnergyHistoryBucketTooltip(bucket), /Elektrisch koelen: —/);
});

test("current history record stays authoritative while live energy entities load", () => {
  const previousEntities = state.entities;
  const previousRaw = state.energyHistoryRaw;
  state.entities = {
    electricalEnergyDaily: { value: "" },
    coolingElectricalEnergyDaily: { value: 5.052 },
    heatpumpCoolingEnergyDaily: { value: 25.314 },
  };
  state.energyHistoryRaw = "@current|20260714|5131|0|5048|0|25289|0|0";

  try {
    const record = getEnergyHistoryRecords().find((item) => item.dateKey === 20260714);
    assert.equal(record?.electricalInputWh, 5131);
    assert.equal(record?.coolingInputWh, 5048);
    assert.equal(record?.heatpumpCoolingOutputWh, 25289);
  } finally {
    state.entities = previousEntities;
    state.energyHistoryRaw = previousRaw;
  }
});

test("energy history render signature tracks day summary changes", () => {
  const model = {
    activeView: "day",
    periodControl: { view: "day", selectedValue: "20260714", minValue: "20260714", maxValue: "20260714" },
    records: [{ dateKey: 20260714 }],
    buckets: [{ dateKey: 20260714, electricalInputWh: 5131 }],
    summary: { electricalInputWh: 0, coolingOutputWh: 25289 },
  };
  const before = getEnergyHistoryRenderSignature(model);
  const after = getEnergyHistoryRenderSignature({
    ...model,
    summary: { ...model.summary, electricalInputWh: 5131 },
  });

  assert.notEqual(before, after);
});
