import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { refreshDecisionLogStorageMetadata, refreshTrendHistoryMetadata } = await import("../js/src/features/storage-history.js");
const originalFetch = globalThis.fetch;
const initial = structuredClone(state);
test.beforeEach(() => {
  Object.assign(state, structuredClone(initial));
  state.entities.trendHistoryEnabled = { value: true, state: "ON" };
});
test.afterEach(() => { globalThis.fetch = originalFetch; });

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

for (const [prefix, refresh, response, readValue] of [
  ["decisionLogStorageMetadata", refreshDecisionLogStorageMetadata,
    (value) => ({ ok: true, json: async () => ({ ok: true, stored_events: value }) }), (metadata) => metadata.storedEvents],
  ["trendHistoryMetadata", refreshTrendHistoryMetadata,
    (value) => ({ ok: true, text: async () => `@flash|yes|old|new|flush|${value}|1` }), (metadata) => metadata.sizeKb],
]) {
  test(`${prefix}: concurrent callers share success, then use the refresh interval`, async () => {
    const pending = deferred();
    let calls = 0;
    globalThis.fetch = () => { calls++; return pending.promise; };
    const first = refresh();
    const second = refresh();
    assert.equal(calls, 1);
    pending.resolve(response(12));
    assert.deepEqual(await Promise.all([first, second]), [true, true]);
    assert.equal(readValue(state[prefix]), 12);
    assert.equal(await refresh(), false);
    assert.equal(calls, 1);
    assert.equal(state[prefix + "FetchPromise"], null);
  });

  test(`${prefix}: shared failures resolve consistently and a forced retry recovers`, async () => {
    const pending = deferred();
    globalThis.fetch = () => pending.promise;
    const first = refresh();
    const second = refresh();
    pending.reject(new Error("offline"));
    assert.deepEqual(await Promise.all([first, second]), [true, true]);
    assert.match(state[prefix + "Error"], /offline/);
    assert.equal(await refresh(), false);
    globalThis.fetch = async () => response(23);
    assert.equal(await refresh({ force: true }), true);
    assert.equal(readValue(state[prefix]), 23);
    assert.equal(state[prefix + "Error"], "");
  });

  for (const oldFinishesFirst of [true, false]) {
    test(`${prefix}: superseded request cannot overwrite state or clear a newer request (${oldFinishesFirst})`, async () => {
      const requests = [deferred(), deferred()];
      let index = 0;
      globalThis.fetch = () => requests[index++].promise;
      const old = refresh();
      const latest = refresh({ force: true });
      const latestPromise = state[prefix + "FetchPromise"];
      if (oldFinishesFirst) {
        requests[0].resolve(response(1));
        assert.equal(await old, false);
        assert.equal(state[prefix + "FetchPromise"], latestPromise);
      }
      requests[1].resolve(response(2));
      assert.equal(await latest, true);
      if (!oldFinishesFirst) {
        requests[0].reject(new Error("late failure"));
        assert.equal(await old, false);
      }
      assert.equal(readValue(state[prefix]), 2);
      assert.equal(state[prefix + "Error"], "");
      assert.equal(state[prefix + "FetchPromise"], null);
    });
  }
}

test("disabling trend metadata invalidates an in-flight response", async () => {
  const pending = deferred();
  globalThis.fetch = () => pending.promise;
  const request = refreshTrendHistoryMetadata();
  delete state.entities.trendHistoryEnabled;
  await refreshTrendHistoryMetadata();
  pending.resolve({ ok: true, text: async () => "@flash|yes|old|new|flush|99|1" });
  assert.equal(await request, false);
  assert.deepEqual(state.trendHistoryMetadata, {});
  assert.equal(state.trendHistoryMetadataLastFetchAt, 0);
});

test("decision metadata rejects an invalid successful HTTP response", async () => {
  globalThis.fetch = async () => ({ ok: true, json: async () => ({ ok: false }) });
  assert.equal(await refreshDecisionLogStorageMetadata(), true);
  assert.match(state.decisionLogStorageMetadataError, /ongeldig antwoord/);
  assert.deepEqual(state.decisionLogStorageMetadata, {});
});
