import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;

const {
  buildWebServerLogCopyText,
  clearWebServerLogHistory,
  createWebServerLogEntry,
  getWebServerLogClearUrl,
  refreshWebServerLogHistory,
  renderWebServerLogHistoryControls,
  renderWebServerLoggerLevelControl,
} =
  await import("../js/src/features/webserver-logs.js");
const { state } = await import("../js/src/core/state.js");

function seedLogState() {
  state.nativeOpen = false;
  state.systemModal = null;
  state.busyAction = "";
  state.webServerLogSource = null;
  state.webServerLogConnected = false;
  state.webServerLogHistoryError = "";
  state.webServerLogHistoryLoaded = true;
  state.webServerLogCsrfToken = "test-csrf-token";
  state.webServerLogHistoryNeedsReconcile = false;
  state.webServerLogEntries = [{ raw: "old log entry", text: "old log entry" }];
}

test("buildWebServerLogCopyText omits ANSI control sequences from live entries", () => {
  state.webServerLogEntries = [
    createWebServerLogEntry("\x1b[31m[E][component:1]: failure\x1b[0m", { receivedAt: 1000 }),
  ];

  const copied = buildWebServerLogCopyText();
  assert.match(copied, /\[E\]\[component:1\]: failure$/);
  assert.doesNotMatch(copied, /\x1b|\[0m/);
});

test("clearWebServerLogHistory clears firmware history before local entries", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  const requests = [];
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async (url, options) => {
      requests.push({ url, options });
      assert.equal(state.webServerLogEntries.length, 1);
      return {
        ok: true,
        status: 200,
      };
    },
  };

  assert.equal(getWebServerLogClearUrl(), "/openquatt/logs/clear");
  assert.equal(await clearWebServerLogHistory(), true);
  assert.equal(requests.length, 1);
  assert.equal(requests[0].url, "/openquatt/logs/clear");
  assert.equal(requests[0].options.method, "POST");
  assert.equal(requests[0].options.body.get("csrf_token"), "test-csrf-token");
  assert.deepEqual(state.webServerLogEntries, []);
  assert.equal(state.webServerLogHistoryLoaded, false);
  assert.equal(state.webServerLogHistoryNeedsReconcile, false);
  assert.equal(state.webServerLogHistoryError, "");
  assert.equal(state.busyAction, "");
});

test("clearWebServerLogHistory preserves visible entries when firmware clearing fails", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => ({ ok: false, status: 500 }),
  };

  assert.equal(await clearWebServerLogHistory(), false);
  assert.equal(state.webServerLogEntries.length, 1);
  assert.equal(state.webServerLogHistoryLoaded, false);
  assert.equal(state.webServerLogHistoryNeedsReconcile, true);
  assert.match(state.webServerLogHistoryError, /HTTP 500/);
  assert.equal(state.busyAction, "");
});

test("clearWebServerLogHistory backfills the closed live-stream gap after failure", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  state.systemModal = "webserver-logs";
  let streamClosed = false;
  state.webServerLogSource = {
    close() {
      streamClosed = true;
    },
  };
  const requests = [];
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async (url, options = {}) => {
      requests.push({ url, options });
      if (options.method === "POST") {
        return { ok: false, status: 503 };
      }
      return {
        ok: true,
        status: 200,
        json: async () => ({
          enabled: true,
          csrf_token: "test-csrf-token",
          entries: [
            { raw: "old log entry", ts: 1000, seq: 1 },
            { raw: "missed while clearing", ts: 2000, seq: 2 },
          ],
        }),
      };
    },
  };

  assert.equal(await clearWebServerLogHistory(), false);
  assert.deepEqual(requests.map(({ url }) => url), [
    "/openquatt/logs/clear",
    "/openquatt/logs/recent",
  ]);
  assert.deepEqual(state.webServerLogEntries.map(({ raw }) => raw), ["old log entry", "missed while clearing"]);
  assert.match(state.webServerLogHistoryError, /HTTP 503/);
  assert.equal(streamClosed, true);
});

test("clearWebServerLogHistory reconciles entries produced after firmware clearing", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  state.systemModal = "webserver-logs";
  let streamClosed = false;
  state.webServerLogSource = {
    close() {
      streamClosed = true;
    },
  };
  const requests = [];
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async (url, options = {}) => {
      requests.push({ url, options });
      if (options.method === "POST") {
        return {
          ok: true,
          status: 200,
        };
      }
      return {
        ok: true,
        status: 200,
        json: async () => ({
          enabled: true,
          csrf_token: "test-csrf-token",
          entries: [
            { raw: "after clear 1", ts: 1000, seq: 1 },
            { raw: "after clear 2", ts: 2000, seq: 2 },
          ],
        }),
      };
    },
  };

  assert.equal(await clearWebServerLogHistory(), true);
  assert.deepEqual(requests.map(({ url }) => url), [
    "/openquatt/logs/clear",
    "/openquatt/logs/recent",
  ]);
  assert.deepEqual(state.webServerLogEntries.map(({ raw }) => raw), ["after clear 1", "after clear 2"]);
  assert.equal(state.webServerLogHistoryLoaded, true);
  assert.equal(streamClosed, true);
});

test("clearWebServerLogHistory refreshes and retries after a rotated CSRF token", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  state.systemModal = "webserver-logs";
  const postedTokens = [];
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async (_url, options = {}) => {
      if (options.method === "POST") {
        postedTokens.push(options.body.get("csrf_token"));
        if (postedTokens.length === 1) {
          return { ok: false, status: 403 };
        }
        return {
          ok: true,
          status: 200,
          json: async () => ({ enabled: true, csrf_token: "rotated-csrf-token", entries: [] }),
        };
      }
      return {
        ok: true,
        status: 200,
        json: async () => ({ enabled: true, csrf_token: "rotated-csrf-token", entries: [] }),
      };
    },
  };

  assert.equal(await clearWebServerLogHistory(), true);
  assert.deepEqual(postedTokens, ["test-csrf-token", "rotated-csrf-token"]);
  assert.equal(state.webServerLogCsrfToken, "rotated-csrf-token");
  assert.equal(state.webServerLogHistoryError, "");
});

test("clearWebServerLogHistory does not repeat an ambiguous clear request", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  let postCount = 0;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => {
      postCount += 1;
      throw new TypeError("connection closed");
    },
  };

  assert.equal(await clearWebServerLogHistory(), false);
  assert.equal(postCount, 1);
  assert.equal(state.webServerLogEntries.length, 1);
  assert.equal(state.webServerLogHistoryNeedsReconcile, true);
});

test("clearWebServerLogHistory replaces stale rows after an ambiguous failure", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  state.systemModal = "webserver-logs";
  let postCount = 0;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async (_url, options = {}) => {
      if (options.method === "POST") {
        postCount += 1;
        throw new TypeError("connection closed");
      }
      return {
        ok: true,
        status: 200,
        json: async () => ({ enabled: true, csrf_token: "test-csrf-token", entries: [] }),
      };
    },
  };

  assert.equal(await clearWebServerLogHistory(), false);
  assert.equal(postCount, 1);
  assert.deepEqual(state.webServerLogEntries, []);
  assert.equal(state.webServerLogHistoryLoaded, true);
  assert.equal(state.webServerLogHistoryNeedsReconcile, false);
  assert.match(state.webServerLogHistoryError, /connection closed/);
});

test("clearWebServerLogHistory keeps reconciliation pending when the modal closes", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  state.systemModal = "webserver-logs";
  let postCount = 0;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => {
      postCount += 1;
      state.systemModal = null;
      return { ok: false, status: 503 };
    },
  };

  assert.equal(await clearWebServerLogHistory(), false);
  assert.equal(postCount, 1);
  assert.equal(state.webServerLogHistoryLoaded, false);
  assert.equal(state.webServerLogHistoryNeedsReconcile, true);
  assert.equal(state.webServerLogEntries.length, 1);
});

test("refreshWebServerLogHistory replaces stale rows without dropping concurrent live entries", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  state.systemModal = "webserver-logs";
  state.webServerLogHistoryNeedsReconcile = true;
  const liveEntry = { raw: "live during refresh", text: "live during refresh", receivedAt: 3000 };
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => {
      state.webServerLogEntries = [...state.webServerLogEntries, liveEntry];
      return {
        ok: true,
        status: 200,
        json: async () => ({
          enabled: true,
          csrf_token: "test-csrf-token",
          entries: [{ raw: "authoritative history", ts: 2000, seq: 1 }],
        }),
      };
    },
  };

  await refreshWebServerLogHistory();
  assert.deepEqual(state.webServerLogEntries.map(({ raw }) => raw), ["authoritative history", "live during refresh"]);
  assert.equal(state.webServerLogHistoryNeedsReconcile, false);
});

test("clearWebServerLogHistory does not replace another pending action", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  state.busyAction = "save-debugLevel";
  let requested = false;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => {
      requested = true;
      return { ok: true, status: 200 };
    },
  };

  assert.equal(await clearWebServerLogHistory(), false);
  assert.equal(requested, false);
  assert.equal(state.busyAction, "save-debugLevel");
  assert.equal(state.webServerLogEntries.length, 1);
});

test("clearWebServerLogHistory requires a firmware CSRF token", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  state.webServerLogCsrfToken = "";
  let requested = false;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => {
      requested = true;
      return { ok: true, status: 200 };
    },
  };

  assert.equal(await clearWebServerLogHistory(), false);
  assert.equal(requested, false);
  assert.match(state.webServerLogHistoryError, /beveiligingstoken/);
  assert.equal(state.webServerLogEntries.length, 1);
});

test("refreshWebServerLogHistory stores the firmware CSRF token", async (t) => {
  const originalWindow = globalThis.window;
  const originalSystemModal = state.systemModal;
  t.after(() => {
    globalThis.window = originalWindow;
    state.systemModal = originalSystemModal;
  });

  seedLogState();
  state.systemModal = "webserver-logs";
  state.webServerLogCsrfToken = "";
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => ({
      ok: true,
      status: 200,
      json: async () => ({ enabled: true, csrf_token: "firmware-csrf-token", entries: [] }),
    }),
  };

  await refreshWebServerLogHistory();
  assert.equal(state.webServerLogCsrfToken, "firmware-csrf-token");
});

test("webserver log controls only render the logger-level card", (t) => {
  const originalEntities = state.entities;
  const originalLoadingEntities = state.loadingEntities;
  const originalBusyAction = state.busyAction;
  const originalInfoOpen = state.settingsInfoOpen;
  t.after(() => {
    state.entities = originalEntities;
    state.loadingEntities = originalLoadingEntities;
    state.busyAction = originalBusyAction;
    state.settingsInfoOpen = originalInfoOpen;
  });

  state.entities = {
    debugLevel: { value: "INFO", options: ["INFO", "DEBUG"] },
  };
  state.loadingEntities = false;
  state.busyAction = "";
  state.settingsInfoOpen = "";

  const markup = renderWebServerLogHistoryControls();
  assert.match(markup, /oq-webserver-log-control-card/);
  assert.match(markup, /data-oq-settings-info="webserverLoggerLevel"/);
  assert.doesNotMatch(markup, /RAM log history|Uitschakelen|Inschakelen/);
  assert.equal((markup.match(/oq-settings-system-row-note/g) || []).length, 0);
});

test("active DEBUG keeps its performance warning visible", (t) => {
  const originalEntities = state.entities;
  const originalLoadingEntities = state.loadingEntities;
  const originalBusyAction = state.busyAction;
  t.after(() => {
    state.entities = originalEntities;
    state.loadingEntities = originalLoadingEntities;
    state.busyAction = originalBusyAction;
  });

  state.entities = {
    debugLevel: { value: "DEBUG", options: ["INFO", "DEBUG"] },
  };
  state.loadingEntities = false;
  state.busyAction = "";

  const markup = renderWebServerLoggerLevelControl();
  assert.match(markup, /oq-webserver-log-control-card--warning/);
  assert.match(markup, /DEBUG kan de web-app en Home Assistant vertragen\./);
});
