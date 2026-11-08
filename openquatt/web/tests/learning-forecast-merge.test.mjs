import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false, activeElement: null };
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null }, setTimeout, clearTimeout };

const { state } = await import("../js/src/core/state.js");
const { commitNumber, commitSwitch, commitText } = await import("../js/src/core/entity-write-actions.js");
const { refreshEntities } = await import("../js/src/core/entity-sync.js");
const { patchLearningDom } = await import("../js/src/core/view-patch-controls.js");
const { renderLearningView } = await import("../js/src/views/learning.js");

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
  state.learningForecastOpen = true;
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
  assert.match(markup, /data-learning-panel="forecast" aria-pressed="true"/);
  assert.equal(patchLearningDom(), true);
  assert.match(added.join(""), /role="alert".*Bewaren &lt;mislukt&gt;/);
  assert.equal(document.activeElement, input);
  assert.equal(input.selectionStart, 1);
  assert.equal(input.selectionEnd, 4);
  assert.equal(state.root.scrollTop, 200);
  delete state.entities.weatherEnabled;
  assert.equal(patchLearningDom(), false, "veranderde tabcapability vereist een structurele render");
});
