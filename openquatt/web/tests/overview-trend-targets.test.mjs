import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  requestAnimationFrame: (callback) => callback(),
  localStorage: { getItem: () => null },
};

const { state } = await import("../js/src/core/state.js");
const { getOverviewLikeHydrationKeys } = await import("../js/src/core/entity-sync.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
const { handleViewAction } = await import("../js/src/features/view-actions.js");
const {
  getOverviewTrendCardsModel,
  getOverviewTrendSeriesCurrentValue,
  getOverviewTrendSeriesValue,
  getVisibleOverviewTrendSeries,
  patchOverviewTrendCurrentValues,
  parseOverviewTrendRow,
  renderOverviewTrendCard,
} = await import("../js/src/views/overview.js");

function entity(value) {
  return { value, state: String(value) };
}

test("diagnosis hydrates the entities needed for current trend values", () => {
  for (const options of [{ forceFast: true }, { includeBulk: true }]) {
    const keys = getOverviewLikeHydrationKeys("diagnosis", options);
    for (const key of ["strategyActiveCode", "strategySupplyTarget", "strategyRequestedPower", "phouseReq"]) {
      assert.ok(keys.includes(key), `${key} missing from ${JSON.stringify(options)}`);
    }
  }
});

test("new trend fields parse on the same timestamp and missing values stay missing", () => {
  const row = parseOverviewTrendRow("1000|8.1|32.4|19.5|20.0|500|700|2100|35.2|2800");
  assert.equal(row.t, 1000);
  assert.equal(row.supplyTarget, 35.2);
  assert.equal(row.phouseRequest, 2800);

  const unavailable = parseOverviewTrendRow("2000|8.1|32.4|19.5|20.0|500|700|2100|nan|nan");
  assert.ok(Number.isNaN(getOverviewTrendSeriesValue({ sampleKey: "supplyTarget" }, unavailable)));
  assert.ok(Number.isNaN(getOverviewTrendSeriesValue({ sampleKey: "phouseRequest" }, unavailable)));
  assert.ok(Number.isNaN(getOverviewTrendSeriesValue({ sampleKey: "supplyTarget" }, parseOverviewTrendRow("3000|8|32|19|20|500|700|2100"))));
});

test("new values remain available in every supported trend window", () => {
  const now = 1_800_000_000_000;
  state.trendHistoryNowMs = now;
  state.trendHistoryRaw = `${now - 60_000}|8.1|32.4|19.5|20.0|500|700|2100|35.2|2800`;
  for (const hours of [3, 12, 24, 72, 168, 336, 720]) {
    state.trendWindowHours = hours;
    const cards = getOverviewTrendCardsModel();
    assert.equal(cards.find((card) => card.id === "temperatures").samples[0].supplyTarget, 35.2);
    assert.equal(cards.find((card) => card.id === "power").samples[0].phouseRequest, 2800);
  }
  state.trendHistoryRaw = "";
  state.trendHistoryNowMs = Number.NaN;
});

test("current strategy values do not show a stale value from another strategy", () => {
  state.entities = {
    strategyActiveCode: entity(2),
    strategySupplyTarget: entity(35.2),
    phouseReq: entity(2800),
    strategyRequestedPower: entity(2800),
  };
  const temperatureCard = getOverviewTrendCardsModel().find((card) => card.id === "temperatures");
  const powerCard = getOverviewTrendCardsModel().find((card) => card.id === "power");
  const target = temperatureCard.series.find((series) => series.id === "supplyTarget");
  const request = powerCard.series.find((series) => series.id === "phouseRequest");

  assert.equal(getOverviewTrendSeriesCurrentValue(target, { supplyTarget: 34 }), 35.2);
  assert.ok(Number.isNaN(getOverviewTrendSeriesCurrentValue(request, { phouseRequest: 2800 })));
  state.entities.strategyActiveCode = entity(3);
  assert.equal(getOverviewTrendSeriesCurrentValue(request, { phouseRequest: 2800 }), 2800);
  assert.ok(Number.isNaN(getOverviewTrendSeriesCurrentValue(target, { supplyTarget: 34 })));
  delete state.entities.strategyRequestedPower;
  assert.ok(Number.isNaN(getOverviewTrendSeriesCurrentValue(request, { phouseRequest: 2800 })));
});

test("legend buttons hide a series from the chart while keeping it selectable", () => {
  const card = {
    id: "temperatures",
    tone: "orange",
    title: "Temperaturen",
    copy: "",
    windowHours: 24,
    samples: [],
    series: [
      { id: "outside", sampleKey: "outside", label: "Buiten", tone: "orange", decimals: 1, unit: " °C" },
      { id: "supplyTarget", sampleKey: "supplyTarget", label: "Stooklijn doel", pillLabel: "Doel", tone: "violet", decimals: 1, unit: " °C" },
    ],
  };
  state.trendHiddenSeries["temperatures:supplyTarget"] = true;
  assert.deepEqual(getVisibleOverviewTrendSeries(card).map((series) => series.id), ["outside"]);
  const markup = renderOverviewTrendCard(card);
  assert.match(markup, /data-trend-series="supplyTarget"[^>]*aria-pressed="false"/);
  assert.match(markup, /aria-label="Stooklijn doel: —"[^>]*>\s*<span>Doel<\/span>/);
  assert.doesNotMatch(markup, /data-oq-trend-hover-dot="supplyTarget"/);
  state.trendHiddenSeries = {};
});

test("legend toggle restores keyboard focus to the replacement button", () => {
  const previousDocument = globalThis.document;
  const previousRoot = state.root;
  const previousHiddenSeries = state.trendHiddenSeries;
  const button = { dataset: { trendCard: "temperatures", trendSeries: "supplyTarget" } };
  const focusCalls = [];
  const replacement = {
    dataset: button.dataset,
    focus: (options) => {
      focusCalls.push(options);
      globalThis.document.activeElement = replacement;
    },
  };
  try {
    globalThis.document = { activeElement: button };
    state.root = { querySelectorAll: () => [] };
    state.trendHiddenSeries = {};
    setRenderCallback(() => {
      state.root = { querySelectorAll: () => [replacement] };
      globalThis.document.activeElement = null;
    });

    assert.equal(handleViewAction("toggle-trend-series", button), true);
    assert.equal(state.trendHiddenSeries["temperatures:supplyTarget"], true);
    assert.equal(globalThis.document.activeElement, replacement);
    assert.deepEqual(focusCalls, [{ preventScroll: true }]);
  } finally {
    setRenderCallback(null);
    state.root = previousRoot;
    state.trendHiddenSeries = previousHiddenSeries;
    globalThis.document = previousDocument;
  }
});

test("live trend values also update the accessible button label", () => {
  state.entities = {
    strategyActiveCode: entity(3),
    strategyRequestedPower: entity(2800),
    phouseReq: entity(2800),
  };
  const request = getOverviewTrendCardsModel().find((card) => card.id === "power").series.find((series) => series.id === "phouseRequest");
  const valueElement = { textContent: "—" };
  const attributes = new Map([["aria-label", `${request.label}: —`]]);
  const pill = {
    querySelector: (selector) => selector === "strong" ? valueElement : null,
    getAttribute: (name) => attributes.get(name),
    setAttribute: (name, value) => attributes.set(name, value),
  };
  const root = {
    querySelector: (selector) => selector === '[data-oq-trend-card="power"]'
      ? { querySelector: (childSelector) => childSelector === '[data-oq-trend-current="phouseRequest"]' ? pill : null }
      : null,
  };

  patchOverviewTrendCurrentValues(root);
  assert.equal(attributes.get("aria-label"), `${request.label}: ${valueElement.textContent}`);
  const firstValue = valueElement.textContent;
  state.entities.phouseReq = entity(3100);
  patchOverviewTrendCurrentValues(root);
  assert.equal(attributes.get("aria-label"), `${request.label}: ${valueElement.textContent}`);
  assert.notEqual(valueElement.textContent, firstValue);
});
