import assert from "node:assert/strict";
import test from "node:test";

const originalFetch = globalThis.fetch;

globalThis.window = {
  clearTimeout,
  location: { href: "http://openquatt.local/?view=settings" },
  setTimeout,
};

const {
  WEB_APP_CACHE_REFRESH_TIMEOUT_MS,
  getWebAppCacheRefreshUrls,
  refreshWebAppCache,
} = await import("../js/src/core/app-cache.js");

test.afterEach(() => {
  globalThis.fetch = originalFetch;
  window.clearTimeout = clearTimeout;
  window.setTimeout = setTimeout;
});

test("OTA cache refresh fully reloads the document and embedded app assets", async () => {
  const requests = [];
  const consumed = [];
  globalThis.fetch = async (url, options) => {
    requests.push({ url, options });
    return {
      ok: true,
      async arrayBuffer() {
        consumed.push(url);
        return new ArrayBuffer(0);
      },
    };
  };

  assert.deepEqual(getWebAppCacheRefreshUrls(), [
    "http://openquatt.local/?view=settings",
    "/0.css",
    "/0.js",
  ]);
  assert.equal(await refreshWebAppCache(), true);
  assert.deepEqual(requests.map(({ url }) => url), getWebAppCacheRefreshUrls());
  requests.forEach(({ options }) => {
    assert.equal(options.cache, "reload");
    assert.equal(options.credentials, "same-origin");
    assert.ok(options.signal instanceof AbortSignal);
  });
  assert.deepEqual(consumed, getWebAppCacheRefreshUrls());
});

test("OTA cache refresh reports a failed asset request without throwing", async () => {
  globalThis.fetch = async (url) => ({
    ok: url !== "/0.js",
    status: url === "/0.js" ? 503 : 200,
    arrayBuffer: async () => new ArrayBuffer(0),
  });

  assert.equal(await refreshWebAppCache(), false);
});

test("OTA cache refresh aborts stalled app-shell requests after a bounded timeout", async () => {
  const timers = [];
  window.clearTimeout = (timer) => {
    timer.cancelled = true;
  };
  window.setTimeout = (callback, delay) => {
    const timer = { callback, cancelled: false, delay };
    timers.push(timer);
    return timer;
  };
  globalThis.fetch = (_url, options) => new Promise((_resolve, reject) => {
    options.signal.addEventListener("abort", () => reject(new Error("aborted")));
  });

  const refreshPromise = refreshWebAppCache();
  assert.equal(timers.length, getWebAppCacheRefreshUrls().length);
  assert.ok(timers.every((timer) => timer.delay === WEB_APP_CACHE_REFRESH_TIMEOUT_MS));
  timers.forEach((timer) => timer.callback());

  assert.equal(await refreshPromise, false);
  assert.ok(timers.every((timer) => timer.cancelled));
});

test("OTA cache refresh also bounds a stalled response body", async () => {
  const timers = [];
  window.clearTimeout = (timer) => {
    timer.cancelled = true;
  };
  window.setTimeout = (callback, delay) => {
    const timer = { callback, cancelled: false, delay };
    timers.push(timer);
    return timer;
  };
  globalThis.fetch = async (_url, options) => ({
    ok: true,
    arrayBuffer: () => new Promise((_resolve, reject) => {
      if (options.signal.aborted) {
        reject(new Error("aborted"));
        return;
      }
      options.signal.addEventListener("abort", () => reject(new Error("aborted")));
    }),
  });

  const refreshPromise = refreshWebAppCache();
  await Promise.resolve();
  assert.equal(timers.length, getWebAppCacheRefreshUrls().length);
  timers.forEach((timer) => timer.callback());

  assert.equal(await refreshPromise, false);
  assert.ok(timers.every((timer) => timer.cancelled));
});
