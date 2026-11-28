import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
let resized, observed;
globalThis.ResizeObserver = class {
  constructor(callback) { resized = callback; }
  observe(target) { observed = target; }
  disconnect() { observed = null; }
};
globalThis.document = { hidden: false, activeElement: null };
globalThis.window = {
  innerWidth: 1200, location: { pathname: "/" }, setTimeout, clearTimeout,
  localStorage: { getItem: () => null }, sessionStorage: { getItem: () => null },
};
const nativeFetch = globalThis.fetch;
const { state } = await import("../js/src/core/state.js");
const { normalizeWeatherForecast, weatherForecastCanDisplay, weatherForecastIsCurrent, weatherCurrentModelIsCurrent } = await import("../js/src/core/weather-forecast.js");
const { refreshWeatherForecast, invalidateWeatherForecast, suspendWeatherForecast } = await import("../js/src/features/weather-forecast.js");
const { renderWeatherForecast, getWeatherChartModel, weatherChartPaths, patchWeatherForecast, getWeatherPointIndex, handleWeatherChartPointer, handleWeatherChartKeyDown, patchWeatherForecastCursor } = await import("../js/src/settings/weather-forecast.js");
const { setLocale } = await import("../js/src/i18n/index.js");

const epoch = 1793314800 - 1793314800 % 3600;
const payload = () => ({ schema: 1, model: "knmi_harmonie_arome_netherlands", configured_location: "Brummen", location: "Brummen (52.09000, 6.15556)",
  enabled: true, valid: true, configuration: 4, generation: 7, now_epoch: epoch + 900, fetched_epoch: epoch + 900,
  start_epoch: epoch, step_s: 3600, temperature_c: Array(36).fill(10), wind_mps: Array(36).fill(2),
  current: { source: "model", valid: true, time_epoch: epoch + 900, temperature_c: 10, relative_humidity_pct: 90, dew_point_c: 8.5 } });
const entity = (value) => ({ value, state: value });
const response = (value, status = 200) => ({ status, ok: status >= 200 && status < 300, json: async () => value });

test.beforeEach(() => {
  state.root = null;
  invalidateWeatherForecast();
  state.weatherForecastHourlyOpen = false; state.appView = "learning"; state.learningPanel = "forecast"; state.settingsGroup = "heating"; state.nativeOpen = false;
  state.busyAction = ""; state.entities = { weatherEnabled: entity(true), weatherLocation: entity("Brummen") };
  document.hidden = false;
  setLocale("nl", { persist: false, applyDocument: false, notify: false });
});
test.afterEach(() => { state.root = null; invalidateWeatherForecast(); globalThis.fetch = nativeFetch; window.setTimeout = setTimeout; });

test("valid controller data preserves exact epochs, units and independent copies", () => {
  const p = payload(), data = normalizeWeatherForecast(p, 100000, 5000);
  p.temperature_c[0] = 50; p.current.dew_point_c = 30;
  assert.equal(data.temperature_c[0], 10); assert.equal(data.current.dew_point_c, 8.5);
  assert.equal(data.now_epoch, epoch + 900);
  assert.equal(weatherForecastIsCurrent(data, 110000, 15000), true);
});

test("malformed forecast schema, nonnumeric data and wrong coverage fail closed", () => {
  const variants = [
    (p) => { p.schema = 2; }, (p) => { p.model = "other"; }, (p) => { p.wind_mps.pop(); },
    (p) => { p.wind_mps[4] = -1; }, (p) => { p.wind_mps[4] = 76; }, (p) => { p.temperature_c[4] = null; },
    (p) => { p.temperature_c[4] = "10"; }, (p) => { p.now_epoch += 0.25; }, (p) => { p.fetched_epoch = p.now_epoch + 1; },
    (p) => { p.configuration = -1; }, (p) => { p.start_epoch -= 34 * 3600; }, (p) => { p.enabled = false; },
    (p) => { p.configured_location = "é".repeat(33); }, (p) => { p.location = "é".repeat(64); },
  ];
  for (const change of variants) { const p = payload(); change(p); assert.throws(() => normalizeWeatherForecast(p)); }
});

test("invalid current does not discard a valid temperature/wind forecast", () => {
  const p = payload(); p.current = { source: "model", valid: false, time_epoch: 0, temperature_c: null, relative_humidity_pct: null, dew_point_c: null };
  assert.equal(normalizeWeatherForecast(p).valid, true);
});

test("model dew point has its own source, model-time and physical bounds", () => {
  const variants = [(p) => { p.current.source = "indoor"; }, (p) => { p.current.time_epoch = p.now_epoch + 900; },
    (p) => { p.current.time_epoch -= 2700; }, (p) => { p.current.time_epoch += 1; },
    (p) => { p.current.dew_point_c = 11; }, (p) => { p.current.dew_point_c = 36; },
    (p) => { p.current.relative_humidity_pct = 101; }, (p) => { p.current.temperature_c = NaN; }];
  for (const change of variants) { const p = payload(); change(p); assert.throws(() => normalizeWeatherForecast(p)); }
});

test("cached data expires monotonically and a fetch never rejuvenates model time", () => {
  const p = payload(); p.now_epoch += 1800; p.fetched_epoch = p.now_epoch;
  const data = normalizeWeatherForecast(p, 100000, 5000);
  assert.equal(weatherCurrentModelIsCurrent(data, 100000, 5000), true);
  assert.equal(weatherCurrentModelIsCurrent(data, 101000, 6000), false);
  assert.equal(weatherForecastIsCurrent(data, 130001, 35001), false);
  assert.equal(weatherForecastIsCurrent(data, 200000, 5001), false);
  assert.equal(weatherForecastIsCurrent(data, 100000, 4999), false);
});

test("consent, location and pending saves hide cached data immediately", () => {
  const data = normalizeWeatherForecast(payload());
  for (const action of [() => { state.entities.weatherEnabled = entity(false); },
    () => { state.entities.weatherLocation = entity("Utrecht"); }, () => { state.busyAction = "save-weatherLocation"; }]) {
    state.entities.weatherEnabled = entity(true); state.entities.weatherLocation = entity("Brummen"); state.busyAction = "";
    action(); assert.equal(weatherForecastIsCurrent(data), false);
  }
});

test("only the visible enabled learning surface fetches controller data", async () => {
  let calls = 0;
  globalThis.fetch = async (url, options) => { calls++; assert.equal(url, "/openquatt/weather/forecast"); assert.equal(options.cache, "no-store"); return response(payload()); };
  assert.equal(await refreshWeatherForecast(), true);
  assert.equal(await refreshWeatherForecast(), false); // throttle
  assert.equal(calls, 1);
  const cached = state.weatherForecast;
  state.appView = "settings"; state.settingsGroup = "cooling"; assert.equal(await refreshWeatherForecast({ force: true }), false);
  assert.equal(state.weatherForecast, cached); assert.equal(calls, 1);
  state.appView = "learning"; state.learningPanel = "forecast"; document.hidden = true;
  assert.equal(await refreshWeatherForecast({ force: true }), false); assert.equal(calls, 1);
});

test("single-flight and revision guards reject an old response after A to B to A", async () => {
  let resolve, calls = 0;
  globalThis.fetch = async () => { calls++; return new Promise((r) => { resolve = r; }); };
  const first = refreshWeatherForecast(); const second = refreshWeatherForecast();
  invalidateWeatherForecast(); state.entities.weatherLocation = entity("Utrecht");
  invalidateWeatherForecast(); state.entities.weatherLocation = entity("Brummen");
  resolve(response(payload()));
  assert.equal(await first, false); assert.equal(await second, false);
  assert.equal(calls, 1); assert.equal(state.weatherForecast, null);
});

test("late JSON body cannot repopulate a surface that was left", async () => {
  let body;
  globalThis.fetch = async () => ({ ok: true, status: 200, json: () => new Promise((r) => { body = r; }) });
  const pending = refreshWeatherForecast(); await Promise.resolve();
  state.appView = "overview"; body(payload());
  assert.equal(await pending, false); assert.equal(state.weatherForecast, null);
});

test("transient HTTP failures retain the last snapshot, while denied or unsupported responses clear it", async () => {
  for (const status of [400, 401, 403, 408, 429, 500, 503, 404]) {
    invalidateWeatherForecast(); const cached = state.weatherForecast = normalizeWeatherForecast(payload());
    globalThis.fetch = async () => response({}, status);
    assert.equal(await refreshWeatherForecast({ force: true }), false);
    assert.equal(state.weatherForecast, [408, 429, 500, 503].includes(status) ? cached : null);
    assert.equal(state.weatherForecastError, status === 404 ? "unsupported" : "failed");
    assert.equal(weatherCurrentModelIsCurrent(state.weatherForecast), false);
  }
});

test("request timeout covers receiving the body and releases single flight", async () => {
  window.setTimeout = (callback) => setTimeout(callback, 5);
  globalThis.fetch = async (_url, options) => ({ ok: true, status: 200,
    json: () => new Promise((_resolve, reject) => options.signal.addEventListener("abort", () => reject(new Error("aborted")), { once: true })) });
  assert.equal(await refreshWeatherForecast(), false);
  assert.equal(state.weatherForecastError, "failed");
  globalThis.fetch = async () => response(payload());
  assert.equal(await refreshWeatherForecast({ force: true }), true);
});

test("chart axes handle flat, negative and zero wind with separate units", () => {
  for (const values of [Array(36).fill(10), Array(36).fill(-40), Array(36).fill(0)]) {
    const model = getWeatherChartModel(values, "temperature", 360);
    assert.ok(model.upper > model.low);
    for (const value of values) assert.ok(Number.isFinite(model.y(value)));
    assert.equal(model.x(0), 42); assert.equal(model.x(35), 344);
  }
  assert.deepEqual(getWeatherChartModel([0, 3.5], "wind").ticks, [6, 4, 2, 0]);
  assert.deepEqual(getWeatherChartModel([0, 276, null], "solar").ticks, [300, 200, 100, 0]);
  assert.ok(getWeatherChartModel([0, 75], "wind").upper >= 75 / 0.76);
});

test("curves stay within consecutive hourly values and the solar fill stops at missing hours", () => {
  const values = [0, 0, 120, 200, 0, null, 300, 0];
  const model = getWeatherChartModel(values, "solar");
  const { line, fill } = weatherChartPaths(values, model);
  assert.equal((line.match(/M/g) || []).length, 2);
  assert.equal((fill.match(/Z/g) || []).length, 2);
  for (const match of line.matchAll(/[MC]([\d.]+) ([\d.]+)(?: ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+))?/g)) {
    const [, x, y, , y2, endX, endY] = match;
    if (!endY) continue;
    const previous = Math.round((Number(endX) - model.x(0)) / (model.x(1) - model.x(0))) - 1;
    const endpoints = [model.y(values[previous]), Number(endY)];
    for (const control of [y, y2]) {
      assert.ok(Number(control) >= Math.min(...endpoints) - .01);
      assert.ok(Number(control) <= Math.max(...endpoints) + .01);
    }
    assert.ok(Number(x) < Number(endX));
  }
});

test("graphs and accessible hourly table localize and escape external labels", () => {
  const p = payload(); p.location = '<img src=x onerror="bad()">';
  state.weatherForecast = normalizeWeatherForecast(p);
  let html = renderWeatherForecast();
  assert.equal((html.match(/<figure /g) || []).length, 2); assert.equal((html.match(/<tbody>/g) || []).length, 1);
  assert.equal((html.match(/<th scope="row">/g) || []).length, 36);
  assert.doesNotMatch(html, /oq-weather-wind-overlay/);
  assert.doesNotMatch(html, /data-oq-weather-now|>Nu<\/span>/);
  assert.match(html, /Geselecteerd uur/);
  const minutes = String(new Date(epoch * 1000).getMinutes()).padStart(2, "0");
  for (const hour of ["00", "06", "12", "18"]) assert.ok(html.includes(`>${hour}:${minutes}</span>`));
  assert.match(html, /Buitentemperatuur/); assert.match(html, /m\/s/); assert.doesNotMatch(html, /Binnenbron gaat voor/);
  assert.ok(!html.includes('<img src=x')); assert.ok(html.includes('&lt;img'));
  setLocale("en", { persist: false, applyDocument: false, notify: false });
  html = renderWeatherForecast(); assert.match(html, /Outdoor temperature/); assert.match(html, /View hourly values/);
});

test("optional wind direction and solar display in both languages, with zero distinct from unavailable data", () => {
  const p = payload();
  p.wind_mps[0] = 0;
  p.wind_direction_deg = Array(36).fill(90);
  p.wind_direction_deg[2] = 361;
  const localTicks = Array.from({ length: 36 }, (_, i) => i).filter(i => i > 0 && i < 35 && new Date((epoch + i * 3600) * 1000).getHours() % 6 === 0);
  const [omitted, faint, northwest] = localTicks.filter(index => index > 2);
  p.wind_direction_deg[omitted] = null;
  p.wind_mps[faint] = 0.6;
  p.wind_direction_deg[northwest] = 315;
  p.solar_wm2 = [0, 135, "0", -1, ...Array(32).fill(null)];
  state.weatherForecast = normalizeWeatherForecast(p);
  assert.deepEqual(state.weatherForecast.solar_wm2.slice(0, 4), [0, 135, null, null]);
  assert.equal(state.weatherForecast.wind_direction_deg[2], null);
  const html = renderWeatherForecast();
  assert.match(html, /data-oq-weather-graph="solar"/);
  assert.match(html, /Windrichting/);
  assert.doesNotMatch(html, new RegExp(`data-oq-wind-hour="${omitted}"`));
  assert.match(html, new RegExp(`data-oq-wind-hour="${faint}" data-calm="true"`));
  assert.match(html, new RegExp(`data-oq-wind-hour="${northwest}"[^<]*<svg[^>]*135deg`));
  assert.match(html, /Pijl wijst waarheen de wind waait/);
  assert.match(html, /<td>—<\/td>\s*<td>0<\/td>/);
  assert.match(html, /<td>O 90°<\/td>\s*<td>135<\/td>/);
  setLocale("en", { persist: false, applyDocument: false, notify: false });
  assert.match(renderWeatherForecast(), /Solar irradiance/);
  assert.match(renderWeatherForecast(), /Arrow points where the wind is blowing/);
  assert.match(renderWeatherForecast(), /<td>E 90°<\/td>/);
  state.weatherForecast = normalizeWeatherForecast({ ...p, solar_wm2: [0] });
  assert.doesNotMatch(renderWeatherForecast(), /data-oq-weather-graph="solar"/);
});

test("a partial patch preserves disclosure, focus and table scroll without touching location input", () => {
  let focused = false;
  const summary = { focus: (options) => { focused = options.preventScroll; } };
  const details = { open: true }, table = { scrollLeft: 75 };
  const node = { innerHTML: "old", querySelector: (selector) => selector === "details" ? details : selector === "summary" ? summary : table };
  state.root = { querySelector: (selector) => selector === "[data-oq-weather-forecast]" ? node : null };
  document.activeElement = summary;
  assert.equal(patchWeatherForecast(), true); assert.equal(details.open, true); assert.equal(table.scrollLeft, 75); assert.equal(focused, true);
});


test("observed external location and consent transitions invalidate pending responses", async () => {
  const { mergeEntityPayload } = await import("../js/src/core/entity-sync.js");
  for (const key of ["weatherLocation", "weatherEnabled"]) {
    invalidateWeatherForecast(); let resolve;
    globalThis.fetch = async () => new Promise((r) => { resolve = r; });
    const old = refreshWeatherForecast({ force: true });
    const initial = state.entities[key];
    const other = key === "weatherLocation" ? "Utrecht" : false;
    state.entities[key] = mergeEntityPayload(key, state.entities[key], entity(other));
    state.entities[key] = mergeEntityPayload(key, state.entities[key], initial);
    resolve(response(payload()));
    assert.equal(await old, false); assert.equal(state.weatherForecast, null);
  }
});

test("response-body time counts against model freshness at the 1800-second boundary", async () => {
  const originalPerformance = globalThis.performance, originalNow = Date.now;
  let mono = 5000, wall = 100000;
  globalThis.performance = { now: () => mono }; Date.now = () => wall;
  try {
    const p = payload(); p.now_epoch += 1800; p.fetched_epoch = p.now_epoch;
    globalThis.fetch = async () => ({ status: 200, ok: true, json: async () => { mono += 7000; wall += 7000; return p; } });
    assert.equal(await refreshWeatherForecast({ force: true }), true);
    assert.equal(state.weatherForecast.receivedMono, 5000);
    assert.equal(weatherCurrentModelIsCurrent(state.weatherForecast), false);
    assert.match(renderWeatherForecast(), /Actueel modeldauwpunt niet beschikbaar/);
  } finally { globalThis.performance = originalPerformance; Date.now = originalNow; }
});


test("unchanged SVG data does not replace live nodes after HTML serialization", () => {
  state.weatherForecast = normalizeWeatherForecast(payload());
  let writes = 0;
  const node = { get innerHTML() { return "browser-normalized SVG"; }, set innerHTML(_value) { writes++; }, querySelector: () => null };
  state.root = { querySelector: () => node };
  assert.equal(patchWeatherForecast(), true); assert.equal(patchWeatherForecast(), true);
  assert.equal(writes, 1);
});


test("tab suspension keeps displayable forecast while current dew point still requires a fresh response", async () => {
  const cached = normalizeWeatherForecast(payload(), Date.now() - 61000, performance.now() - 61000);
  state.weatherForecast = cached;
  assert.equal(weatherForecastCanDisplay(cached), true);
  assert.equal(weatherForecastIsCurrent(cached), false);
  suspendWeatherForecast();
  document.hidden = true;
  assert.equal(await refreshWeatherForecast({ force: true }), false);
  assert.equal(state.weatherForecast, cached);
  document.hidden = false;
  let resolve;
  globalThis.fetch = async () => new Promise((r) => { resolve = r; });
  const refresh = refreshWeatherForecast({ force: true });
  assert.match(renderWeatherForecast(), /<figure /);
  assert.equal(state.weatherForecast, cached);
  resolve(response(payload()));
  assert.equal(await refresh, true);
  assert.notEqual(state.weatherForecast, cached);
  assert.equal(weatherCurrentModelIsCurrent(state.weatherForecast), true);
  const expired = normalizeWeatherForecast(payload(), Date.now() - 10800001, performance.now() - 10800001);
  assert.equal(weatherForecastCanDisplay(expired), false);
  state.entities.weatherEnabled = entity(false);
  assert.equal(weatherForecastCanDisplay(cached), false);
});

test("hourly disclosure action survives a complete panel replacement and an invalidated forecast", async () => {
  const { handleViewAction } = await import("../js/src/features/view-actions.js");
  state.weatherForecast = normalizeWeatherForecast(payload());
  const details = { open: false };
  const button = { dataset: {}, closest: () => details };
  assert.equal(handleViewAction("toggle-weather-hourly", button, { preventDefault() {} }), true);
  assert.equal(details.open, true);
  assert.match(renderWeatherForecast(), /class="oq-weather-hourly" open/);
  invalidateWeatherForecast();
  state.weatherForecast = normalizeWeatherForecast(payload());
  assert.match(renderWeatherForecast(), /class="oq-weather-hourly" open/);
  handleViewAction("toggle-weather-hourly", button, { preventDefault() {} });
  assert.equal(details.open, false);
  assert.doesNotMatch(renderWeatherForecast(), /class="oq-weather-hourly" open/);
});

function mountWeatherCharts(metrics = ["temperature", "wind"]) {
  const readout = { textContent: "", set innerHTML(html) { this.markup = html; this.textContent = html.replace(/<[^>]+>/g, ""); } };
  const figures = metrics.map((metric) => {
    const attrs = {}, pointAttrs = {}, cursorAttrs = {};
    const svg = { dataset: { oqWeatherGraph: metric }, viewBox: { baseVal: { width: 422 } },
      getScreenCTM: () => ({ a: 2, d: 3, e: 90 }), setAttribute: (key, value) => { attrs[key] = value; } };
    const cursor = { setAttribute: (key, value) => { cursorAttrs[key] = value; },
      removeAttribute: (key) => { delete cursorAttrs[key]; },
      querySelector: () => ({ setAttribute: (key, value) => { pointAttrs[key] = value; } }) };
    const highlight = { style: {}, querySelector: () => ({ style: { setProperty: (_, value) => { highlight.angle = value; } } }) };
    const sample = { dataset: { oqWindHour: 0 } };
    const figure = { dataset: { oqWeatherChart: metric }, svg, attrs, pointAttrs, cursorAttrs, highlight, sample,
      querySelector: (selector) => selector === "svg" ? svg : selector === "[data-oq-weather-cursor]" ? cursor : highlight,
      querySelectorAll: () => [sample] };
    return figure;
  });
  const surface = { querySelector: () => readout };
  state.root = { querySelectorAll: () => figures, querySelector: () => surface };
  return { figures, readout };
}

test("pointer coordinates include the cropped plot offset and synchronize both exact-value cursors", () => {
  const p = payload(); p.temperature_c[18] = 11.2; p.wind_mps[18] = 3.4;
  state.weatherForecast = normalizeWeatherForecast(p);
  const { figures, readout } = mountWeatherCharts();
  const svgX = getWeatherChartModel(p.temperature_c).x(18);
  assert.equal(handleWeatherChartPointer({ target: { closest: () => figures[0] }, clientX: svgX * 2 + 90 }), true);
  assert.equal(state.weatherForecastIndex, 18);
  assert.match(readout.textContent, /11,2 °C.*3,4 m\/s/);
  for (const figure of figures) {
    assert.equal(figure.attrs["aria-valuenow"], "18");
    assert.equal(figure.cursorAttrs.transform, `translate(${svgX.toFixed(1)} 0)`);
    assert.ok(Number.isFinite(Number(figure.pointAttrs.cy)));
    assert.equal(figure.pointAttrs.rx, 2);
    assert.equal(figure.pointAttrs.ry, 4 / 3);
  }
  assert.equal(getWeatherPointIndex(-100, 480), 0);
  assert.equal(getWeatherPointIndex(1000, 480), 35);
  state.entities.weatherLocation = entity("Utrecht");
  patchWeatherForecastCursor();
  assert.equal(figures[0].cursorAttrs.hidden, "");
  assert.doesNotMatch(readout.textContent, /11,2/);
});

test("the solar chart keeps the selected hour across charts even when its value is unavailable", () => {
  const p = payload();
  p.solar_wm2 = [0, null, ...Array(34).fill(135)];
  p.wind_direction_deg = Array(36).fill(360);
  state.weatherForecast = normalizeWeatherForecast(p);
  const { figures, readout } = mountWeatherCharts(["temperature", "wind", "solar"]);
  const [, wind, solar] = figures;
  const press = (key) => handleWeatherChartKeyDown({ target: solar.svg, key, preventDefault() {} });
  assert.equal(press("Home"), true);
  assert.match(readout.textContent, /uit N \(360°\).*Zoninstraling 0 W\/m²/);
  assert.match(readout.markup, /180deg/);
  assert.equal(wind.highlight.angle, "180deg");
  assert.equal(wind.sample.hidden, true);
  assert.equal(solar.pointAttrs.visibility, "visible");
  assert.equal(press("ArrowRight"), true);
  assert.doesNotMatch(readout.textContent, /Zoninstraling/);
  assert.equal(solar.pointAttrs.visibility, "hidden");
  assert.equal(wind.sample.hidden, false);
  assert.equal(figures[0].cursorAttrs.transform, solar.cursorAttrs.transform);
});

test("keyboard inspection clamps hourly selection and clears stale values", () => {
  state.weatherForecast = normalizeWeatherForecast(payload());
  const { figures, readout } = mountWeatherCharts();
  let prevented = 0;
  const key = (value) => handleWeatherChartKeyDown({ key: value, target: figures[1].svg, preventDefault: () => { prevented++; } });
  key("End"); key("ArrowRight"); assert.equal(state.weatherForecastIndex, 35);
  key("Home"); key("ArrowLeft"); assert.equal(state.weatherForecastIndex, 0);
  key("ArrowRight"); assert.equal(state.weatherForecastIndex, 1);
  assert.match(readout.textContent, /10 °C/);
  key("Escape"); assert.equal(state.weatherForecastIndex, null);
  assert.equal(figures[1].cursorAttrs.hidden, "");
  assert.equal(prevented, 6);
});

test("responsive plot changes keep a round cursor and disconnect the old surface", () => {
  state.weatherForecast = normalizeWeatherForecast(payload());
  state.weatherForecastIndex = 18;
  const { figures } = mountWeatherCharts();
  let scale = 1;
  for (const figure of figures) figure.svg.getScreenCTM = () => ({ a: scale, d: 1 });
  patchWeatherForecastCursor();
  assert.ok(observed);
  assert.equal(figures[0].pointAttrs.rx, 4);
  scale = 2; resized();
  assert.equal(figures[0].pointAttrs.rx * scale, 4);
  assert.equal(figures[0].pointAttrs.ry, 4);
  state.root = null; resized();
  assert.equal(observed, null);
});


test("full settings render preserves the graph keyboard target through the shared focus helper", async () => {
  const { captureSettingsFocusContinuity, restoreSettingsFocusContinuity } = await import("../js/src/core/settings-focus-continuity.js");
  state.weatherForecast = normalizeWeatherForecast(payload());
  const html = renderWeatherForecast();
  for (const metric of ["temperature", "wind"]) {
    const focusKey = html.match(new RegExp(`data-oq-weather-graph="${metric}" data-oq-focus-key="([^"]+)"`))?.[1];
    const original = { dataset: { oqFocusKey: focusKey }, closest: () => null };
    const root = { contains: (node) => node === original };
    const saved = captureSettingsFocusContinuity(root, "learning", { activeElement: original });
    assert.ok(saved, "SVG must participate in complete learning-render continuity");
    let focused = false;
    const replacement = { disabled: false, focus: (options) => { focused = options.preventScroll; } };
    restoreSettingsFocusContinuity({ querySelector: (selector) => {
      assert.equal(selector, `[data-oq-focus-key="weather-${metric}"]`); return replacement;
    } }, saved, { activeElement: null });
    assert.equal(focused, true);
  }
});
