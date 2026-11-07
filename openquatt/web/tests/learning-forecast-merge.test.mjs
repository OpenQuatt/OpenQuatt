import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false, activeElement: null };
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null }, setTimeout, clearTimeout };

const { state } = await import("../js/src/core/state.js");
const { commitNumber, commitSwitch } = await import("../js/src/core/entity-write-actions.js");
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

test("de Leren-patch bewaart Vooruitblikbediening en patcht tegelijk actuele meldingen", () => {
  const input = { value: "Brummen", selectionStart: 1, selectionEnd: 4 };
  document.activeElement = input;
  const messages = { innerHTML: "" };
  const toggle = { setAttribute() {}, dataset: {}, classList: { toggle() {} } };
  const pill = { classList: { toggle() {} } };
  const panel = { querySelector: selector => ({ '[data-oq-field="weatherLocation"]': input,
    '[data-control-key="weatherEnabled"]': toggle, '[data-oq-switch-pill="weatherEnabled"]': pill })[selector] };
  state.root = { scrollTop: 200, querySelector: selector => ({ "[data-oq-learning-messages]": messages,
    "[data-oq-learning-panels]": {}, "[data-oq-weather-settings]": panel })[selector] };
  state.controlError = "Bewaren <mislukt>";
  const markup = renderLearningView();
  assert.match(markup, /data-oq-learning-messages/);
  assert.match(markup, /data-learning-panel="forecast" aria-pressed="true"/);
  assert.equal(patchLearningDom(), true);
  assert.match(messages.innerHTML, /Bewaren &lt;mislukt&gt;/);
  assert.equal(document.activeElement, input);
  assert.equal(input.selectionStart, 1);
  assert.equal(input.selectionEnd, 4);
  assert.equal(state.root.scrollTop, 200);
  delete state.entities.weatherEnabled;
  assert.equal(patchLearningDom(), false, "veranderde tabcapability vereist een structurele render");
});
