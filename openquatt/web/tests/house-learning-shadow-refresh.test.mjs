import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.document = { hidden: false };
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null }, setTimeout, clearTimeout };

const { invalidateHouseLearningShadow, state } = await import("../js/src/core/state.js");
const { refreshHouseLearningStatus, resetHouseLearningData } = await import("../js/src/features/house-learning.js");
const { setAppView } = await import("../js/src/core/navigation.js");
const { commitNumber, commitSwitch } = await import("../js/src/core/entity-write-actions.js");
const { setViewPatchControls } = await import("../js/src/core/view-patch-controls.js");

const ok = payload => ({ ok: true, status: 200, json: async () => payload });
const flush = () => new Promise(resolve => setImmediate(resolve));
function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return { promise, resolve };
}

function ready(ttl = 4000) {
  return { schema: 1, mode: "passive", enabled: true, status: "collecting", storage_ready: true,
    journal_status: "saved_verified", records: 12, context_revision: 8, auto_apply_allowed: false,
    batch_advice_ready: true, shadow_ttl_ms: ttl,
    shadow: { schema: 1, semantics: "one_step_from_observed_state", scope: "base_before_start_stop_dispatch",
      source: "actual", state_origin: "live_pre_tick", run_id: null, context_revision: 8, tick_ms: 34000,
      available: true, reason: "ready", manual_w: 3100, candidate_w: 2750, delta_w: -350, auto_apply_allowed: false } };
}

function clock(t) {
  let now = 40000, nextId = 0;
  const timers = new Map();
  t.mock.method(performance, "now", () => now);
  t.mock.method(Date, "now", () => 1_800_000_000_000 + now);
  t.mock.method(window, "setTimeout", (callback, delay) => {
    const id = ++nextId;
    timers.set(id, { at: now + delay, callback });
    return id;
  });
  t.mock.method(window, "clearTimeout", id => timers.delete(id));
  return {
    get now() { return now; },
    get pending() { return timers.size; },
    async to(target) {
      let count = 0;
      for (;;) {
        const entry = [...timers].filter(([, timer]) => timer.at <= target).sort((a, b) => a[1].at - b[1].at)[0];
        if (!entry) break;
        assert.ok(++count < 100, "geen onbeperkte timer/requestlus");
        const [id, timer] = entry;
        timers.delete(id);
        now = timer.at;
        timer.callback();
        await flush();
      }
      now = target;
      await flush();
    },
  };
}

test.beforeEach(t => {
  state.appView = "learning";
  state.learningForecastOpen = false;
  state.nativeOpen = false;
  document.hidden = false;
  state.busyAction = state.houseLearningReset = "";
  state.entities = { houseLearningEnabled: { value: true, state: "ON" } };
  state.houseLearningStatus = null;
  state.houseLearningStatusError = "";
  state.houseLearningEndpointAvailable = state.houseLearningEndpointChecked = true;
  state.houseLearningEndpointChecking = false;
  state.houseLearningFetchPromise = null;
  state.houseLearningLastFetchAt = 0;
  state.houseLearningRequestId += 1;
  state.houseLearningShadowEpoch += 1;
  state.root = { querySelector: selector => selector === "[data-oq-house-learning]" ? {} : null };
  setViewPatchControls({ patchLearningDom: () => true });
  t.mock.method(globalThis, "fetch", () => { throw new Error("unexpected unmocked request"); });
});
test.afterEach(() => { invalidateHouseLearningShadow(); state.root = null; });

test("poll40 behoudt TTL44, volgt op44.2 en verschuift normale poll50 niet", async t => {
  const time = clock(t);
  let reads = 0, patches = 0;
  setViewPatchControls({ patchLearningDom: () => { patches += 1; return true; } });
  t.mock.method(globalThis, "fetch", async () => {
    reads += 1;
    const frame = time.now < 44000 ? 34000 : 44000 + Math.floor((time.now - 44000) / 10000) * 10000;
    return ok({ ...ready(frame + 10000 - time.now), shadow: { ...ready().shadow, tick_ms: frame } });
  });
  await refreshHouseLearningStatus();
  assert.equal(state.houseLearningStatus.shadow.expiresAt, 44000);
  await time.to(44199);
  assert.equal(reads, 1);
  assert.equal(state.houseLearningStatus.shadow.expiresAt, 44000, "oude watts krijgen geen langere TTL");
  await time.to(44200);
  assert.equal(reads, 2);
  assert.equal(state.houseLearningStatus.shadow.expiresAt, 54000);
  assert.equal(patches, 1);
  assert.equal(time.pending, 1);
  await time.to(50000);
  await refreshHouseLearningStatus();
  assert.equal(reads, 3, "de vervolgopvraag stelt de gewone fallback niet uit");
  assert.equal(time.pending, 1, "de oude expiry-timer is vervangen");
  await time.to(54200);
  assert.equal(reads, 4, "slechts één opvraag op de volgende expiry");
});

for (const shortTtl of [false, true]) {
  test(`${shortTtl ? "een steeds zeer korte TTL" : "aanhoudend server-expired"} blijft bij één follow-up plus twee retries`, async t => {
    const time = clock(t);
    let reads = 0;
    t.mock.method(globalThis, "fetch", async () => { reads += 1; return ok(shortTtl ? ready(1) : { ...ready(), shadow_expired: true }); });
    await refreshHouseLearningStatus();
    await time.to(49000);
    assert.equal(reads, 4);
    assert.equal(time.pending, 0);
    await time.to(50000);
    await refreshHouseLearningStatus();
    assert.equal(reads, 5, "alleen de gewone poll begint een nieuwe begrensde poging");
  });
}

for (const [name, payload] of [
  ["legacy", { ...ready(), shadow: undefined }],
  ["ongeldige shadow", { ...ready(), shadow_ttl_ms: 0 }],
  ["inactief", { ...ready(), enabled: false }],
  ["advies ontbreekt", { ...ready(), batch_advice_ready: false }],
  ["onbekend formaat", { schema: 2 }],
]) {
  test(`${name} start geen expiry-polling`, async t => {
    const time = clock(t);
    let reads = 0;
    t.mock.method(globalThis, "fetch", async () => { reads += 1; return ok(payload); });
    await refreshHouseLearningStatus();
    await time.to(49000);
    assert.equal(reads, 1);
    assert.equal(time.pending, 0);
  });
}

for (const [name, invalidate] of [
  ["navigatie", () => { setAppView("settings"); setAppView("learning"); }],
  ["verborgen", () => { document.hidden = true; }],
  ["native", () => { state.nativeOpen = true; }],
  ["Vooruitblik", () => { state.learningForecastOpen = true; }],
  ["write", () => { state.busyAction = "save-maxWater"; }],
  ["reset", () => { state.houseLearningReset = "pending"; }],
  ["onzekere reset", () => { state.houseLearningReset = "uncertain"; }],
  ["andere requestgeneratie", () => { state.houseLearningRequestId += 1; }],
  ["verwijderde status", () => { state.houseLearningStatus = null; }],
]) {
  test(`${name} sluit de geplande vervolgopvraag`, async t => {
    const time = clock(t);
    let reads = 0;
    t.mock.method(globalThis, "fetch", async () => { reads += 1; return ok(ready()); });
    await refreshHouseLearningStatus();
    invalidate();
    await time.to(49000);
    assert.equal(reads, 1);
    assert.equal(time.pending, 0);
  });
}

test("terugnavigatie tijdens de vervolgbody kan geen watts herstellen", async t => {
  const time = clock(t), body = deferred();
  let reads = 0;
  t.mock.method(globalThis, "fetch", async () => ++reads === 2 ? { ok: true, json: () => body.promise } : ok(ready()));
  await refreshHouseLearningStatus();
  await time.to(44200);
  setAppView("settings"); setAppView("learning");
  body.resolve(ready(10000));
  await flush();
  assert.equal(state.houseLearningStatus.shadow, null);
  assert.equal(state.houseLearningLastFetchAt, 0);
  assert.equal(time.pending, 0);
  await refreshHouseLearningStatus();
  assert.equal(reads, 3);
  assert.equal(state.houseLearningStatus.shadow.available, true);
});

test("een body tijdens een andere schrijfactie publiceert geen nieuwe watts", async t => {
  const time = clock(t), body = deferred();
  let reads = 0;
  t.mock.method(globalThis, "fetch", async () => ++reads === 2 ? { ok: true, json: () => body.promise } : ok(ready()));
  await refreshHouseLearningStatus();
  await time.to(44200);
  state.busyAction = "save-maxWater";
  body.resolve(ready(10000));
  await flush();
  assert.equal(state.houseLearningStatus.shadow, null);
  assert.equal(time.pending, 0);
});

for (const duringBody of [false, true]) {
  test(`een afgeronde PH-write ${duringBody ? "tijdens de vervolgbody" : "vóór de geplande timer"} houdt oude watts ongeldig`, async t => {
    const time = clock(t), body = deferred();
    let reads = 0, writes = 0, bulkReads = 0;
    state.entities.phKp = { value: 250, state: "250", min_value: 0, max_value: 1000, step: 1 };
    t.mock.method(globalThis, "fetch", async (url, options = {}) => {
      if (url === "/auth/status") return ok({}); // Change-log logging; not a learning read.
      if (url === "/openquatt/entities") {
        bulkReads += 1;
        const keys = new URLSearchParams(options.body).get("entities").split("\n").map(line => line.split("\t")[0]);
        return ok({ entities: Object.fromEntries(keys.map(key => [key, key === "phKp"
          ? { ...state.entities.phKp, value: 500, state: "500" } : state.entities[key] || { value: 1, state: "1" }])), missing: [] });
      }
      if (options.method === "POST") {
        assert.match(url, /^\/number\/.*\/set\?value=500$/);
        writes += 1;
        return ok({});
      }
      reads += 1;
      return reads === 2 ? { ok: true, json: () => body.promise } : ok(ready());
    });
    await refreshHouseLearningStatus();
    assert.notEqual(state.houseLearningShadowTimer, null);
    if (duringBody) await time.to(44200);
    await commitNumber("phKp", 500);
    assert.equal(state.busyAction, "", "de nieuwere write is volledig afgerond vóór de oude body");
    assert.equal(state.entities.phKp.value, 500);
    assert.equal(state.houseLearningStatus.shadow, null);
    assert.equal(state.houseLearningShadowTimer, null);
    if (duringBody) {
      body.resolve(ready(10000));
      await flush();
    }
    await time.to(49000);
    assert.equal(state.houseLearningStatus.shadow, null);
    assert.equal(state.houseLearningShadowTimer, null);
    assert.equal(reads, duringBody ? 2 : 1);
    assert.equal(writes, 1);
    assert.ok(bulkReads >= 1, "bevestiging door de echte bulkreadback-route");
    assert.equal(time.pending, 0);
  });
}

test("een bekende leerwrite fence't de al uitgegeven vervolgbody", async t => {
  const time = clock(t), body = deferred(), post = deferred();
  let reads = 0;
  t.mock.method(globalThis, "fetch", async (url, options = {}) => {
    if (options.method === "POST") return post.promise;
    return ++reads === 2 ? { ok: true, json: () => body.promise } : ok(ready());
  });
  await refreshHouseLearningStatus();
  await time.to(44200);
  const write = commitSwitch("houseLearningEnabled", false);
  body.resolve(ready(10000));
  await flush();
  assert.equal(state.houseLearningStatus.shadow, null);
  post.resolve({ ok: false, status: 503 });
  await write;
  assert.equal(state.houseLearningStatus.shadow, null);
  assert.equal(time.pending, 0);
});

test("reset negeert de vervolgbody en gebruikt alleen zijn eigen bevestiging", async t => {
  const time = clock(t), body = deferred();
  let reads = 0;
  t.mock.method(globalThis, "fetch", async () => {
    reads += 1;
    if (reads === 2) return { ok: true, json: () => body.promise };
    return ok(reads > 2 ? { ...ready(), enabled: false, status: "paused", records: 0, journal_status: "cleared", shadow: undefined } : ready());
  });
  await refreshHouseLearningStatus();
  await time.to(44200);
  state.controlError = "";
  const reset = resetHouseLearningData(async () => {}, { confirmReset: () => true, pollDelays: [0, 0] });
  body.resolve(ready(10000));
  assert.equal(await reset, true);
  assert.equal(state.houseLearningStatus.shadow, null);
  assert.equal(state.entities.houseLearningEnabled.value, false);
  assert.equal(time.pending, 0);
});

test("read-timeout blijft 8s, deelt single-flight en veroorzaakt geen retrylus", async t => {
  const time = clock(t);
  let reads = 0;
  t.mock.method(globalThis, "fetch", async (_url, { signal }) => {
    if (++reads !== 2) return ok(ready());
    return { ok: true, json: () => new Promise((_resolve, reject) => signal.addEventListener("abort", () => reject(signal.reason), { once: true })) };
  });
  await refreshHouseLearningStatus();
  await time.to(44200);
  const same = refreshHouseLearningStatus({ force: true });
  await time.to(52199);
  assert.equal(reads, 2);
  assert.ok(state.houseLearningFetchPromise);
  await time.to(52200);
  await same;
  assert.equal(state.houseLearningFetchPromise, null);
  assert.equal(state.houseLearningStatus, null);
  assert.equal(time.pending, 0);
  await time.to(60000);
  await refreshHouseLearningStatus();
  assert.equal(reads, 3);
});

test("HTTP-fout volgt de gewone pollfallback, zonder snelle retries", async t => {
  const time = clock(t);
  let reads = 0;
  t.mock.method(globalThis, "fetch", async () => ++reads === 1 ? ok(ready()) : { ok: false, status: 503 });
  await refreshHouseLearningStatus();
  await time.to(49000);
  assert.equal(reads, 2);
  assert.equal(time.pending, 0);
  await time.to(50000);
  await refreshHouseLearningStatus();
  assert.equal(reads, 3);
});
