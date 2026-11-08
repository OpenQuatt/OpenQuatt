import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false, activeElement: null };
globalThis.window = {
  location: { pathname: "/" },
  localStorage: { getItem: () => null },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
};

const { state } = await import("../js/src/core/state.js");
const { refreshEntities } = await import("../js/src/core/entity-sync.js");
const { commitDateTime, commitNumber, commitSelect, commitSwitch, commitText } = await import("../js/src/core/entity-write-actions.js");
const { patchLearningDom } = await import("../js/src/core/view-patch-controls.js");
const { renderLearningView } = await import("../js/src/views/learning.js");

function response(payload, status = 200) {
  return { ok: status === 200, status, json: async () => payload };
}

function bulkResponse(body) {
  const entities = {};
  for (const line of new URLSearchParams(body).get("entities").split("\n")) {
    const key = line.split("\t")[0];
    entities[key] = state.entities[key] ? { ...state.entities[key] } : { value: 1, state: "1" };
  }
  return response({ entities, missing: [] });
}

function learningStatus(enabled = false) {
  return { schema: 1, mode: "passive", enabled, status: "paused", records: 0, tick_epoch: Math.floor(Date.now() / 1000) };
}

function readback(value = false, extra = {}) {
  return { entities: { houseLearningEnabled: { value, state: value ? "ON" : "OFF" } }, missing: [], ...extra };
}

function resetState() {
  state.appView = "learning";
  state.entities = {};
  state.drafts = {};
  state.inputDrafts = {};
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.nativeOpen = false;
  state.loadingEntities = false;
  state.root = null;
  state.deviceReconnectMode = "";
  state.entitySyncFailureCount = 0;
  state.optionalMissingEntities = {};
  state.houseLearningRequestId = 0;
  state.houseLearningFetchPromise = null;
  state.houseLearningLastFetchAt = 0;
  state.houseLearningStatus = null;
  state.houseLearningStatusError = "";
  state.houseLearningEndpointAvailable = true;
  state.houseLearningEndpointChecked = true;
  state.houseLearningEndpointChecking = false;
  state.houseLearningReset = "";
}

test.beforeEach(t => {
  resetState();
  t.mock.method(globalThis, "fetch", () => { throw new Error("unexpected unmocked request"); });
});
test.afterEach(() => {
  state.root = null;
  document.activeElement = null;
});

// Advance only the monotonic confirmation clock. Request timers fire explicitly;
// no test waits for a real controller or a real 20-second timeout.
function confirmationClock(t) {
  let now = 0;
  let nextId = 0;
  const timers = new Map();
  t.mock.method(performance, "now", () => now);
  t.mock.method(globalThis, "setTimeout", (callback, delay) => {
    now += delay;
    queueMicrotask(callback);
    return ++nextId;
  });
  t.mock.method(window, "setTimeout", (callback, delay) => {
    const id = ++nextId;
    timers.set(id, { callback, delay });
    return id;
  });
  t.mock.method(window, "clearTimeout", id => timers.delete(id));
  return {
    advance(ms) { now += ms; },
    get now() { return now; },
    expire() {
      const [id, timer] = [...timers].at(-1);
      timers.delete(id);
      now += timer.delay;
      queueMicrotask(timer.callback);
    },
  };
}

test("leerschakelaar bevestigt een trage verse bulkbody en schrijft eenmaal", async t => {
  const clock = confirmationClock(t);
  state.entities.houseLearningEnabled = { value: true, state: "ON" };
  let writes = 0;
  let reads = 0;
  t.mock.method(globalThis, "fetch", async url => {
    if (url === "/auth/status") return response({}); // Change-log logging; not a setting write.
    if (url === "/openquatt/entities") {
      reads += 1;
      return { ok: true, json: async () => { clock.advance(3500); return readback(); } };
    }
    if (url === "/openquatt/learning/status") return response(learningStatus());
    writes += 1;
    return response({});
  });
  await commitSwitch("houseLearningEnabled", false);
  assert.equal(writes, 1);
  assert.equal(reads, 1);
  assert.equal(state.entities.houseLearningEnabled.value, false);
  assert.equal(state.controlError, "");
  assert.match(state.controlNotice, /uitgeschakeld/);
});

test("een eerste read-timeout kan binnen dezelfde bevestigingsdeadline herstellen", async t => {
  const clock = confirmationClock(t);
  state.entities.houseLearningEnabled = { value: true, state: "ON" };
  let reads = 0;
  let writes = 0;
  t.mock.method(globalThis, "fetch", async (url, { signal } = {}) => {
    if (url === "/auth/status") return response({}); // Change-log logging; not a setting write.
    if (url === "/openquatt/entities") {
      if (++reads === 1) return { ok: true, json: () => new Promise((_resolve, reject) => {
        signal.addEventListener("abort", () => reject(signal.reason), { once: true });
        clock.expire();
      }) };
      return response(readback());
    }
    if (url === "/openquatt/learning/status") return response(learningStatus());
    writes += 1;
    return response({});
  });
  await commitSwitch("houseLearningEnabled", false);
  assert.equal(writes, 1);
  assert.equal(reads, 2);
  assert.equal(clock.now, 8250);
  assert.equal(state.controlError, "");
});

test("stilvallende readbodies respecteren de totale 20s en publiceren geen succes", async t => {
  const clock = confirmationClock(t);
  state.entities.houseLearningEnabled = { value: true, state: "ON" };
  let reads = 0;
  t.mock.method(globalThis, "fetch", async (url, { signal } = {}) => {
    if (url !== "/openquatt/entities") return response(url.includes("status") ? learningStatus() : {});
    reads += 1;
    return { ok: true, json: () => new Promise((_resolve, reject) => {
      signal.addEventListener("abort", () => reject(signal.reason), { once: true });
      clock.expire();
    }) };
  });
  await commitSwitch("houseLearningEnabled", false);
  assert.equal(clock.now, 20000);
  assert.equal(reads, 3);
  assert.equal(state.busyAction, "");
  assert.equal(state.controlNotice, "");
  assert.equal(state.entities.houseLearningEnabled.value, true);
  assert.match(state.controlError, /nog niet bevestigd/);
});

test("een goede response na de absolute deadline wordt niet bevestigd", async t => {
  const clock = confirmationClock(t);
  state.entities.houseLearningEnabled = { value: true, state: "ON" };
  t.mock.method(globalThis, "fetch", async url => {
    if (url === "/openquatt/entities") {
      clock.advance(20000);
      return response(readback());
    }
    return response(url.includes("status") ? learningStatus() : {});
  });
  await commitSwitch("houseLearningEnabled", false);
  assert.equal(state.entities.houseLearningEnabled.value, true);
  assert.equal(state.controlNotice, "");
  assert.match(state.controlError, /nog niet bevestigd/);
});

for (const [label, payload] of [
  ["ontbrekend veld", { entities: {}, missing: ["houseLearningEnabled"] }],
  ["tegenstrijdige missing", readback(false, { missing: ["houseLearningEnabled"] })],
  ["ongeldige bool", { entities: { houseLearningEnabled: { value: null, state: "OFF" } }, missing: [] }],
  ["tegenstrijdige state", { entities: { houseLearningEnabled: { value: false, state: "ON" } }, missing: [] }],
  ["ontbrekende missing-array", readback(false, { missing: undefined })],
]) {
  test(`leerschakelaar blijft onbevestigd bij ${label}`, async t => {
    confirmationClock(t);
    state.entities.houseLearningEnabled = { value: true, state: "ON" };
    t.mock.method(globalThis, "fetch", async url => response(url === "/openquatt/entities" ? payload : url.includes("status") ? learningStatus() : {}));
    await commitSwitch("houseLearningEnabled", false);
    assert.equal(state.entities.houseLearningEnabled.value, true);
    assert.equal(state.controlNotice, "");
    assert.match(state.controlError, /nog niet bevestigd/);
  });
}

test("een ingehaalde leerschakelaar-readback publiceert de oude stand niet", async t => {
  confirmationClock(t);
  state.entities.houseLearningEnabled = { value: true, state: "ON" };
  t.mock.method(globalThis, "fetch", async url => {
    if (url === "/openquatt/entities") {
      state.houseLearningRequestId += 1;
      return response(readback());
    }
    return response(url.includes("status") ? learningStatus() : {});
  });
  await commitSwitch("houseLearningEnabled", false);
  assert.equal(state.entities.houseLearningEnabled.value, true);
  assert.equal(state.controlNotice, "");
});

test("aanvoerbron gebruikt dezelfde trage, begrensde bevestiging zonder tweede write", async t => {
  const clock = confirmationClock(t);
  let writes = 0;
  let reads = 0;
  t.mock.method(globalThis, "fetch", async (url, options = {}) => {
    if (url === "/auth/status") return response({}); // Change-log logging; not a setting write.
    if (url === "/openquatt/entities") return bulkResponse(options.body);
    if (options.method === "POST") { writes += 1; return response({}); }
    reads += 1;
    clock.advance(3500);
    return response({ value: "Local", state: "Local" });
  });
  await commitSelect("waterSupplySource", "Local");
  assert.equal(writes, 1);
  assert.equal(reads, 1);
  assert.equal(state.entities.waterSupplySource.value, "Local");
  assert.equal(state.controlError, "");
});

test("aanvoerbron blijft onbevestigd als de responsebody ontbreekt", async t => {
  confirmationClock(t);
  state.entities.waterSupplySource = { value: "Home Assistant", state: "Home Assistant" };
  let writes = 0;
  t.mock.method(globalThis, "fetch", async (_url, options = {}) => {
    if (options.method === "POST") writes += 1;
    return response({});
  });
  await commitSelect("waterSupplySource", "Local");
  assert.equal(writes, 1);
  assert.equal(state.entities.waterSupplySource.value, "Home Assistant");
  assert.equal(state.controlNotice, "");
  assert.match(state.controlError, /niet worden bevestigd/);
});

for (const [label, action] of [
  ["number", () => commitNumber("maxWater", 45)],
  ["select", () => commitSelect("strategy", "Power House")],
  ["text", () => commitText("waterSupplyHaInputCalibrationIdentity", "woning")],
  ["datetime", () => commitDateTime("openquattResumeAt", "2026-11-07T18:30")],
]) {
  test(`${label}-write behoudt een waarschuwing bij onvolledige readback`, async t => {
    t.mock.method(globalThis, "fetch", async url => {
      if (url === "/openquatt/entities") throw new Error("readback unavailable");
      return response({});
    });
    await action();
    assert.match(state.controlError, /readback unavailable/);
    assert.notEqual(state.controlNotice, "");
    assert.equal(state.busyAction, "");
  });
}

test("telemetriewrite verbergt geen mislukte statusread achter een cached consentstand", async t => {
  confirmationClock(t);
  state.entities.usageTelemetryEnabled = { value: true, state: "ON" };
  state.entities.usageTelemetryChoiceConfigured = { value: true, state: "ON" };
  t.mock.method(globalThis, "fetch", async url => {
    if (url === "/openquatt/entities") throw new Error("readback unavailable");
    return response({});
  });
  await commitSwitch("usageTelemetryEnabled", true);
  assert.match(state.controlError, /readback unavailable/);
  assert.equal(state.busyAction, "");
});

for (const succeeds of [false, true]) {
  test(`een oude ${succeeds ? "goede" : "foute"} poll wist of vervangt geen nieuwe identieke actiefout`, async t => {
    state.entities.boilerSupportStopThreshold = { value: 20 };
    await commitNumber("boilerSupportStartThreshold", 10);
    const validationError = state.controlError;
    assert.notEqual(validationError, "");
    let complete;
    const started = new Promise(resolve => {
      t.mock.method(globalThis, "fetch", () => { resolve(); return new Promise(done => { complete = done; }); });
    });
    const poll = refreshEntities(["maxWater"]);
    await started;
    // Same message string after a distinct action: comparing strings alone is not a fence.
    await commitNumber("boilerSupportStartThreshold", 10);
    complete(succeeds ? response({ entities: { maxWater: { value: 45 } }, missing: [] }) : response({}, 500));
    await poll;
    assert.equal(state.controlError, validationError);
  });
}

test("de leerschakelaar houdt de eigen waarschuwing gescheiden van parallelle bulkreads", async t => {
  state.busyAction = "switch-houseLearningEnabled";
  state.controlError = "nieuwe leerstand nog niet bevestigd";
  t.mock.method(globalThis, "fetch", async () => response({}, 500));
  await refreshEntities(["maxWater"]);
  assert.equal(state.controlError, "nieuwe leerstand nog niet bevestigd");
});

for (const key of ["maxWater", "warmupStatus"]) {
  test(`een write-refresh van ${key} bevestigt velden zonder OTA/reconnect-zijeffecten`, async t => {
    state.busyAction = "save-maxWater";
    state.lastEntitySyncSuccessAt = 123;
    state.controlError = "eerdere leeswaarschuwing";
    t.mock.method(globalThis, "fetch", async () => response({ entities: { [key]: { value: 45, state: "45" } }, missing: [] }));
    await refreshEntities([key]);
    assert.equal(state.lastEntitySyncSuccessAt, 123, "de bestaande busy-gate voor refresh-success blijft behouden");
    assert.equal(state.controlError, "", "een eigen geslaagde read mag de leeswaarschuwing wel wissen");
  });
}

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((done, fail) => { resolve = done; reject = fail; });
  return { promise, resolve, reject };
}

for (const owner of ["learning", "supply"]) {
  for (const newerAction of ["pending", "success", "failure", "validation"]) {
    test(`${owner}-bevestiging na een nieuwere ${newerAction}-actie behoudt diens feedback en busy-eigendom`, async t => {
      confirmationClock(t);
      state.entities.houseLearningEnabled = { value: true, state: "ON" };
      state.entities.waterSupplySource = { value: "Home Assistant", state: "Home Assistant" };
      state.entities.boilerSupportStopThreshold = { value: 20 };
      const readStarted = deferred();
      const heldRead = deferred();
      const writeStarted = deferred();
      const heldWrite = deferred();
      let firstRead = true;
      let statusReads = 0;
      let newerWrites = 0;
      t.mock.method(globalThis, "fetch", async (url, options = {}) => {
        const ownerRead = owner === "learning" ? url === "/openquatt/entities" : options.method !== "POST" && url.startsWith("/select/");
        if (ownerRead && firstRead) {
          firstRead = false;
          readStarted.resolve();
          return heldRead.promise;
        }
        if (url.startsWith("/number/")) {
          newerWrites += 1;
          writeStarted.resolve();
          if (newerAction === "pending") return heldWrite.promise;
          return response({}, newerAction === "failure" ? 500 : 200);
        }
        if (url === "/openquatt/entities") return bulkResponse(options.body);
        if (url === "/openquatt/learning/status") { statusReads += 1; return response(learningStatus()); }
        return response({ entities: {} });
      });
      const first = owner === "learning" ? commitSwitch("houseLearningEnabled", false) : commitSelect("waterSupplySource", "Local");
      await readStarted.promise;
      state.appView = "settings";
      const second = newerAction === "validation"
        ? commitNumber("boilerSupportStartThreshold", 10)
        : commitNumber("maxWater", 45);
      if (newerAction === "pending") await writeStarted.promise;
      else await second;
      const feedback = { notice: state.controlNotice, error: state.controlError };
      heldRead.resolve(response(owner === "learning" ? readback() : { value: "Local", state: "Local" }));
      await first;
      assert.deepEqual({ notice: state.controlNotice, error: state.controlError }, feedback);
      assert.equal(state.busyAction, newerAction === "pending" ? "save-maxWater" : "");
      assert.equal(statusReads, 0, "een ingehaalde actie start geen nieuwe statusread");
      assert.equal(newerWrites, newerAction === "validation" ? 0 : 1);
      if (newerAction === "pending") {
        heldWrite.resolve(response({}, 500));
        await second;
        assert.match(state.controlError, /HTTP 500/);
      }
    });
  }

  test(`${owner}-bevestiging met late leesfout vervangt geen nieuwere validatiefout`, async t => {
    confirmationClock(t);
    state.entities.houseLearningEnabled = { value: true, state: "ON" };
    state.entities.waterSupplySource = { value: "Home Assistant", state: "Home Assistant" };
    state.entities.boilerSupportStopThreshold = { value: 20 };
    const readStarted = deferred();
    const heldRead = deferred();
    t.mock.method(globalThis, "fetch", async (url, options = {}) => {
      if (url === "/openquatt/entities" || options.method !== "POST") {
        readStarted.resolve();
        return heldRead.promise;
      }
      return response({});
    });
    const first = owner === "learning" ? commitSwitch("houseLearningEnabled", false) : commitSelect("waterSupplySource", "Local");
    await readStarted.promise;
    await commitNumber("boilerSupportStartThreshold", 10);
    const error = state.controlError;
    heldRead.reject(new Error("late read failure"));
    await first;
    assert.equal(state.controlError, error);
    assert.equal(state.controlNotice, "");
    assert.equal(state.busyAction, "");
  });
}

test("achtergrondsync patcht Leren-meldingen zonder focus of de omringende DOM te vervangen", () => {
  state.entities.houseLearningEnabled = { value: false, state: "OFF" };
  const focus = { id: "grafiekknop" };
  document.activeElement = focus;
  let messageWrites = 0;
  let markup = "";
  const messages = {
    get innerHTML() { return markup; },
    set innerHTML(value) { markup = value; messageWrites += 1; },
  };
  const status = { innerHTML: "" };
  const panel = { querySelector: selector => selector === "[data-oq-house-learning-status]" ? status : null };
  state.root = {
    scrollTop: 412,
    querySelector: selector => selector === "[data-oq-learning-messages]" ? messages : selector === "[data-oq-house-learning]" ? panel : null,
    set innerHTML(_value) { assert.fail("de hele pagina mag niet worden vervangen"); },
  };
  state.controlError = "Schrijven <mislukt>";
  assert.equal(patchLearningDom(), true);
  assert.match(markup, /role="alert".*Schrijven &lt;mislukt&gt;/);
  assert.equal(document.activeElement, focus);
  assert.equal(state.root.scrollTop, 412);
  patchLearningDom();
  assert.equal(messageWrites, 1);
  state.controlError = "";
  state.controlNotice = "Opnieuw bevestigd";
  patchLearningDom();
  assert.match(markup, /role="status".*Opnieuw bevestigd/);
  assert.doesNotMatch(markup, /role="alert"/);
  state.controlNotice = "";
  patchLearningDom();
  assert.doesNotMatch(markup, /role=/);
  assert.equal(document.activeElement, focus);
  assert.equal(state.root.scrollTop, 412);
  assert.match(renderLearningView(), /data-oq-learning-messages/);
});

test("ontbrekende lokale bannercontainer vraagt de bestaande volledige renderfallback", () => {
  state.root = { querySelector: () => null };
  assert.equal(patchLearningDom(), false);
});
