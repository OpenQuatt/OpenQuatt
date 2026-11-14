import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false, activeElement: null };
globalThis.window = { innerWidth: 1200, location: { pathname: "/" }, setTimeout, clearTimeout,
  localStorage: { getItem: () => null }, sessionStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { normalizeWeatherForecast, weatherForecastCanDisplay, weatherScenarioCanDisplay, weatherScenarioNow } = await import("../js/src/core/weather-forecast.js");
const { refreshWeatherForecast, invalidateWeatherForecast, suspendWeatherForecast } = await import("../js/src/features/weather-forecast.js");
const { renderForecastProgress } = await import("../js/src/settings/forecast-progress.js");
const { renderWeatherForecast } = await import("../js/src/settings/weather-forecast.js");
const { setLocale, t } = await import("../js/src/i18n/index.js");
const nativeFetch = globalThis.fetch, nativePerformance = globalThis.performance, nativeNow = Date.now;
const epoch = 1800000000;
const entity = value => ({ value, state: value });
const response = value => ({ status: 200, ok: true, json: async () => value });
const payload = () => ({ schema: 1, model: "knmi_harmonie_arome_netherlands", configured_location: "Brummen", location: "Brummen",
  enabled: true, valid: true, configuration: 4, generation: 8, now_epoch: epoch + 900, fetched_epoch: epoch + 900,
  start_epoch: epoch, step_s: 3600, temperature_c: Array(36).fill(10), wind_mps: Array(36).fill(2),
  current: { valid: false, time_epoch: 0 },
  scenario: { schema: 1, valid: true, reason: "ready", issued_epoch: epoch + 840, configuration: 4,
    weather_generation: 7, weather_fetched_epoch: epoch + 780, weather_start_epoch: epoch, model_context: 1, model_samples: 155,
    room_start_c: 0, held_heat_w: 0, h_w_per_k: 220, c_wh_per_k: 8000,
    horizon_valid: [true, true, true], temperature_c: [0, 0.25, 0.5] } });

test.beforeEach(() => {
  state.root = null; invalidateWeatherForecast(); state.learningDisplayMode = "actual";
  window.location.pathname = "/";
  state.appView = "learning"; state.learningPanel = "forecast"; state.nativeOpen = false; state.busyAction = ""; state.drafts = {};
  state.entities = { houseLearningEnabled: entity(true), weatherEnabled: entity(true), weatherLocation: entity("Brummen"), weatherStatus: entity("observing"),
    weatherIssuedEpoch: entity(epoch + 840), weatherTemperature30m: entity(99), weatherTemperature1h: entity(98), weatherTemperature3h: entity(97) };
  state.houseLearningStatus = null; state.houseLearningStatusError = ""; document.hidden = false;
  Date.now = () => (epoch + 900) * 1000;
  setLocale("nl", { persist: false, applyDocument: false, notify: false });
});
test.afterEach(() => { state.root = null; invalidateWeatherForecast(); globalThis.fetch = nativeFetch; Date.now = nativeNow; globalThis.performance = nativePerformance; });

test("the atomic issue owns all three values and zero-valued frozen context in both locales", () => {
  const p = payload(); state.weatherForecast = normalizeWeatherForecast(p);
  p.scenario.temperature_c[0] = 99; p.scenario.horizon_valid[0] = false; p.scenario.room_start_c = 20;
  assert.equal(state.weatherForecast.scenario.temperature_c[0], 0);
  assert.equal(state.weatherForecast.scenario.room_start_c, 0);
  assert.equal(weatherScenarioCanDisplay(state.weatherForecast), true);
  assert.ok(state.weatherForecast.scenario.issued_epoch < state.weatherForecast.fetched_epoch);
  for (const locale of ["nl", "en"]) {
    setLocale(locale, { persist: false, applyDocument: false, notify: false });
    const html = renderForecastProgress();
    assert.match(html, /data-quality="provisional"/);
    assert.match(html, locale === "nl" ? /0,0 °C/ : /0\.0 °C/);
    assert.doesNotMatch(html, /99 °C|98 °C|97 °C/);
    assert.equal((html.match(/data-scenario-horizon=/g) || []).length, 3);
    assert.ok(html.includes(t("learningWorkspace.forecast.condition")));
    assert.ok(html.includes(t("learningWorkspace.forecast.earlierWeather")));
  }
});

test("older restored issues stay hidden while the weather is still usable", () => {
  const p = payload();
  p.scenario.restored_model = true;
  state.weatherForecast = normalizeWeatherForecast(p);
  assert.match(renderForecastProgress(), /data-scenario-freshness="unavailable"/);
  assert.match(renderWeatherForecast(), /<figure /);
  p.scenario.restored_model = "true";
  assert.throws(() => normalizeWeatherForecast(p), /weather-scenario-format/);
});

test("weather settings show current errors despite a cached scenario", () => {
  state.weatherForecast = normalizeWeatherForecast(payload());
  state.learningPanel = "configuration";
  state.entities.weatherStatus = entity("dns_failed");
  assert.match(renderForecastProgress({ statusOnly: true }), /Nieuwe weergegevens ophalen lukt niet/);
  state.entities.weatherLocation = entity("Utrecht");
  state.entities.weatherStatus = entity("location_ambiguous");
  assert.match(renderForecastProgress({ statusOnly: true }), /Meerdere plaatsen gevonden/);
  state.entities.weatherStatus = entity("observing");
  assert.equal(renderForecastProgress({ statusOnly: true }), "");
});

test("older forecast responses show weather without claiming room estimates", async () => {
  const legacy = payload(); delete legacy.scenario;
  state.weatherForecast = normalizeWeatherForecast(legacy);
  let html = renderForecastProgress();
  assert.match(html, /data-scenario-freshness="unavailable"/);
  assert.match(renderWeatherForecast(), /<figure /);
  assert.doesNotMatch(html, /99,0 °C/);
  const invalid = payload(); Object.assign(invalid.scenario, { valid: false, reason: "model_unavailable", horizon_valid: [false, false, false], temperature_c: [null, null, null] });
  state.weatherForecast = normalizeWeatherForecast(invalid);
  html = renderForecastProgress(); assert.match(html, /data-quality="unknown"/);
  assert.doesNotMatch(html, /99,0 °C|98,0 °C|97,0 °C/);
  for (const value of [null, undefined, {}]) {
    const p = payload(); p.scenario = value; globalThis.fetch = async () => response(p);
    assert.equal(await refreshWeatherForecast({ force: true }), false);
    assert.equal(state.weatherForecast, null);
    assert.doesNotMatch(renderForecastProgress(), /99,0 °C|98,0 °C|97,0 °C/);
  }
});

test("malformed or mismatched atomic fields fail closed while same-config newer weather remains legitimate", () => {
  const variants = [s => { s.schema = 2; }, s => { s.valid = "true"; }, s => { s.issued_epoch = 0; },
    s => { s.issued_epoch = epoch + 901; }, s => { s.configuration++; }, s => { s.weather_generation = 0; },
    s => { s.weather_fetched_epoch = s.issued_epoch + 1; }, s => { s.weather_start_epoch++; },
    s => { s.model_context = 0; }, s => { s.model_samples = 0; }, s => { s.room_start_c = null; },
    s => { s.held_heat_w = null; }, s => { s.h_w_per_k = 0; }, s => { s.c_wh_per_k = null; },
    s => { s.horizon_valid.pop(); }, s => { s.horizon_valid[0] = false; }, s => { s.temperature_c[0] = null; },
    s => { s.temperature_c[0] = Infinity; }, s => { s.valid = false; }];
  for (const change of variants) { const p = payload(); change(p.scenario); assert.throws(() => normalizeWeatherForecast(p)); }
  assert.equal(normalizeWeatherForecast(payload()).scenario.weather_generation, 7);
  const disabled = payload(); disabled.enabled = disabled.valid = false;
  assert.throws(() => normalizeWeatherForecast(disabled));
});

test("controller time and monotonic receipt age govern expiry, body delay and stale publications", () => {
  let mono = 5000, wall = 100000;
  globalThis.performance = { now: () => mono }; Date.now = () => wall;
  const p = payload(); p.now_epoch = p.scenario.issued_epoch + 1799; p.fetched_epoch = p.now_epoch;
  state.weatherForecast = normalizeWeatherForecast(p, wall, mono);
  assert.equal(weatherScenarioCanDisplay(state.weatherForecast), true);
  assert.equal(weatherScenarioNow(state.weatherForecast), p.now_epoch);
  assert.match(renderForecastProgress(), /0,0 °C/);
  mono += 1001; wall += 1001;
  const html = renderForecastProgress();
  const first = html.slice(html.indexOf('data-scenario-horizon="1800"'), html.indexOf('data-scenario-horizon="3600"'));
  assert.doesNotMatch(first, /0,0 °C/); assert.match(first, /verlopen/);
  mono += 30000; wall += 30000;
  assert.equal(weatherScenarioCanDisplay(state.weatherForecast), false);
  assert.doesNotMatch(renderForecastProgress(), /0,25 °C/);
  assert.match(renderForecastProgress(), /data-scenario-freshness="unavailable"/);
});

test("late bodies after off, pending saves, receipt failure or location changes cannot repaint older atomic data", async () => {
  for (const action of [() => { state.entities.weatherEnabled = entity(false); },
    () => { state.busyAction = "save-weatherLocation"; }, () => { state.entities.weatherLocation.save_status = "pending"; },
    () => { state.entities.weatherLocation.save_status = "failed"; }, () => { state.entities.weatherLocation = entity("Utrecht"); }]) {
    invalidateWeatherForecast(); state.busyAction = ""; state.entities.weatherEnabled = entity(true); state.entities.weatherLocation = entity("Brummen");
    let body; globalThis.fetch = async () => ({ status: 200, ok: true, json: () => new Promise(resolve => { body = resolve; }) });
    const pending = refreshWeatherForecast({ force: true });
    for (let i = 0; i < 5 && !body; i++) await Promise.resolve();
    assert.ok(body); action(); body(payload());
    assert.equal(await pending, false); assert.equal(state.weatherForecast, null);
  }
});

test("known receipt transitions hide cached issues; a new authoritative frame can prove recovery", async () => {
  globalThis.fetch = async () => response(payload());
  assert.equal(await refreshWeatherForecast({ force: true }), true);
  state.entities.weatherLocation.save_request = "0123456789abcdef0123456789abcdef";
  state.entities.weatherLocation.save_status = "failed";
  assert.equal(weatherScenarioCanDisplay(state.weatherForecast), false);
  assert.doesNotMatch(renderForecastProgress(), /0,25 °C|0,5 °C/);
  assert.equal(await refreshWeatherForecast({ force: true }), true);
  assert.equal(weatherScenarioCanDisplay(state.weatherForecast), true);
});

test("partial JSON retains the last issue without renewing it and body download time cannot extend a target deadline", async () => {
  const cached = state.weatherForecast = normalizeWeatherForecast(payload());
  globalThis.fetch = async () => ({ status: 200, ok: true, json: async () => { throw new SyntaxError("partial JSON"); } });
  assert.equal(await refreshWeatherForecast({ force: true }), false);
  assert.equal(state.weatherForecast, cached);
  assert.match(renderForecastProgress(), /0,25 °C/);
  assert.match(renderForecastProgress(), /data-scenario-freshness="cached"/);
  assert.doesNotMatch(renderForecastProgress(), /99,0 °C|98,0 °C|97,0 °C/);
  let mono = 5000, wall = 100000;
  globalThis.performance = { now: () => mono }; Date.now = () => wall;
  const p = payload(); p.now_epoch = p.scenario.issued_epoch + 1799; p.fetched_epoch = p.now_epoch;
  globalThis.fetch = async () => ({ status: 200, ok: true, json: async () => { mono += 7000; wall += 7000; return p; } });
  assert.equal(await refreshWeatherForecast({ force: true }), true);
  assert.equal(state.weatherForecast.receivedMono, 5000);
  const html = renderForecastProgress();
  const first = html.slice(html.indexOf('data-scenario-horizon="1800"'), html.indexOf('data-scenario-horizon="3600"'));
  assert.doesNotMatch(first, /0,0 °C/); assert.match(first, /verlopen/);
});

test("a missed fetch keeps all atomic values with an explicit cached label and success restores current status", async () => {
  let mono = 5000, wall = 100000;
  globalThis.performance = { now: () => mono }; Date.now = () => wall;
  globalThis.fetch = async () => response(payload());
  assert.equal(await refreshWeatherForecast({ force: true }), true);
  const cached = state.weatherForecast;
  assert.match(renderForecastProgress(), /data-scenario-freshness="current"/);
  mono += 10000; wall += 10000;
  globalThis.fetch = async () => { throw new TypeError("Failed to fetch"); };
  assert.equal(await refreshWeatherForecast({ force: true }), false);
  assert.equal(state.weatherForecast, cached);
  assert.equal(state.weatherForecast.receivedMono, 5000);
  for (const locale of ["nl", "en"]) {
    setLocale(locale, { persist: false, applyDocument: false, notify: false });
    const html = renderForecastProgress();
    assert.match(html, /data-scenario-freshness="cached"/);
    assert.ok(html.includes(t("learningWorkspace.forecast.lastReceived")));
    assert.ok(html.includes(t("learningWorkspace.forecast.cachedCopy")));
    assert.equal((html.match(/<strong>[^<]*°C<\/strong>/g) || []).length, 3);
  }
  mono += 31000; wall += 31000;
  assert.equal(weatherScenarioCanDisplay(cached), false);
  const p = payload(); p.now_epoch += 41; p.scenario.temperature_c = [0.1, 0.3, 0.6];
  globalThis.fetch = async () => response(p);
  assert.equal(await refreshWeatherForecast({ force: true }), true);
  assert.notEqual(state.weatherForecast, cached);
  assert.equal(state.weatherForecastError, "");
  assert.match(renderForecastProgress(), /data-scenario-freshness="current"/);
  assert.doesNotMatch(renderForecastProgress(), /data-scenario-freshness="cached"/);
});

test("room scenario expires after 30 seconds without confirmation while weather stays available", () => {
  let mono = 5000, wall = 100000;
  globalThis.performance = { now: () => mono }; Date.now = () => wall;
  const cached = state.weatherForecast = normalizeWeatherForecast(payload());
  state.weatherForecastError = "failed";
  document.hidden = true;
  suspendWeatherForecast();
  mono += 30000; wall += 30000;
  document.hidden = false;
  assert.match(renderForecastProgress(), /data-scenario-freshness="cached"/);
  mono += 1; wall += 1;
  assert.match(renderForecastProgress(), /data-scenario-freshness="unavailable"/);
  assert.equal(state.weatherForecast, cached);
  assert.match(renderWeatherForecast(), /<figure /);
});

test("known learner disablement and pending reset hide the last room scenario", () => {
  state.weatherForecast = normalizeWeatherForecast(payload());
  assert.match(renderForecastProgress(), /0,25 °C/);
  state.entities.houseLearningEnabled = entity(false);
  assert.doesNotMatch(renderForecastProgress(), /0,25 °C/);
  assert.match(renderWeatherForecast(), /<figure /);
  state.entities.houseLearningEnabled = entity(true);
  state.houseLearningReset = "pending";
  assert.doesNotMatch(renderForecastProgress(), /0,25 °C/);
  state.houseLearningReset = "";
});

test("an authoritative invalid scenario or disabled frame replaces the cache; malformed full frames clear it", async () => {
  for (const kind of ["invalid", "disabled", "format", "scenario-format", "denied", "unsupported"]) {
    invalidateWeatherForecast(); state.weatherForecast = normalizeWeatherForecast(payload());
    const p = payload();
    if (["invalid", "disabled"].includes(kind)) {
      Object.assign(p.scenario, { valid: false, reason: "context_changed", horizon_valid: [false, false, false], temperature_c: [null, null, null] });
      if (kind === "disabled") p.enabled = p.valid = false;
    } else if (kind === "format") p.schema = 2;
    else if (kind === "scenario-format") p.scenario.configuration++;
    globalThis.fetch = async () => ({ ...response(p), status: kind === "denied" ? 403 : kind === "unsupported" ? 404 : 200, ok: !["denied", "unsupported"].includes(kind) });
    const result = await refreshWeatherForecast({ force: true });
    assert.equal(result, ["invalid", "disabled"].includes(kind));
    assert.doesNotMatch(renderForecastProgress(), /<strong>[^<]*°C<\/strong>/);
    if (!["invalid", "disabled"].includes(kind)) assert.equal(state.weatherForecast, null);
  }
});

test("cached atomic data still hides on consent, location, receipt, endpoint or clock changes", () => {
  for (const change of [() => { state.entities.weatherEnabled = entity(false); },
    () => { state.drafts.weatherEnabled = false; }, () => { state.busyAction = "save-weatherLocation"; },
    () => { state.entities.weatherLocation = entity("Utrecht"); }, () => { state.entities.weatherLocation.save_status = "pending"; },
    () => { window.location.pathname = "/other/"; }, () => { Date.now = () => 9999999999999; }]) {
    window.location.pathname = "/"; state.busyAction = ""; state.drafts = {};
    state.entities.weatherEnabled = entity(true); state.entities.weatherLocation = entity("Brummen");
    Date.now = () => (epoch + 900) * 1000;
    const data = normalizeWeatherForecast(payload(), Date.now() - 12000, performance.now() - 12000);
    data.locationReceipt = JSON.stringify(["", ""]);
    state.weatherForecast = data; state.weatherForecastError = "failed";
    assert.match(renderForecastProgress(), /0,25 °C/);
    change();
    assert.doesNotMatch(renderForecastProgress(), /<strong>[^<]*°C<\/strong>/);
  }
});

test("legacy scalar scenarios do not acquire atomic cache privileges", async () => {
  const p = payload(); delete p.scenario;
  state.weatherForecast = normalizeWeatherForecast(p);
  assert.doesNotMatch(renderForecastProgress(), /99,0 °C/);
  globalThis.fetch = async () => { throw new TypeError("offline"); };
  assert.equal(await refreshWeatherForecast({ force: true }), false);
  assert.ok(state.weatherForecast);
  assert.doesNotMatch(renderForecastProgress(), /99,0 °C|98,0 °C|97,0 °C/);
  state.weatherForecastError = "";
  state.weatherForecast = normalizeWeatherForecast(p, Date.now() - 30001, performance.now() - 30001);
  assert.doesNotMatch(renderForecastProgress(), /99,0 °C|98,0 °C|97,0 °C/);
});

test("a current response for another configured location clears the old scenario instead of retaining it", async () => {
  globalThis.fetch = async () => response(payload());
  assert.equal(await refreshWeatherForecast({ force: true }), true);
  assert.match(renderForecastProgress(), /0,25 °C/);
  globalThis.fetch = async () => response({ ...payload(), configured_location: "Utrecht", location: "Utrecht" });
  assert.equal(await refreshWeatherForecast({ force: true }), false);
  assert.equal(state.weatherForecast, null);
  assert.equal(state.weatherForecastError, "failed");
  assert.doesNotMatch(renderForecastProgress(), /<strong>[^<]*°C<\/strong>/);
});

test("cached atomic and legacy data are bound to the local endpoint, never to wire metadata", () => {
  for (const legacy of [false, true]) {
    window.location.pathname = "/a/";
    const p = payload(); p.endpoint = "/b/openquatt/weather/forecast";
    if (legacy) delete p.scenario;
    state.weatherForecast = normalizeWeatherForecast(p);
    assert.equal(state.weatherForecast.endpoint, "/a/openquatt/weather/forecast");
    assert.equal(weatherForecastCanDisplay(state.weatherForecast), true);
    assert.match(renderWeatherForecast(), /<figure /);
    if (legacy) assert.doesNotMatch(renderForecastProgress(), /99,0 °C/);
    else assert.match(renderForecastProgress(), /0,25 °C/);
    window.location.pathname = "/b/index.html";
    assert.equal(weatherForecastCanDisplay(state.weatherForecast), false);
    assert.equal(weatherScenarioCanDisplay(state.weatherForecast), false);
    assert.doesNotMatch(renderWeatherForecast(), /<figure /);
    assert.doesNotMatch(renderForecastProgress(), /99,0 °C|0,25 °C/);
  }
});

test("request endpoint identity survives hostile metadata and a late body cannot enter another base path", async () => {
  window.location.pathname = "/a";
  let body;
  globalThis.fetch = async url => {
    assert.equal(url, "/a/openquatt/weather/forecast");
    return { status: 200, ok: true, json: () => new Promise(resolve => { body = resolve; }) };
  };
  const pending = refreshWeatherForecast({ force: true });
  for (let i = 0; i < 5 && !body; i++) await Promise.resolve();
  assert.ok(body); window.location.pathname = "/b"; body(payload());
  assert.equal(await pending, false); assert.equal(state.weatherForecast, null);
  globalThis.fetch = async url => {
    assert.equal(url, "/b/openquatt/weather/forecast");
    return response({ ...payload(), endpoint: "/a/openquatt/weather/forecast" });
  };
  assert.equal(await refreshWeatherForecast({ force: true }), true);
  assert.equal(state.weatherForecast.endpoint, "/b/openquatt/weather/forecast");
  assert.equal(weatherScenarioCanDisplay(state.weatherForecast), true);
});
