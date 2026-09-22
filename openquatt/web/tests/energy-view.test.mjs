import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.localStorage = { getItem: () => null };

const { state } = await import("../js/src/core/state.js");
const {
  getEnergyHistoryBucketTooltip,
  getEnergyHistoryRecords,
  getEnergyHistoryRenderSignature,
  getEnergyHistorySummaryRecords,
  renderOverviewEnergyColumn,
} = await import("../js/src/views/energy.js");
const { OVERVIEW_ENERGY_COLUMN_CONFIGS } = await import("../js/src/core/config.js");
const { getEnergyViewEntityKeys } = await import("../js/src/core/entity-sync.js");

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

  assert.match(tooltip, /Elektrisch totaal: 4,9 kWh/);
  assert.match(tooltip, /Elektrisch koelen: 4,3 kWh/);
  assert.match(tooltip, /EER koelen: 4,94/);
  assert.doesNotMatch(tooltip, /Output \/ elektrisch/);
});

test("day summary uses the authoritative day record instead of partial hour buckets", () => {
  const dayRecord = { dateKey: 20260712, electricalInputWh: 4879, heatpumpCoolingOutputWh: 21025 };
  const hourBuckets = [{ dateKey: 20260712, electricalInputWh: 4400, heatpumpCoolingOutputWh: 19000 }];

  assert.deepEqual(getEnergyHistorySummaryRecords([dayRecord], hourBuckets, "day", "20260712"), [dayRecord]);
  assert.deepEqual(getEnergyHistorySummaryRecords([dayRecord], hourBuckets, "week", "20260706"), hourBuckets);
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
