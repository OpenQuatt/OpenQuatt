import assert from "node:assert/strict";
import test from "node:test";

import { copyTextToClipboard, fetchWithTimeout } from "../js/src/core/browser-utils.js";

test("fetchWithTimeout returns the fetch response before the deadline", async (t) => {
  const originalFetch = globalThis.fetch;
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.fetch = originalFetch;
    globalThis.window = originalWindow;
  });

  const response = { ok: true };
  globalThis.window = globalThis;
  globalThis.fetch = async (_input, options) => {
    assert.ok(options.signal instanceof AbortSignal);
    return response;
  };

  assert.equal(await fetchWithTimeout("/test", { cache: "no-store" }, 100), response);
});

test("fetchWithTimeout reports the endpoint-specific timeout", async (t) => {
  const originalFetch = globalThis.fetch;
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.fetch = originalFetch;
    globalThis.window = originalWindow;
  });

  globalThis.window = globalThis;
  globalThis.fetch = (_input, options) => new Promise((_resolve, reject) => {
    options.signal.addEventListener("abort", () => reject(new Error("aborted")), { once: true });
  });

  await assert.rejects(
    fetchWithTimeout("/test", {}, 1, "test endpoint timed out"),
    /test endpoint timed out/,
  );
});

test("clipboard fallback keeps focus on the invoking control without scrolling", async (t) => {
  const originalDocument = globalThis.document;
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.document = originalDocument;
    globalThis.window = originalWindow;
  });

  const focusCalls = [];
  const focusOrigin = {
    isConnected: true,
    focus: (options) => focusCalls.push(["origin", options]),
  };
  const textarea = {
    style: {},
    setAttribute() {},
    focus: (options) => focusCalls.push(["textarea", options]),
    select() {},
    value: "",
  };
  globalThis.window = { isSecureContext: false, navigator: {} };
  globalThis.document = {
    activeElement: focusOrigin,
    body: {
      appendChild() {},
      removeChild() {},
    },
    createElement: () => textarea,
    execCommand: () => true,
  };

  assert.equal(await copyTextToClipboard("OpenQuatt"), true);
  assert.deepEqual(focusCalls, [
    ["textarea", { preventScroll: true }],
    ["origin", { preventScroll: true }],
  ]);
});

test("rejected clipboard API falls back to execCommand instead of failing", async (t) => {
  const originalDocument = globalThis.document;
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.document = originalDocument;
    globalThis.window = originalWindow;
  });

  let apiCalls = 0;
  globalThis.window = {
    isSecureContext: true,
    navigator: {
      clipboard: {
        writeText: async () => {
          apiCalls += 1;
          throw new Error("NotAllowedError");
        },
      },
    },
  };
  globalThis.document = {
    activeElement: null,
    body: {
      appendChild() {},
      removeChild() {},
    },
    createElement: () => ({
      style: {},
      setAttribute() {},
      focus() {},
      select() {},
      value: "",
    }),
    execCommand: () => true,
  };

  assert.equal(await copyTextToClipboard("OpenQuatt"), true);
  assert.equal(apiCalls, 1);
});
