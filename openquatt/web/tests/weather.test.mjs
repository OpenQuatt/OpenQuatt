import assert from "node:assert/strict";
import test from "node:test";

const nativeSetTimeout = globalThis.setTimeout;
const nativeClearTimeout = globalThis.clearTimeout;
const nativeFetch = globalThis.fetch;
const nativeNow = Date.now;

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false, activeElement: null };
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: nativeSetTimeout,
  clearTimeout: nativeClearTimeout,
  localStorage: { getItem: () => null },
  sessionStorage: { getItem: () => null },
};

const { state } = await import("../js/src/core/state.js");
const { normalizeWeatherLocation, confirmWeatherLocationWrite, saveWeatherLocation } = await import("../js/src/core/weather-location.js");
const { commitWeatherLocation } = await import("../js/src/features/weather.js");
const { commitText } = await import("../js/src/core/entity-write-actions.js");
const { SETTINGS_BACKUP_EXCLUDED_KEYS } = await import("../js/src/core/config.js");
const { hasWeatherSettings, renderWeatherSettings, patchWeatherSettings } = await import("../js/src/settings/weather.js");
const { normalizeWeatherForecast } = await import("../js/src/core/weather-forecast.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
const { setLocale } = await import("../js/src/i18n/index.js");

const weatherMarkup = () => renderWeatherSettings();

function entity(value) { return { value, state: value }; }
const TOKEN = "0123456789abcdef0123456789abcdef";
function stored(value, token = TOKEN) { return { ...entity(value), save_request: token, save_status: "stored" }; }

function capability(location = "Brummen") {
  return { houseLearningEnabled: entity(true), weatherLocation: entity(location), weatherEnabled: entity(false) };
}

function validForecast() {
  const now = Math.floor(Date.now() / 1000);
  state.weatherForecast = normalizeWeatherForecast({ schema: 1, model: "knmi_harmonie_arome_netherlands", enabled: true, valid: true,
    configured_location: "Brummen", location: "Brummen", now_epoch: now, fetched_epoch: now - 60,
    generation: 1, configuration: 1, start_epoch: Math.floor(now / 3600) * 3600, step_s: 3600,
    current: { valid: false, time_epoch: 0 }, temperature_c: Array(36).fill(8), wind_mps: Array(36).fill(2) });
}

function response(payload = {}, status = 200) {
  return { ok: status >= 200 && status < 300, status, json: async () => payload };
}

function deferred() {
  let resolve;
  const promise = new Promise((complete) => { resolve = complete; });
  return { promise, resolve };
}

async function fastPolling(run) {
  // Only the reconciliation delays use the global timer; request deadlines use
  // window.setTimeout. Keep the latter real so a test cannot fake a successful IO.
  globalThis.setTimeout = (callback, _delay, ...args) => nativeSetTimeout(callback, 0, ...args);
  try { return await run(); }
  finally { globalThis.setTimeout = nativeSetTimeout; }
}

test.beforeEach(() => {
  state.entities = capability();
  state.weatherForecast = null;
  state.weatherForecastError = "";
  state.inputDrafts = {};
  state.drafts = {};
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.loadingEntities = false;
  state.root = null;
  state.focusedField = null;
  setRenderCallback(null);
  setLocale("nl", { persist: false, applyDocument: false, notify: false });
});

test.afterEach(() => {
  globalThis.fetch = nativeFetch;
  globalThis.setTimeout = nativeSetTimeout;
  Date.now = nativeNow;
  document.activeElement = null;
  state.root = null;
  state.focusedField = null;
  state.busyAction = "";
  setRenderCallback(null);
  setLocale("nl", { persist: false, applyDocument: false, notify: false });
});

test("weather location validation keeps Dutch coordinates and UTF-8 byte limits", () => {
  for (const [value, expected] of [[" Brummen ", "Brummen"], ["52.09000, 6.15556", "52.09000, 6.15556"], ["", ""], ["é".repeat(32), "é".repeat(32)]]) {
    assert.equal(normalizeWeatherLocation(value), expected);
  }
  for (const value of [null, 42, "x", "a".repeat(65), "é".repeat(33), "Brum\nmen", "Brum\u0000men", "https://example.invalid/", "52, 8", "52,", "52,6,7"]) {
    assert.throws(() => normalizeWeatherLocation(value), /plaatsnaam|coördinaten|64 bytes/);
  }
});

test("full postcodes preserve their original letters and spacing through write confirmation", async () => {
  for (const value of ["6971AB", "6971 AB", "6971ab", "6971 ab", "6971aB"]) {
    assert.equal(normalizeWeatherLocation(value), value);
    let posted = "";
    const result = await confirmWeatherLocationWrite(value, {
      requestToken: TOKEN,
      post: async (normalized, token) => { posted = normalized; assert.equal(token, TOKEN); },
      read: async () => stored(value), pollDelays: [0],
    });
    assert.equal(posted, value);
    assert.equal(result.normalized, value);
    assert.equal(result.payload.value, value);
  }
});

test("HTTP acknowledgement without changed Text is not storage confirmation", async () => {
  let posts = 0;
  let reads = 0;
  const waits = [];
  await assert.rejects(confirmWeatherLocationWrite("Utrecht", {
    post: async () => { posts += 1; },
    read: async () => { reads += 1; return entity("Brummen"); },
    pollDelays: [0, 5, 10],
    wait: async (delay) => { waits.push(delay); },
  }), /niet bevestigd/);
  assert.equal(posts, 1);
  assert.equal(reads, 3);
  assert.deepEqual(waits, [5, 10]);
});

test("an equal old location, failed verification or another request never confirms this save", async () => {
  for (const payload of [entity("Brummen"), stored("Brummen", "f".repeat(32)),
    { ...stored("Brummen"), save_status: "pending" }, { ...stored("Brummen"), save_status: "failed" }]) {
    await assert.rejects(confirmWeatherLocationWrite("Brummen", {
      requestToken: TOKEN, post: async () => {}, read: async () => payload, pollDelays: [0],
    }), /niet bevestigd/);
  }
});

test("A to B to A waits for its own committed receipt instead of the unchanged old value", async () => {
  let reads = 0, posts = 0;
  const sequence = [stored("Brummen", "f".repeat(32)), { ...stored("Brummen"), save_status: "pending" }, stored("Brummen")];
  const result = await confirmWeatherLocationWrite("Brummen", {
    requestToken: TOKEN, post: async () => { ++posts; }, read: async () => sequence[reads++],
    pollDelays: [0, 0, 0],
  });
  assert.equal(result.payload, sequence[2]);
  assert.equal(reads, 3);
  assert.equal(posts, 1);
});

test("lost POST acknowledgement succeeds after confirmed persisted readback without another POST", async () => {
  let posts = 0;
  let reads = 0;
  const payload = { ...stored("Brummen"), name: "Weather Location" };
  const result = await confirmWeatherLocationWrite(" Brummen ", {
    requestToken: TOKEN,
    post: async (value) => { posts += 1; assert.equal(value, "Brummen"); throw new Error("lost acknowledgement"); },
    read: async () => { reads += 1; if (reads === 1) throw new Error("temporary read failure"); return payload; },
    pollDelays: [0, 1], wait: async () => {},
  });
  assert.equal(posts, 1);
  assert.equal(reads, 2);
  assert.equal(result.normalized, "Brummen");
  assert.equal(result.payload, payload);
});

test("failed or malformed GET cannot refresh stale location and a lost write remains uncertain", async () => {
  for (const postFails of [false, true]) {
    let posts = 0;
    let reads = 0;
    const stale = [null, {}, { value: 0 }, entity("Brummen")];
    await assert.rejects(confirmWeatherLocationWrite("Zutphen", {
      post: async () => { posts += 1; if (postFails) throw new Error("lost POST"); },
      read: async () => { reads += 1; if (reads === 1) throw new Error("offline"); return stale[reads - 2]; },
      pollDelays: [0, 0, 0, 0, 0],
    }), postFails ? /onzeker/ : /niet bevestigd/);
    assert.equal(posts, 1);
    assert.equal(reads, 5);
  }
});

test("real request wrapper performs one encoded POST and uncached verified GET", async () => {
  const calls = [];
  globalThis.fetch = async (url, init) => {
    calls.push({ url: String(url), init });
    return response(init.method === "POST" ? {} : stored("Brummen, Gelderland"));
  };
  assert.equal((await saveWeatherLocation("Brummen, Gelderland", { pollDelays: [0], requestToken: TOKEN })).normalized, "Brummen, Gelderland");
  assert.equal(calls.length, 2);
  assert.equal(calls[0].init.method, "POST");
  assert.equal(calls[0].url, `/text/Weather%20Location/set?value=Brummen%2C%20Gelderland&save_request=${TOKEN}`);
  assert.equal(calls[1].init.cache, "no-store");
  assert.equal(calls[1].init.headers["Cache-Control"], "no-store");
  assert.ok(calls.every(({ init }) => init.signal instanceof AbortSignal));
});

test("unsuccessful read HTTP statuses do not confirm a successful POST", async () => {
  const methods = [];
  globalThis.fetch = async (_url, init) => {
    methods.push(init.method || "GET");
    return response(entity("Utrecht"), init.method === "POST" ? 200 : 503);
  };
  await assert.rejects(saveWeatherLocation("Utrecht", { pollDelays: [0, 0, 0] }), /niet bevestigd/);
  assert.deepEqual(methods, ["POST", "GET", "GET", "GET"]);
});

test("failed confirmation preserves raw draft, committed entity and actionable error", async () => {
  const methods = [];
  globalThis.fetch = async (_url, init) => {
    methods.push(init.method || "GET");
    return response(init.method === "POST" ? {} : entity("Brummen"));
  };
  assert.equal(await fastPolling(() => commitWeatherLocation(" Zutphen ")), false);
  assert.equal(methods.filter((method) => method === "POST").length, 1);
  assert.equal(state.entities.weatherLocation.value, "Brummen");
  assert.equal(state.inputDrafts.weatherLocation, " Zutphen ");
  assert.match(state.controlError, /niet.*bevestigd|niet worden bevestigd/);
  assert.equal(state.controlNotice, "");
  assert.equal(state.busyAction, "");
  assert.match(weatherMarkup(), /value=" Zutphen "/);
});

test("duplicate actions cannot launch another write while acknowledgement is pending", async () => {
  const pending = deferred();
  let posts = 0;
  let token;
  globalThis.fetch = async (url, init) => {
    if (init.method === "POST") { posts += 1; token = new URL(url, "http://device").searchParams.get("save_request"); return pending.promise; }
    return response(stored("Utrecht", token));
  };
  const first = commitWeatherLocation("Utrecht");
  assert.equal(state.busyAction, "save-weatherLocation");
  assert.equal(await commitWeatherLocation("Zutphen"), false);
  assert.equal(posts, 1);
  pending.resolve(response());
  assert.equal(await first, true);
  assert.equal(state.entities.weatherLocation.value, "Utrecht");
  assert.equal(state.busyAction, "");
});

test("confirmation of an earlier write preserves a newer local draft", async () => {
  const pending = deferred();
  let token;
  globalThis.fetch = async (url, init) => {
    if (init.method === "POST") { token = new URL(url, "http://device").searchParams.get("save_request"); return pending.promise; }
    return response(stored("Utrecht", token));
  };
  const saving = commitWeatherLocation("Utrecht");
  state.inputDrafts.weatherLocation = "Zutphen";
  state.drafts.weatherLocation = "Zutphen";
  pending.resolve(response());
  assert.equal(await saving, true);
  assert.equal(state.entities.weatherLocation.value, "Utrecht");
  assert.equal(state.inputDrafts.weatherLocation, "Zutphen");
  assert.equal(state.drafts.weatherLocation, "Zutphen");
  assert.match(weatherMarkup(), /value="Zutphen"/);
});

test("missing capability hides the panel and refuses writes rather than inventing an entity", async () => {
  for (const entities of [{}, { weatherLocation: entity("Brummen") }, { weatherEnabled: entity(false) }]) {
    state.entities = entities;
    let calls = 0;
    globalThis.fetch = async (_url, init) => { calls += 1; return response(init.method === "POST" ? {} : entity("Utrecht")); };
    assert.equal(hasWeatherSettings(), false);
    assert.equal(weatherMarkup(), "");
    assert.equal(await commitWeatherLocation("Utrecht"), false);
    assert.equal(calls, 0);
    assert.deepEqual(state.entities, entities);
  }
});

test("generic Text action uses persisted readback and backups omit location and consent", async () => {
  let posts = 0;
  let token;
  globalThis.fetch = async (url, init) => {
    if (init.method === "POST") { posts += 1; token = new URL(url, "http://device").searchParams.get("save_request"); throw new Error("acknowledgement lost"); }
    return response(stored("Zutphen", token));
  };
  assert.equal(await commitText("weatherLocation", "Zutphen"), true);
  assert.equal(state.entities.weatherLocation.value, "Zutphen");
  assert.equal(posts, 1, "one POST per operation");
  assert.ok(SETTINGS_BACKUP_EXCLUDED_KEYS.has("weatherEnabled"));
  assert.ok(SETTINGS_BACKUP_EXCLUDED_KEYS.has("weatherLocation"));
});

test("weather UI localizes NL and EN without translating stored location or wire values", () => {
  state.entities.weatherEnabled = entity(true);
  state.entities.weatherStatus = entity("location_ambiguous");
  state.entities.weatherResolvedLocation = entity("Brummen (52.09000, 6.15556)");
  const nl = weatherMarkup();
  assert.match(nl, /Vooruitblik/);
  assert.match(nl, /Meerdere plaatsen gevonden/);
  assert.match(nl, /Bij inschakelen:[^<]*Open-Meteo/);
  setLocale("en", { persist: false, applyDocument: false, notify: false });
  const en = weatherMarkup();
  assert.match(en, /Outlook/);
  assert.match(en, /Multiple places found/);
  assert.match(en, /Enabling shares your location with Open-Meteo/);
  assert.match(en, /value="Brummen"/);
  assert.equal(state.entities.weatherStatus.value, "location_ambiguous");
  assert.equal(state.entities.weatherLocation.value, "Brummen");
});

test("hostile statuses remain internal and location text is escaped for HTML and input attributes", () => {
  state.entities.weatherEnabled = entity(true);
  const hostile = '\"><img src=x onerror="alert(1)"><script>bad()</script>';
  state.inputDrafts.weatherLocation = hostile;
  state.entities.weatherResolvedLocation = entity(hostile);
  for (const locale of ["nl", "en"]) {
    setLocale(locale, { persist: false, applyDocument: false, notify: false });
    for (const status of [hostile, "__proto__", "constructor", "toString"]) {
      state.entities.weatherStatus = entity(status);
      const markup = weatherMarkup();
      assert.doesNotMatch(markup, /<img|<script/);
      assert.match(markup, /&quot;&gt;&lt;img/);
      assert.match(markup, /&lt;script&gt;bad\(\)&lt;\/script&gt;/);
      assert.doesNotMatch(markup, /value=""\s*>\s*<img/);
      assert.match(markup, locale === "nl" ? /Wacht op status van de controller/ : /Waiting for controller status/);
    }
  }
});

test("diagnostic patch changes no location input, focus, selection or draft and skips identical markup", () => {
  state.entities.weatherEnabled = entity(true);
  state.entities.weatherStatus = entity("waiting_for_weather");
  state.inputDrafts.weatherLocation = "Brummen, Gelderland";
  const input = { value: "Brummen, Gelderland", selectionStart: 3, selectionEnd: 9 };
  document.activeElement = input;
  state.focusedField = "weatherLocation";
  let markup = "";
  let writes = 0;
  const diagnostics = { querySelector: () => ({}), get innerHTML() { return markup; }, set innerHTML(value) { markup = value; writes += 1; } };
  const toggle = { setAttribute() {}, dataset: {}, classList: { toggle() {} } };
  const pill = { classList: { toggle() {} } };
  const nodes = { "[data-oq-weather-diagnostics]": diagnostics, '[data-oq-field="weatherLocation"]': input,
    '[data-control-key="weatherEnabled"]': toggle, '[data-oq-switch-pill="weatherEnabled"]': pill };
  const panel = {
    querySelector: (selector) => nodes[selector],
    set innerHTML(_value) { assert.fail("patch must not replace the panel containing the focused input"); },
  };
  state.root = { querySelector: (selector) => selector === "[data-oq-weather-settings]" ? panel : nodes[selector] };
  assert.equal(patchWeatherSettings(), true);
  assert.equal(patchWeatherSettings(), true);
  assert.equal(writes, 1);
  state.entities.weatherStatus = entity("observing");
  assert.equal(patchWeatherSettings(), true);
  assert.equal(writes, 2);
  assert.equal(document.activeElement, input);
  assert.equal(input.value, "Brummen, Gelderland");
  assert.equal(input.selectionStart, 3);
  assert.equal(input.selectionEnd, 9);
  assert.equal(state.inputDrafts.weatherLocation, "Brummen, Gelderland");
  setLocale("en", { persist: false, applyDocument: false, notify: false });
  assert.equal(patchWeatherSettings(), true);
  assert.equal(writes, 3);
  assert.match(markup, /constant heat delivery/);
  assert.doesNotMatch(markup, /Calculating and comparing|oq-settings-action-note/);
});

test("capability changes require a structural render rather than leaving a stale weather panel", () => {
  state.root = { querySelector: () => null };
  assert.equal(patchWeatherSettings(), false);
  state.root = { querySelector: () => ({}) };
  state.entities = {};
  assert.equal(patchWeatherSettings(), false);
  state.root = { querySelector: () => null };
  assert.equal(patchWeatherSettings(), false);
});
