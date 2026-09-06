import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

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
  normalizeHouseLearningStatus,
  refreshHouseLearningStatus,
  resetHouseLearningData,
  shouldRefreshHouseLearningStatusSurface,
} = await import("../js/src/features/house-learning.js");
const { renderHouseLearningSettings, renderHouseLearningStatusMarkup } = await import("../js/src/settings/house-learning.js");
const { SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");

function switchEntity(value = false) {
  return { value, state: value };
}

test.afterEach(() => {
  document.hidden = false;
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.houseLearningStatus = null;
  state.houseLearningStatusError = "";
  state.houseLearningLastFetchAt = 0;
  state.houseLearningFetchPromise = null;
  state.houseLearningRequestId = 0;
  state.houseLearningReset = "";
  state.houseLearningResetError = "";
});

function statusPayload(overrides = {}) {
  return {
    schema: 1,
    mode: "passive",
    enabled: false,
    storage_ready: true,
    status: "paused",
    source_status: "blocked",
    model_validation_status: "batch_model_unavailable",
    journal_status: "ready",
    invalid_reasons: ["calorimetry_not_verified"],
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
  assert.doesNotMatch(renderHouseLearningStatusMarkup(ready), /Voldoende data/);
  assert.match(renderHouseLearningStatusMarkup({ ...ready, adviceReady: true }), /Voldoende data/);
  assert.match(renderHouseLearningStatusMarkup({ ...ready, tickEpoch: Math.floor(Date.now() / 1000) - 31 }), /Status verouderd/);
  assert.match(renderHouseLearningStatusMarkup({ ...ready, blockedReasons: ["model_disagreement"] }), /1R1C-model verschillen/);
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

test("learnerpaneel vereist switch plus endpoint en toont fail-closed meetcontract", () => {
  state.entities = {};
  state.houseLearningEndpointAvailable = true;
  state.houseLearningStatus = normalizeHouseLearningStatus(statusPayload());
  assert.equal(renderHouseLearningSettings(), "");

  state.entities = {
    installationTopology: { value: "duo" },
    houseLearningEnabled: switchEntity(false),
    houseLearningReset: {},
    houseLearningHydraulics: { value: "Series HP1 to HP2", option: ["Unknown", "Series HP1 to HP2"] },
    houseLearningFluid: { value: "Water", option: ["Unknown", "Water"] },
    houseLearningExternalHeat: { value: "No other heat in CM2", option: ["Unknown", "No other heat in CM2"] },
    houseLearningCalorimetryConfirmed: switchEntity(false),
    houseLearningHeatUncertainty: { value: 0, min_value: 0, max_value: 2000, step: 10, uom: "W" },
  };
  const markup = renderHouseLearningSettings();
  assert.match(markup, /Passief leren/);
  assert.match(markup, /Water · Duo: HP1 → HP2 in serie/);
  assert.doesNotMatch(markup, /data-oq-field="houseLearning(?:Hydraulics|Fluid)"/);
  assert.doesNotMatch(markup, /Hydraulische opstelling|Warmtedragende vloeistof/);
  assert.match(markup, /Geen automatische wijzigingen/);
  assert.doesNotMatch(markup, /Andere warmtebron|No other heat in CM2|R1-uit is geen bewijs/);
  assert.ok(!SETTINGS_GROUP_KEY_MAP.heating.includes("houseLearningExternalHeat"));
  assert.doesNotMatch(markup, />0 W\/K</);
  assert.match(markup, /warmtemeting nog niet gecontroleerd/);
  assert.doesNotMatch(markup, /calorimetry_not_confirmed/);
  assert.equal(markup.match(/warmtemeting nog niet gecontroleerd/g)?.length, 1);
  assert.match(markup, /Nog niet beoordeeld/);
  assert.match(markup, /data-oq-action="download-house-learning"/);
  assert.ok(SETTINGS_GROUP_KEY_MAP.heating.includes("houseLearningEnabled"));
  assert.ok(!SETTINGS_GROUP_KEY_MAP.heating.includes("houseLearningHydraulics"));
  assert.ok(!SETTINGS_GROUP_KEY_MAP.heating.includes("houseLearningFluid"));
  state.entities.installationTopology = { value: "single" };
  assert.match(renderHouseLearningSettings(), /Water · Single: één warmtepomp/);
  assert.doesNotMatch(renderHouseLearningSettings(), /HP1 → HP2/);
  for (const key of ["houseLearningCalorimetryConfirmed", "houseLearningHeatUncertainty", "houseLearningMaximumFlow", "houseLearningJunctionTolerance", "houseLearningGainBound"]) {
    assert.ok(!SETTINGS_GROUP_KEY_MAP.heating.includes(key));
    assert.ok(!markup.includes(`data-oq-field="${key}"`));
  }
  assert.doesNotMatch(markup, /Meetcontract en grenzen|Calorimetrie gecontroleerd|Onzekerheid warmtevermogen|Maximale meetflow|Temperatuurtolerantie koppelpunt|Grens externe warmtebijdrage/);
});

test("mock biedt de capability alleen voor Q-edition en Waveshare", async () => {
  const mockSource = await readFile(new URL("../js/mock-device.js", import.meta.url), "utf8");
  assert.match(mockSource, /state\.hardware === "heatpump_controller_q" \|\| state\.hardware === "waveshare"/);
  assert.match(mockSource, /entities\.delete\(entityKey\(domain, name\)\)/);
  assert.match(mockSource, /\/openquatt\/learning\/status/);
  assert.doesNotMatch(mockSource, /Power House Learning External Heat|No other heat in CM2/);
  assert.match(mockSource, /records: 32/);
});

test("firmware-endpoint bewaart caches en requestscratch uitsluitend in PSRAM", async () => {
  const [header, source, runtime] = await Promise.all([
    readFile(new URL("../../../components/openquatt_house_learning_status/OpenQuattHouseLearningStatus.h", import.meta.url), "utf8"),
    readFile(new URL("../../../components/openquatt_house_learning_status/OpenQuattHouseLearningStatus.cpp", import.meta.url), "utf8"),
    readFile(new URL("../../includes/control/oq_ph_learning_runtime.h", import.meta.url), "utf8"),
  ]);
  assert.match(header, /STATUS_BUFFER_SIZE = 4U \* 1024U/);
  assert.match(header, /EXPORT_BUFFER_SIZE = 24U \* 1024U/);
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
