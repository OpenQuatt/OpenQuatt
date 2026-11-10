import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false, activeElement: null };
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null }, setTimeout, clearTimeout };

const { state } = await import("../js/src/core/state.js");
const { commitNumber, commitSwitch, commitText } = await import("../js/src/core/entity-write-actions.js");
const { refreshEntities } = await import("../js/src/core/entity-sync.js");
const { patchLearningDom } = await import("../js/src/core/view-patch-controls.js");
const { shouldRefreshWeatherForecast } = await import("../js/src/features/weather-forecast.js");
const { shouldRefreshHouseLearningStatusSurface } = await import("../js/src/features/house-learning.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const { renderLearningView } = await import("../js/src/views/learning.js");
const { patchWeatherOutlook } = await import("../js/src/settings/weather.js");

const ok = payload => ({ ok: true, status: 200, json: async () => payload });
function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return { promise, resolve };
}

test.beforeEach(t => {
  state.entities = { weatherEnabled: { value: true, state: "ON" }, weatherLocation: { value: "Brummen", state: "Brummen" } };
  state.busyAction = state.controlError = state.controlNotice = "";
  state.weatherConfigRevision = 0;
  state.weatherOptOutUnconfirmed = false;
  state.learningPanel = "configuration";
  state.loadingEntities = false;
  state.nativeOpen = false;
  state.drafts = {};
  state.inputDrafts = {};
  state.root = null;
  state.deviceReconnectMode = "";
  t.mock.method(globalThis, "fetch", () => { throw new Error("unexpected request"); });
});
test.afterEach(() => { state.root = null; document.activeElement = null; });

test("late weerbevestiging kan een nieuwere nummerwrite niet afronden of diens feedback vervangen", async t => {
  const read = deferred(), reading = deferred(), write = deferred(), writing = deferred();
  t.mock.method(globalThis, "fetch", async (url, options = {}) => {
    if (url === "/openquatt/entities") { reading.resolve(); return read.promise; }
    if (url.startsWith("/number/")) { writing.resolve(); return write.promise; }
    assert.equal(options.method, "POST", "een ingehaalde weeractie mag geen nieuwe forecastrequest starten");
    return ok({});
  });
  const first = commitSwitch("weatherEnabled", false);
  await reading.promise;
  const second = commitNumber("maxWater", 45);
  await writing.promise;
  read.resolve(ok({ entities: { weatherEnabled: { value: false, state: "OFF" },
    weatherStatus: { value: "disabled", state: "disabled", consent_state: "off" } }, missing: [] }));
  await first;
  assert.equal(state.busyAction, "save-maxWater");
  assert.equal(state.controlNotice, "");
  assert.equal(state.controlError, "");
  assert.equal(state.entities.weatherEnabled.value, true, "de oude body wordt niet alsnog gepubliceerd");
  assert.equal(state.weatherOptOutUnconfirmed, true, "onbevestigde opt-out mag weerpolling niet hervatten");
  write.resolve({ ok: false, status: 500 });
  await second;
  assert.match(state.controlError, /HTTP 500/);
});

for (const newerAction of ["pending", "success", "failure", "validation"]) {
  test(`late locatiebevestiging behoudt een nieuwere ${newerAction}-actie`, async t => {
    state.appView = "settings";
    state.entities.boilerSupportStopThreshold = { value: 20 };
    const read = deferred(), reading = deferred(), write = deferred(), writing = deferred();
    let token;
    let posts = 0;
    t.mock.method(globalThis, "fetch", async (url, options = {}) => {
      if (url.startsWith("/text/")) {
        if (options.method === "POST") {
          posts += 1;
          token = new URL(url, "http://fixture").searchParams.get("save_request");
          return ok({});
        }
        reading.resolve();
        return read.promise;
      }
      if (url.startsWith("/number/")) {
        writing.resolve();
        return newerAction === "pending" ? write.promise : { ok: newerAction !== "failure", status: newerAction === "failure" ? 500 : 200 };
      }
      if (url === "/openquatt/entities") {
        const keys = new URLSearchParams(options.body).get("entities").split("\n").map(line => line.split("\t")[0]);
        return ok({ entities: Object.fromEntries(keys.map(key => [key, state.entities[key] || { value: 1, state: "1" }])), missing: [] });
      }
      return ok({ entities: {} });
    });
    const first = commitText("weatherLocation", "Utrecht");
    await reading.promise;
    const second = newerAction === "validation"
      ? commitNumber("boilerSupportStartThreshold", 10)
      : commitNumber("maxWater", 45);
    if (newerAction === "pending") await writing.promise;
    else await second;
    const feedback = { notice: state.controlNotice, error: state.controlError };
    read.resolve(ok({ value: "Utrecht", state: "Utrecht", save_request: token, save_status: "stored" }));
    assert.equal(await first, false);
    assert.deepEqual({ notice: state.controlNotice, error: state.controlError }, feedback);
    assert.equal(state.entities.weatherLocation.value, "Brummen");
    assert.equal(state.inputDrafts.weatherLocation, "Utrecht");
    assert.equal(state.busyAction, newerAction === "pending" ? "save-maxWater" : "");
    assert.equal(posts, 1, "de locatie-POST wordt nooit herhaald");
    if (newerAction === "pending") { write.resolve({ ok: false, status: 500 }); await second; }
  });
}

test("late locatiefout behoudt een nieuwere validatiefout en ruimt alleen de eigen busy-marker op", async t => {
  state.appView = "settings";
  state.entities.boilerSupportStopThreshold = { value: 20 };
  const post = deferred(), posting = deferred();
  let requests = 0;
  t.mock.method(globalThis, "fetch", () => { requests += 1; posting.resolve(); return post.promise; });
  const first = commitText("weatherLocation", "Utrecht");
  await posting.promise;
  await commitNumber("boilerSupportStartThreshold", 10);
  const error = state.controlError;
  post.resolve({ ok: false, status: 400 });
  assert.equal(await first, false);
  assert.equal(state.controlError, error);
  assert.equal(state.controlNotice, "");
  assert.equal(state.busyAction, "");
  assert.equal(requests, 1);
});

test("locatie A kan de busy-marker en draft van nieuwere locatie C niet wissen", async t => {
  state.appView = "settings";
  const readA = deferred(), readingA = deferred(), postC = deferred(), postingC = deferred();
  const tokens = [];
  t.mock.method(globalThis, "fetch", async (url, options = {}) => {
    if (url.startsWith("/number/")) return { ok: false, status: 500 };
    assert.ok(url.startsWith("/text/"));
    if (options.method === "POST") {
      tokens.push(new URL(url, "http://fixture").searchParams.get("save_request"));
      if (tokens.length === 2) { postingC.resolve(); return postC.promise; }
      return ok({});
    }
    if (tokens.length === 1) { readingA.resolve(); return readA.promise; }
    return ok({ value: "Zutphen", state: "Zutphen", save_request: tokens[1], save_status: "stored" });
  });
  const first = commitText("weatherLocation", "Utrecht");
  await readingA.promise;
  await commitNumber("maxWater", 45);
  const third = commitText("weatherLocation", "Zutphen");
  await postingC.promise;
  readA.resolve(ok({ value: "Utrecht", state: "Utrecht", save_request: tokens[0], save_status: "stored" }));
  assert.equal(await first, false);
  assert.equal(state.busyAction, "save-weatherLocation");
  assert.equal(state.inputDrafts.weatherLocation, "Zutphen");
  assert.equal(state.entities.weatherLocation.value, "Brummen");
  assert.equal(state.controlNotice, "");
  assert.equal(state.controlError, "");
  postC.resolve(ok({}));
  assert.equal(await third, true);
  assert.equal(state.entities.weatherLocation.value, "Zutphen");
  assert.equal(state.busyAction, "");
  assert.equal(tokens.length, 2);
});

test("een bulkpoll van vóór een locatieactie wist geen nieuwe weerspecifieke fout", async t => {
  const read = deferred(), reading = deferred();
  t.mock.method(globalThis, "fetch", () => { reading.resolve(); return read.promise; });
  state.controlError = "locatie niet bevestigd";
  const poll = refreshEntities(["weatherEnabled"]);
  await reading.promise;
  state.weatherConfigRevision += 1;
  state.controlError = "locatie niet bevestigd";
  read.resolve(ok({ entities: { weatherEnabled: { value: true, state: "ON" } }, missing: [] }));
  await poll;
  assert.equal(state.controlError, "locatie niet bevestigd");
});

test("de Leren-patch bewaart Vooruitblikbediening en toont tegelijk actuele meldingen als toast", () => {
  state.appView = "learning";
  const input = { value: "Brummen", selectionStart: 1, selectionEnd: 4 };
  document.activeElement = input;
  // Stands in for the mounted toast stack.
  const added = [];
  const stack = { style: { setProperty() {} }, classList: { toggle() {} }, replaceChildren() {},
    insertAdjacentHTML: (_position, markup) => added.push(markup), querySelector: () => null };
  const toggle = { setAttribute() {}, dataset: {}, classList: { toggle() {} } };
  const pill = { classList: { toggle() {} } };
  const panel = { querySelector: selector => ({ '[data-oq-field="weatherLocation"]': input,
    '[data-control-key="weatherEnabled"]': toggle, '[data-oq-switch-pill="weatherEnabled"]': pill })[selector] };
  state.root = { scrollTop: 200, querySelector: selector => ({ "[data-oq-feedback-stack]": stack,
    "[data-oq-learning-panels]": {}, "[data-oq-weather-settings]": panel })[selector] };
  state.controlError = "Bewaren <mislukt>";
  const markup = renderLearningView();
  assert.match(markup, /data-oq-feedback-stack/);
  assert.match(markup, /data-learning-panel="configuration" aria-current="page"/);
  assert.equal(patchLearningDom(), true);
  assert.match(added.join(""), /role="alert".*Bewaren &lt;mislukt&gt;/);
  assert.equal(document.activeElement, input);
  assert.equal(input.selectionStart, 1);
  assert.equal(input.selectionEnd, 4);
  assert.equal(state.root.scrollTop, 200);
  delete state.entities.weatherEnabled;
  assert.equal(patchLearningDom(), false, "veranderde tabcapability vereist een structurele render");
});


test("vijf leeronderdelen scheiden uitleg, dagelijks gebruik, instellingen en diagnose in beide talen", () => {
  state.appView = "learning";
  state.entities.houseLearningEnabled = { value: true, state: "ON" };
  state.entities.houseLearningReset = {};
  state.houseLearningEndpointAvailable = state.houseLearningEndpointChecked = true;
  state.houseLearningStatus = null;
  state.entities.weatherStatus = { value: "observing", state: "observing" };
  for (const locale of ["nl", "en"]) {
    setLocale(locale, { persist: false, applyDocument: false, notify: false });
    const titles = locale === "nl"
      ? ["Leren en voorspellen", "Huismodel volgen", "Vooruitblik", "Instellingen", "Diagnose en data"]
      : ["Learning and forecasting", "Monitor home model", "Outlook", "Settings", "Diagnostics and data"];
    const badge = locale === "nl" ? "Experimenteel" : "Experimental";
    for (const [index, panel] of ["explanation", "measurements", "forecast", "configuration", "diagnostics"].entries()) {
      state.learningPanel = panel;
      const markup = renderLearningView();
      const heading = markup.match(/<header class="oq-settings-section-head oq-learning-page-head">([\s\S]*?)<\/header>/)?.[1];
      assert.match(heading ?? "", new RegExp(String.raw`<h3>${titles[index]}</h3>\s*<span[^>]+>${badge}</span>`));
      assert.equal((markup.match(/oq-settings-section-badge--experimental/g) || []).length, 1);
      assert.equal((markup.match(/data-learning-panel=/g) || []).length, 5);
      assert.match(markup, new RegExp(`data-learning-panel="${panel}" aria-current="page"`));
      if (panel === "configuration") assert.doesNotMatch(markup, /Een nieuwe berekening|A new calculation|data-oq-learning-scenario|<table/);
      assert.equal(markup.includes('data-oq-field="weatherLocation"'), panel === "configuration");
      assert.equal(markup.includes('data-control-key="houseLearningEnabled"'), panel === "configuration");
      assert.equal(markup.includes('data-oq-button-key="houseLearningReset"'), panel === "configuration");
      assert.equal(markup.includes('data-oq-action="download-house-learning"'), panel === "diagnostics");
      if (panel === "diagnostics") {
        assert.doesNotMatch(markup, /data-oq-forecast-quality/);
        assert.match(markup, /data-oq-house-learning-secondary[\s\S]*data-oq-house-learning-shadow[\s\S]*data-oq-house-learning-technical[\s\S]*data-oq-house-learning-actions/);
      }
      const href = markup.match(new RegExp('href="([^"]+)"[^>]*data-learning-panel="' + panel + '"'))?.[1];
      assert.ok(href);
      const url = new URL(href.replaceAll("&amp;", "&"), "http://fixture/");
      assert.equal(url.searchParams.get("view"), "learning");
      assert.equal(url.searchParams.get("section"), panel === "measurements" ? null : panel);
      assert.equal(markup.includes("oq-learning-explanation"), panel === "explanation");
      assert.equal(markup.includes('data-oq-learning-scenario'), panel === "forecast");
      assert.equal(markup.includes('data-oq-house-learning-chart-panel'), panel === "measurements");
      assert.equal(shouldRefreshWeatherForecast(), panel === "forecast");
      assert.equal(shouldRefreshHouseLearningStatusSurface(), !["explanation", "forecast"].includes(panel));
    }
    document.hidden = true;
    assert.equal(shouldRefreshWeatherForecast(), false);
    assert.equal(shouldRefreshHouseLearningStatusSurface(), false);
    document.hidden = false;
  }
  setLocale("nl", { persist: false, applyDocument: false, notify: false });
});

test("Vooruitblik wisselt tussen de uitmelding en de prognose zonder fouten te verbergen", () => {
  state.appView = "learning";
  state.learningPanel = "forecast";
  state.entities.weatherEnabled = { value: false, state: "OFF" };
  state.entities.weatherStatus = { value: "disabled", consent_state: "off" };
  const markup = renderLearningView();
  assert.match(markup, /Weersverwachting staat uit/);
  assert.match(markup, /href="[^"]*section=configuration"\s+data-oq-action="select-learning-panel"[^>]*>Naar weerinstellingen/);
  assert.doesNotMatch(markup, /data-oq-learning-scenario|data-oq-weather-forecast/);

  state.entities.weatherStatus.consent_state = "failed";
  const error = renderLearningView();
  assert.match(error, /kon niet worden opgeslagen/);
  assert.doesNotMatch(error, /Zet de weersverwachting aan/);

  const panel = { dataset: { oqWeatherOutlook: "off" } };
  state.root = { querySelector: selector => selector === "[data-oq-weather-outlook]" ? panel : null };
  assert.equal(patchWeatherOutlook(), true);
  state.entities.weatherEnabled = { value: true, state: "ON" };
  state.entities.weatherStatus.consent_state = "on";
  assert.equal(patchWeatherOutlook(), false);
  assert.match(renderLearningView(), /data-oq-learning-scenario[\s\S]*data-oq-weather-forecast/);
  panel.dataset.oqWeatherOutlook = "";
  assert.equal(patchWeatherOutlook(), true);
  state.entities.weatherEnabled = { value: false, state: "OFF" };
  assert.equal(patchWeatherOutlook(), false);
});
