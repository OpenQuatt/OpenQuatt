import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { runInNewContext } from "node:vm";

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false };
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
  localStorage: { getItem: () => null },
};

const { state } = await import("../js/src/core/state.js");
const {
  HOUSE_LEARNING_STATUS_INTERVAL_MS,
  downloadHouseLearningExport,
  isHouseLearningResetConfirmed,
  loadHouseLearningChart,
  normalizeHouseLearningStatus,
  refreshHouseLearningStatus,
  resetHouseLearningData,
  shouldRefreshHouseLearningStatusSurface,
} = await import("../js/src/features/house-learning.js");
const { patchHouseLearningSettingsStatus, renderHouseLearningSettings, renderHouseLearningStatusMarkup } = await import("../js/src/settings/house-learning.js");
const { SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");

function switchEntity(value = false) {
  return { value, state: value };
}

test.afterEach(() => {
  document.hidden = false;
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.houseLearningStatus = null;
  state.houseLearningEndpointAvailable = false;
  state.houseLearningEndpointChecked = false;
  state.houseLearningEndpointChecking = false;
  state.houseLearningStatusError = "";
  state.houseLearningLastFetchAt = 0;
  state.houseLearningFetchPromise = null;
  state.houseLearningRequestId = 0;
  state.houseLearningChart = null;
  state.houseLearningChartLoading = false;
  state.houseLearningChartError = "";
  state.houseLearningChartFetchedAt = 0;
  state.houseLearningChartRequestId = 0;
  state.houseLearningReset = "";
  state.houseLearningResetError = "";
  state.root = null;
  state.focusedField = null;
  setRenderCallback(null);
});

function statusPayload(overrides = {}) {
  return {
    schema: 1,
    mode: "passive",
    enabled: false,
    control_mode: 0,
    storage_ready: true,
    status: "paused",
    source_status: "blocked",
    model_validation_status: "batch_model_unavailable",
    journal_status: "ready",
    invalid_reasons: ["essential_source"],
    records: 42,
    batch_status: "collecting",
    batch_advice_ready: false,
    advice_ready: false,
    auto_apply_allowed: false,
    h_batch: null,
    t0_batch: null,
    u_rls: "NaN",
    c_rls_wh_per_k: null,
    rls_samples: 18,
    rls_ready: false,
    rls_readiness_reasons: ["not_enough_samples"],
    tick_epoch: Math.floor(Date.now() / 1000),
    blocked_reasons: [],
    collection: {
      batch_active: true,
      batch_elapsed_s: 125,
      batch_target_s: 300,
      thermal_active: false,
      thermal_elapsed_s: 0,
      thermal_target_s: 900,
      thermal_intervals: 0,
    },
    sources: {
      room: { route: "selected_room_temperature", valid: true },
      setpoint: { route: "selected_room_setpoint", valid: true },
      outside: { route: "selected_outside_temperature", valid: true },
      flow: { route: "selected_flow", valid: false },
    },
    memory: {},
    ...overrides,
  };
}

test("passieve leerstatus normaliseert onbeschikbare getallen naar null", () => {
  const status = normalizeHouseLearningStatus(statusPayload());
  assert.equal(status.hBatch, null);
  assert.equal(status.uRls, null);
  assert.equal(status.records, 42);
  assert.equal(status.sources.flow.valid, false);
  assert.deepEqual(status.collection.batch, { active: true, elapsedSeconds: 125, targetSeconds: 300, intervals: null });
  assert.equal(normalizeHouseLearningStatus(statusPayload({ collection: undefined })).collection, null);
  assert.throws(() => normalizeHouseLearningStatus({ schema: 2, mode: "passive" }), /statusformaat/);
});

test("leerstatus wordt alleen op het zichtbare Power House instellingenscherm opgehaald", async () => {
  state.appView = "settings";
  state.settingsGroup = "heating";
  state.entities = { houseLearningEnabled: switchEntity(false) };
  state.houseLearningLastFetchAt = 0;
  state.houseLearningFetchPromise = null;
  globalThis.fetch = async () => ({ ok: true, status: 200, json: async () => statusPayload() });

  assert.equal(HOUSE_LEARNING_STATUS_INTERVAL_MS, 10000);
  assert.equal(await refreshHouseLearningStatus(), true);
  assert.equal(state.houseLearningEndpointAvailable, true);

  state.entities.strategy = { value: "Water Temperature Control" };
  let fetches = 0;
  globalThis.fetch = async () => {
    fetches += 1;
    return { ok: true, status: 200, json: async () => statusPayload() };
  };
  assert.equal(shouldRefreshHouseLearningStatusSurface(), false);
  assert.equal(await refreshHouseLearningStatus({ force: true }), false);
  assert.equal(fetches, 0);

  state.entities.strategy = { value: "Power House" };
  state.appView = "overview";
  assert.equal(await refreshHouseLearningStatus({ force: true }), false);
  assert.equal(fetches, 0);
});

test("directe heating-entry vervangt de eerste capabilitycheck na de statusrespons", async () => {
  state.appView = "settings";
  state.settingsGroup = "heating";
  state.entities = { strategy: { value: "Power House" }, houseLearningEnabled: switchEntity(false) };
  let respond;
  globalThis.fetch = () => new Promise((resolve) => { respond = resolve; });

  const refresh = refreshHouseLearningStatus({ force: true });
  assert.match(renderHouseLearningSettings(), /Beschikbaarheid van passief leren wordt gecontroleerd/);
  respond({ ok: false, status: 404, json: async () => ({}) });
  assert.equal(await refresh, true);
  assert.equal(renderHouseLearningSettings(), "");
});

test("directe heating-entry vervangt de laadstatus ook na een geslaagde respons", async () => {
  state.appView = "settings";
  state.settingsGroup = "heating";
  state.entities = { strategy: { value: "Power House" }, houseLearningEnabled: switchEntity(true) };
  let rendered = "";
  let renders = 0;
  const statusNode = { innerHTML: "" };
  const panel = { removed: false, remove() { this.removed = true; }, querySelector: (selector) => selector === "[data-oq-house-learning-status]" ? statusNode : null };
  state.root = { querySelector: () => panel };
  setRenderCallback(() => { renders += 1; rendered = renderHouseLearningSettings(); });
  let respond;
  globalThis.fetch = () => new Promise((resolve) => { respond = resolve; });

  const refresh = refreshHouseLearningStatus({ force: true });
  assert.match(rendered, /Beschikbaarheid van passief leren wordt gecontroleerd/);
  assert.equal(patchHouseLearningSettingsStatus(), true);
  assert.equal(panel.removed, false);
  assert.match(statusNode.innerHTML, /Beschikbaarheid van passief leren wordt gecontroleerd/);
  respond({ ok: true, status: 200, json: async () => statusPayload({ enabled: true, control_mode: 2, status: "collecting" }) });

  assert.equal(await refresh, true);
  assert.match(rendered, /Verzamelt nu/);
  assert.equal(renders, 2);
  globalThis.fetch = async () => ({ ok: true, status: 200, json: async () => statusPayload({ enabled: true, control_mode: 2, status: "collecting", records: 43 }) });
  assert.equal(await refreshHouseLearningStatus({ force: true }), true);
  assert.equal(renders, 2, "normale statusupdates bouwen het instellingenscherm niet opnieuw op");
});

test("een herhaalde 404-probe toont geen laadblok opnieuw", async () => {
  state.appView = "settings";
  state.settingsGroup = "heating";
  state.entities = { strategy: { value: "Power House" }, houseLearningEnabled: switchEntity(false) };
  const responses = [];
  globalThis.fetch = () => new Promise((resolve) => responses.push(resolve));

  const firstProbe = refreshHouseLearningStatus({ force: true });
  responses.shift()({ ok: false, status: 404, json: async () => ({}) });
  await firstProbe;
  assert.equal(renderHouseLearningSettings(), "");

  const panel = {
    remove() { this.removed = true; },
    querySelector: () => ({ innerHTML: "" }),
  };
  state.root = { querySelector: () => panel };
  const retry = refreshHouseLearningStatus({ force: true });

  assert.equal(renderHouseLearningSettings(), "");
  assert.equal(patchHouseLearningSettingsStatus(), true);
  assert.equal(panel.removed, true);
  responses.shift()({ ok: false, status: 404, json: async () => ({}) });
  assert.equal(await retry, false);
});

test("een late statusrespons na navigatie bevestigt de capability niet", async () => {
  state.appView = "settings";
  state.settingsGroup = "heating";
  state.entities = { strategy: { value: "Power House" }, houseLearningEnabled: switchEntity(false) };
  let respond;
  globalThis.fetch = () => new Promise((resolve) => { respond = resolve; });

  const refresh = refreshHouseLearningStatus({ force: true });
  state.appView = "overview";
  respond({ ok: true, status: 200, json: async () => statusPayload() });

  assert.equal(await refresh, false);
  assert.equal(state.houseLearningEndpointChecked, false);
  assert.equal(state.houseLearningEndpointChecking, false);
});

test("annuleren verstuurt geen reset en een geweigerde reset blijft als fout zichtbaar", async () => {
  document.hidden = false;
  state.appView = "settings";
  state.settingsGroup = "heating";
  state.entities = { strategy: { value: "Power House" }, houseLearningEnabled: switchEntity(true) };
  state.busyAction = "";
  state.controlError = "";
  state.houseLearningReset = "";
  state.houseLearningResetError = "";
  let presses = 0;
  const press = async () => { presses += 1; };

  assert.equal(await resetHouseLearningData(press, { confirmReset: () => false, pollDelays: [0] }), false);
  assert.equal(presses, 0);
  assert.equal(state.houseLearningReset, "");

  const failedPress = async () => {
    presses += 1;
    state.controlError = "Actie mislukt voor reset. HTTP 500";
  };
  assert.equal(await resetHouseLearningData(failedPress, { confirmReset: () => true, pollDelays: [0] }), false);
  assert.equal(presses, 1);
  assert.equal(state.houseLearningReset, "error");
  assert.match(renderHouseLearningStatusMarkup(), /HTTP 500/);
});

test("reset negeert een oude status en meldt pas succes na leeg, gepauzeerd en gewist journal", async () => {
  document.hidden = false;
  state.appView = "settings";
  state.settingsGroup = "heating";
  state.entities = { strategy: { value: "Power House" }, houseLearningEnabled: switchEntity(true) };
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.houseLearningReset = "";
  state.houseLearningResetError = "";
  state.houseLearningRequestId = 0;
  state.houseLearningLastFetchAt = 0;
  state.houseLearningFetchPromise = null;

  let releaseOldResponse;
  let fetches = 0;
  globalThis.fetch = async () => {
    fetches += 1;
    if (fetches === 1) return new Promise((resolve) => { releaseOldResponse = resolve; });
    return {
      ok: true,
      status: 200,
      json: async () => statusPayload({ enabled: false, records: 0, journal_status: "cleared" }),
    };
  };
  const oldRequest = refreshHouseLearningStatus({ force: true });
  const reset = resetHouseLearningData(async () => { state.controlError = ""; }, {
    confirmReset: () => true,
    pollDelays: [0],
  });
  releaseOldResponse({
    ok: true,
    status: 200,
    json: async () => statusPayload({ enabled: true, records: 99, journal_status: "ready" }),
  });

  assert.equal(await oldRequest, false);
  assert.equal(await reset, true);
  assert.equal(fetches, 2);
  assert.equal(isHouseLearningResetConfirmed(), true);
  assert.equal(isHouseLearningResetConfirmed({ ...state.houseLearningStatus, status: "reset_pending" }), false);
  assert.equal(state.houseLearningStatus.records, 0);
  assert.match(state.controlNotice, /gewist.*gepauzeerd/);
});

test("reset wist een lopende grafiekexport en laat de laadtoestand niet hangen", async () => {
  state.appView = "settings";
  state.settingsGroup = "heating";
  state.entities = { strategy: { value: "Power House" }, houseLearningEnabled: switchEntity(true) };
  let releaseExport;
  let requests = 0;
  globalThis.fetch = async () => {
    requests += 1;
    if (requests === 1) return new Promise((resolve) => { releaseExport = resolve; });
    return { ok: true, status: 200, json: async () => statusPayload({ enabled: false, records: 0, journal_status: "cleared" }) };
  };
  const load = loadHouseLearningChart();
  assert.equal(state.houseLearningChartLoading, true);
  assert.equal(await resetHouseLearningData(async () => { state.controlError = ""; }, { confirmReset: () => true, pollDelays: [0] }), true);
  assert.equal(state.houseLearningChartLoading, false);
  assert.equal(state.houseLearningChart, null);
  releaseExport({ ok: true, status: 200, json: async () => ({ schema: 1, mode: "passive", record_columns: ["start_epoch_s", "end_epoch_s", "mean_outside_c", "mean_heat_w"], records: [[1, 2, 5, 1000]] }) });
  assert.equal(await load, false);
  assert.equal(state.houseLearningChart, null);
});

test("geaccepteerde maar onbevestigde reset blijft onzeker en wordt niet opnieuw verstuurd", async () => {
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.houseLearningReset = "";
  state.houseLearningResetError = "";
  state.houseLearningFetchPromise = null;
  state.houseLearningLastFetchAt = 0;
  let presses = 0;
  globalThis.fetch = async () => ({
    ok: true,
    status: 200,
    json: async () => statusPayload({ enabled: false, records: 7, journal_status: "reset_pending" }),
  });

  assert.equal(await resetHouseLearningData(async () => {
    presses += 1;
    state.controlError = "";
  }, { confirmReset: () => true, pollDelays: [0, 0] }), false);
  assert.equal(presses, 1);
  assert.equal(state.houseLearningReset, "uncertain");
  assert.match(renderHouseLearningStatusMarkup(), /nog niet bevestigd/);
  assert.equal(state.controlNotice, "");
});

test("endpointfouten blijven zichtbaar en een late JSON-body wordt genegeerd", async () => {
  document.hidden = false;
  state.appView = "settings";
  state.settingsGroup = "heating";
  state.entities = { houseLearningEnabled: switchEntity(false) };
  state.houseLearningEndpointAvailable = false;
  state.houseLearningStatus = normalizeHouseLearningStatus(statusPayload({ enabled: true, advice_ready: true }));
  state.houseLearningLastFetchAt = 0;
  globalThis.fetch = async () => ({ ok: false, status: 503 });

  assert.equal(await refreshHouseLearningStatus({ force: true }), true);
  assert.equal(state.houseLearningEndpointAvailable, true);
  assert.equal(state.houseLearningStatus, null);
  assert.match(renderHouseLearningSettings(), /HTTP 503/);

  let releaseJson;
  let markJsonStarted;
  const jsonStarted = new Promise((resolve) => { markJsonStarted = resolve; });
  globalThis.fetch = async () => ({
    ok: true,
    status: 200,
    json: () => {
      markJsonStarted();
      return new Promise((resolve) => { releaseJson = resolve; });
    },
  });
  const pending = refreshHouseLearningStatus({ force: true });
  await jsonStarted;
  document.hidden = true;
  releaseJson(statusPayload({ enabled: true }));
  assert.equal(await pending, false);
  assert.equal(state.houseLearningStatus, null);
  document.hidden = false;
});

test("gecombineerde validatie en statusleeftijd bepalen de leerkwaliteit", () => {
  state.houseLearningStatusError = "";
  const ready = normalizeHouseLearningStatus(statusPayload({
    enabled: true,
    invalid_reasons: [],
    blocked_reasons: [],
    batch_advice_ready: true,
    rls_ready: true,
    advice_ready: false,
  }));
  assert.match(renderHouseLearningStatusMarkup(ready), /niet gezamenlijk gevalideerd/);
  assert.doesNotMatch(renderHouseLearningStatusMarkup(ready), />Warmteverlies komt overeen</);
  assert.match(renderHouseLearningStatusMarkup({ ...ready, adviceReady: true }), />Warmteverlies komt overeen</);
  assert.match(renderHouseLearningStatusMarkup({ ...ready, tickEpoch: Math.floor(Date.now() / 1000) - 31 }), /Status verouderd/);
  assert.match(renderHouseLearningStatusMarkup({ ...ready, blockedReasons: ["model_disagreement"] }), /1R1C-model verschillen/);
});

test("modelovereenstemming valideert C niet, ook bij oude firmware", () => {
  for (const extra of [{}, { capacity_validated: false }]) {
    const status = normalizeHouseLearningStatus(statusPayload({
      enabled: true, advice_ready: true, rls_ready: true, rls_samples: 100,
      c_rls_wh_per_k: 12000, invalid_reasons: [], blocked_reasons: [], ...extra,
    }));
    assert.equal(status.capacityValidated, false);
    const markup = renderHouseLearningStatusMarkup(status);
    assert.match(markup, /Warmteverlies komt overeen/);
    assert.match(markup, /Warmteopslag \(C\)[\s\S]*Voorlopig; nog niet gevalideerd/);
    assert.doesNotMatch(markup, />Gevalideerd<|>Gereed</);
  }
});

test("voorlopige 1R1C-data staat boven batchdiagnostiek", () => {
  const markup = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
    enabled: true,
    control_mode: 2,
    status: "collecting",
    invalid_reasons: [],
    records: 0,
    rls_samples: 5,
    u_rls: 196.3,
    c_rls_wh_per_k: 4803.5,
    rls_readiness_reasons: ["not_enough_samples", "outside_span"],
    sources: { ...statusPayload().sources, flow: { route: "selected_flow", valid: true } },
  })));
  assert.match(markup, /Modelstatus[\s\S]*Voorlopige schatting/);
  assert.match(markup, /5 geaccepteerde perioden van 30 minuten/);
  assert.match(markup, /1R1C-perioden[\s\S]*>5</);
  assert.match(markup, /Warmteverlies \(U\)[\s\S]*196.3 W\/K/);
  assert.match(markup, /Warmteopslag \(C\)[\s\S]*4803.5 Wh\/K/);
  assert.doesNotMatch(markup, /Nog geen metingen/);
  assert.ok(markup.indexOf("Warmteverlies (U)") < markup.indexOf("Modeldiagnostiek"));
  assert.ok(markup.indexOf("Modeldiagnostiek") < markup.indexOf("Batch warmteverlies (H)"));
});

test("leerstatus toont actuele verzameling, losse bronnen en wachtredenen", () => {
  const collecting = normalizeHouseLearningStatus(statusPayload({
    enabled: true,
    control_mode: 2,
    status: "collecting",
    invalid_reasons: ["source_stale"],
    collection: { ...statusPayload().collection, batch_active: false, thermal_active: true },
    sources: { ...statusPayload().sources, setpoint: { route: "selected_room_setpoint", valid: false }, flow: { route: "selected_flow", valid: true } },
  }));
  const markup = renderHouseLearningStatusMarkup(collecting);
  assert.match(markup, /Verzamelt nu/);
  assert.match(markup, /Woninglijn[\s\S]*Wacht[\s\S]*Wacht op kamer setpoint/);
  assert.match(markup, /Opwarmen en afkoelen \(1R1C\)[\s\S]*Verzamelt/);
  assert.match(markup, /Kamer[\s\S]*Geldig/);
  assert.match(markup, /Kamer setpoint[\s\S]*Ongeldig/);
  assert.doesNotMatch(markup, /kamer: .* · setpoint:/);
});

test("setpointherstel vertraagt alleen de woninglijn terwijl 1R1C verzamelt", () => {
  const markup = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
    enabled: true, control_mode: 2, status: "collecting",
    invalid_reasons: ["setpoint_recovery"], blocked_reasons: [],
    collection: { ...statusPayload().collection, batch_active: false, thermal_active: true },
    sources: { ...statusPayload().sources, flow: { route: "selected_flow", valid: true } },
  })));
  assert.match(markup, /Verzamelt nu/);
  assert.match(markup, /Wacht tot de kamer stabiel is na de setpointwijziging/);
  assert.match(markup, /Opwarmen en afkoelen \(1R1C\)[\s\S]*Verzamelt/);
});

test("CM0 en CM1 tonen een echte blokkade in plaats van wachten op verwarming", () => {
  for (const controlMode of [0, 1]) {
    const markup = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
      enabled: true,
      control_mode: controlMode,
      status: "blocked",
      invalid_reasons: ["boiler_heat"],
      sources: { ...statusPayload().sources, flow: { route: "selected_flow", valid: true } },
    })));
    assert.match(markup, /Wacht op meting/);
    assert.match(markup, /Verzamelen wacht tot alleen de warmtepomp verwarmt/);
    assert.doesNotMatch(markup, /ketelbijdrage nog niet uitgesloten/);
  }
});

test("CM0 en CM1 tonen verzamelen tijdens een geldige verwarmingspauze", () => {
  for (const controlMode of [0, 1]) {
    const markup = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
      enabled: true, control_mode: controlMode, status: "collecting",
      invalid_reasons: [], blocked_reasons: [],
      collection: { ...statusPayload().collection, batch_active: true, thermal_active: true },
    })));
    assert.match(markup, /Verzamelt tijdens verwarmingspauze/);
    assert.doesNotMatch(markup, /Wacht op verwarming/);
  }
});

test("blijvende runtimeblokkade vraagt ook na pauzeren om herstart met behoud van opgeslagen leerdata", () => {
  for (const enabled of [true, false]) {
    const markup = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
      enabled, status: enabled ? "blocked" : "paused", source_status: "ok",
      invalid_reasons: [], blocked_reasons: ["runtime_blocked"],
      collection: { ...statusPayload().collection, batch_active: false, thermal_active: false },
    })));
    assert.match(markup, /Herstart nodig/);
    assert.match(markup, /Herstart de regelaar en schakel passief leren opnieuw in/);
    assert.match(markup, /Opgeslagen leerdata blijft behouden/);
    assert.doesNotMatch(markup, /Wacht op meting/);
  }
  const stale = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
    enabled: true, status: "blocked", blocked_reasons: ["runtime_blocked"],
    tick_epoch: Math.floor(Date.now() / 1000) - 31,
  })));
  assert.doesNotMatch(stale, /Herstart nodig/);
});

test("serie-koppelpunt toont de watermetingen als gerichte uitleg", () => {
  state.entities = {
    hp1WaterOut: { value: 35.1, state: 35.1 },
    hp2WaterIn: { value: 30.1, state: 30.1 },
  };
  const markup = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
    enabled: true,
    control_mode: 2,
    status: "blocked",
    source_status: "series_junction_mismatch",
  })));
  assert.match(markup, /Watertemperaturen sluiten nog niet op elkaar aan/);
  assert.match(markup, /Controleer HP1 water uit en HP2 water in/);
  assert.match(markup, /HP1 water uit[\s\S]*35.1 °C/);
  assert.match(markup, /HP2 water in[\s\S]*30.1 °C/);
});

test("bronroutes en leerwaterwaarden gebruiken bestaande entities", () => {
  state.entities = {
    hp1WaterIn: { value: 28.4, state: 28.4 },
    hp1WaterOut: { value: 35.1, state: 35.1 },
    hp2WaterIn: { value: 29.8, state: 29.8 },
    hp2WaterOut: { value: 34.6, state: 34.6 },
  };
  const markup = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
    enabled: true,
    control_mode: 2,
    status: "collecting",
    collection: { ...statusPayload().collection, thermal_active: true, thermal_intervals: 0 },
    sources: {
      ...statusPayload().sources,
      flow: { route: "HP1/HP2 composition", valid: true },
      outside: { route: "Synthesized zero", valid: true },
    },
  })));
  assert.match(markup, /HP1 en HP2/);
  assert.match(markup, /0 bij stilstand/);
  assert.match(markup, /0 meetperioden afgerond/);
  for (const key of ["hp1WaterIn", "hp1WaterOut", "hp2WaterIn", "hp2WaterOut"]) assert.ok(SETTINGS_GROUP_KEY_MAP.heating.includes(key));
});

test("gepauzeerde en verouderde status claimen geen actuele verzameling of geldige bron", () => {
  const paused = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload()));
  assert.doesNotMatch(paused, /Verzamelt|>Geldig</);
  const stale = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
    enabled: true,
    control_mode: 2,
    status: "collecting",
    tick_epoch: Math.floor(Date.now() / 1000) - 31,
    sources: { ...statusPayload().sources, flow: { route: "selected_flow", valid: true } },
  })));
  assert.doesNotMatch(stale, /Verzamelt|>Geldig</);
});

test("oude firmware houdt verzamelvoortgang expliciet onbekend", () => {
  const markup = renderHouseLearningStatusMarkup(normalizeHouseLearningStatus(statusPayload({
    enabled: true,
    status: "collecting",
    collection: undefined,
    invalid_reasons: [],
  })));
  assert.match(markup, /Woninglijn[\s\S]*Onbekend/);
  assert.match(markup, /Opwarmen en afkoelen \(1R1C\)[\s\S]*Onbekend/);
  assert.match(markup, /geeft geen voortgang door/);
});

test("diagnostische export gebruikt de bestaande browserdownload", async () => {
  let clicked = false;
  const previousCreateObjectURL = URL.createObjectURL;
  const previousRevokeObjectURL = URL.revokeObjectURL;
  const previousCreateElement = document.createElement;
  const previousBody = document.body;
  URL.createObjectURL = () => "blob:house-learning";
  URL.revokeObjectURL = () => {};
  document.createElement = () => ({ click: () => { clicked = true; }, remove: () => {} });
  document.body = { appendChild: () => {} };
  globalThis.fetch = async () => ({ ok: true, status: 200, json: async () => ({ schema: 1, records: [] }) });
  state.busyAction = "";
  try {
    await downloadHouseLearningExport();
    assert.equal(clicked, true);
    assert.equal(state.busyAction, "");
  } finally {
    URL.createObjectURL = previousCreateObjectURL;
    URL.revokeObjectURL = previousRevokeObjectURL;
    document.createElement = previousCreateElement;
    document.body = previousBody;
  }
});

test("learnerpaneel vereist switch plus endpoint zonder installatie-instellingen", () => {
  state.entities = {};
  state.houseLearningEndpointAvailable = true;
  state.houseLearningStatus = normalizeHouseLearningStatus(statusPayload());
  assert.equal(renderHouseLearningSettings(), "");

  state.entities = {
    houseLearningEnabled: switchEntity(false),
    houseLearningReset: {},
  };
  const markup = renderHouseLearningSettings();
  assert.match(markup, /Passief leren/);
  assert.doesNotMatch(markup, /data-oq-field="houseLearning(?:Hydraulics|Fluid)"/);
  assert.doesNotMatch(markup, /Hydraulische opstelling|Warmtedragende vloeistof/);
  assert.match(markup, /Geen automatische wijzigingen/);
  assert.doesNotMatch(markup, /Andere warmtebron|No other heat in CM2|R1-uit is geen bewijs/);
  assert.doesNotMatch(markup, /Meetcontract en grenzen|Calorimetrie gecontroleerd|Onzekerheid warmtevermogen|Maximale meetflow|Temperatuurtolerantie koppelpunt|Grens externe warmtebijdrage/);
  assert.match(markup, /Nog niet beoordeeld/);
  assert.match(markup, /data-oq-action="download-house-learning"/);
  assert.ok(SETTINGS_GROUP_KEY_MAP.heating.includes("houseLearningEnabled"));
  assert.ok(!SETTINGS_GROUP_KEY_MAP.heating.includes("houseLearningHydraulics"));
  assert.ok(!SETTINGS_GROUP_KEY_MAP.heating.includes("houseLearningFluid"));
  for (const key of ["houseLearningCalorimetryConfirmed", "houseLearningHeatUncertainty", "houseLearningMaximumFlow", "houseLearningJunctionTolerance", "houseLearningGainBound"]) {
    assert.ok(!SETTINGS_GROUP_KEY_MAP.heating.includes(key));
    assert.ok(!markup.includes(`data-oq-field="${key}"`));
  }
});

test("mock biedt de capability alleen voor Q-edition en Waveshare", async () => {
  const mockSource = await readFile(new URL("../js/mock-device.js", import.meta.url), "utf8");
  assert.match(mockSource, /state\.hardware === "heatpump_controller_q" \|\| state\.hardware === "waveshare"/);
  assert.match(mockSource, /entities\.delete\(entityKey\(domain, name\)\)/);
  assert.match(mockSource, /\/openquatt\/learning\/status/);
  assert.doesNotMatch(mockSource, /Power House Learning (Calorimetry Confirmed|Heat Uncertainty|Maximum Flow|Junction Tolerance|Gain Bound|External Heat)|No other heat in CM2/);
  assert.match(mockSource, /records: 32/);
});

test("firmware-endpoint bewaart caches en requestscratch uitsluitend in PSRAM", async () => {
  const [header, source, runtime] = await Promise.all([
    readFile(new URL("../../../components/openquatt_house_learning_status/OpenQuattHouseLearningStatus.h", import.meta.url), "utf8"),
    readFile(new URL("../../../components/openquatt_house_learning_status/OpenQuattHouseLearningStatus.cpp", import.meta.url), "utf8"),
    readFile(new URL("../../includes/control/oq_ph_learning_runtime.h", import.meta.url), "utf8"),
  ]);
  assert.match(header, /STATUS_BUFFER_SIZE = 4U \* 1024U/);
  assert.match(header, /EXPORT_BUFFER_SIZE = 32U \* 1024U/);
  assert.match(header, /REQUEST_BUFFER_SIZE = EXPORT_BUFFER_SIZE/);
  assert.match(source, /status_buffer_\.allocate_external/);
  assert.match(source, /export_buffer_\.allocate_external/);
  assert.match(source, /request_buffer_\.allocate_external/);
  assert.match(source, /xSemaphoreTake\(this->request_mutex_, 0\)/);
  assert.match(source, /request->send\(429/);
  assert.match(source, /request_is_authenticated/);
  assert.match(source, /snapshot_(?:status|export).*write_snapshot/s);
  assert.match(runtime, /\\\"invalid_reasons\\\":/);
  assert.match(runtime, /\\\"invalid_reasons_mask\\\"/);
  assert.match(runtime, /\\\"rls_readiness_reasons_mask\\\"/);
  assert.match(runtime, /\\\"tick_epoch\\\"/);
  assert.match(runtime, /\\\"model_validation_status\\\"/);
  assert.match(runtime, /ModelValidationStatus::MODEL_DISAGREEMENT/);
  assert.match(runtime, /enabled && summary\.cross_validated_advice_ready/);
  assert.match(runtime, /__DATE__[\s\S]*__TIME__[\s\S]*ph-passive-1/);
});


test("preview-reset pauzeert leren en bevestigt gewiste opslag zoals de firmware", async () => {
  const source = await readFile(new URL("../js/mock-device.js", import.meta.url), "utf8");
  const functionSource = (name) => {
    const start = source.indexOf(`  function ${name}(`);
    assert.ok(start >= 0);
    return source.slice(start, source.indexOf("\n  function ", start + 1));
  };
  let enabled = true;
  const mockState = { boiler: "off", houseLearning: { records: 32, rlsSamples: 174, resetCount: 0, journalStatus: "ready" } };
  const mock = runInNewContext(`${functionSource("handleButtonPress")}\n${functionSource("getHouseLearningStatusPayload")}\n({ handleButtonPress, getHouseLearningStatusPayload })`, {
    state: mockState,
    isSwitchEnabled: (name) => name === "Power House Passive Learning" ? enabled : true,
    setSwitch: (name, value) => { assert.equal(name, "Power House Passive Learning"); enabled = value; },
    getEntity: (domain) => ({ value: domain === "text_sensor" ? "CM2 - Heatpump" : 50 }),
    notifyMockUpdated: () => {},
  });
  assert.equal(isHouseLearningResetConfirmed(normalizeHouseLearningStatus(mock.getHouseLearningStatusPayload())), false);
  mock.handleButtonPress("Power House Learning Reset");
  const status = normalizeHouseLearningStatus(mock.getHouseLearningStatusPayload());
  assert.equal(isHouseLearningResetConfirmed(status), true);
  assert.equal(status.rlsSamples, 0);
  assert.equal(mockState.houseLearning.resetCount, 1);
});


test("grafiekpatch behoudt meetpuntfocus bij ongewijzigde data en volgt nieuwe fit", () => {
  state.entities = { houseLearningEnabled: switchEntity(true), houseColdTemp: { value: -10 }, houseOutdoorMax: { value: 16 }, housePower: { value: 5200 } };
  state.houseLearningEndpointAvailable = true;
  state.houseLearningStatus = normalizeHouseLearningStatus(statusPayload());
  state.houseLearningChart = [];
  state.houseLearningChartFetchedAt = 100;
  const statusNode = { innerHTML: "" };
  let signature;
  let writes = 0;
  const chartNode = { getAttribute: () => signature, setAttribute: (_key, value) => { signature = value; }, set innerHTML(_value) { writes += 1; } };
  const panel = { querySelector: (selector) => selector === "[data-oq-house-learning-status]" ? statusNode : chartNode };
  state.root = { querySelector: () => panel };
  patchHouseLearningSettingsStatus();
  patchHouseLearningSettingsStatus();
  assert.equal(writes, 1);
  state.houseLearningStatus.hBatch = 210;
  patchHouseLearningSettingsStatus();
  assert.equal(writes, 2);
});
