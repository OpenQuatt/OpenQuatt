import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;

const {
  WEB_SERVER_LOG_MAX_ENTRIES,
  WEB_SERVER_LOG_RECONNECT_DELAYS_MS,
  buildWebServerLogCopyText,
  cancelWebServerLogReconnect,
  clearWebServerLogHistory,
  closeWebServerLogStream,
  createWebServerLogEntry,
  getWebServerLogClearUrl,
  handleWebServerLogError,
  handleWebServerLogMessage,
  handleWebServerLogOpen,
  handleWebServerLogPing,
  isCurrentWebServerLogSourceEvent,
  isDuplicateWebServerLogEntry,
  mergeWebServerLogEntries,
  openWebServerLogsModal,
  refreshWebServerLogHistory,
  renderWebServerLogHistoryControls,
  renderWebServerLoggerLevelControl,
  scheduleWebServerLogReconnect,
  syncWebServerLogStream,
  trimWebServerLogEntries,
} =
  await import("../js/src/features/webserver-logs.js");
const { state } = await import("../js/src/core/state.js");

function seedLogState() {
  if (state.webServerLogReconnectTimer) {
    try {
      globalThis.clearTimeout(state.webServerLogReconnectTimer);
    } catch {
      // Ignore timer cleanup in test setup.
    }
  }
  state.nativeOpen = false;
  state.systemModal = null;
  state.busyAction = "";
  state.mounted = false;
  state.webServerLogSource = null;
  state.webServerLogConnected = false;
  state.webServerLogEnabled = null;
  state.webServerLogError = "";
  state.webServerLogReconnectTimer = null;
  state.webServerLogReconnectAttempt = 0;
  state.webServerLogNeedsBackfill = false;
  state.webServerLogHistoryError = "";
  state.webServerLogHistoryLoaded = true;
  state.webServerLogHistoryLoading = false;
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

const SSE_CONNECTING = 0;
const SSE_CLOSED = 2;

function createFakeLogSource({ readyState = 1 } = {}) {
  return {
    readyState,
    closeCount: 0,
    listeners: {},
    addEventListener(type, handler) {
      if (!this.listeners[type]) {
        this.listeners[type] = [];
      }
      this.listeners[type].push(handler);
    },
    close() {
      this.closeCount += 1;
    },
  };
}

function stubReconnectTimers(t) {
  const originalSetTimeout = globalThis.setTimeout;
  const originalClearTimeout = globalThis.clearTimeout;
  const scheduled = [];
  globalThis.setTimeout = (callback, delay) => {
    const id = { callback, delay };
    scheduled.push(id);
    return id;
  };
  globalThis.clearTimeout = (id) => {
    const index = scheduled.indexOf(id);
    if (index >= 0) {
      scheduled.splice(index, 1);
    }
  };
  t.after(() => {
    globalThis.setTimeout = originalSetTimeout;
    globalThis.clearTimeout = originalClearTimeout;
    cancelWebServerLogReconnect();
  });
  return scheduled;
}

function seedLifecycleState() {
  seedLogState();
  state.mounted = true;
  state.systemModal = "webserver-logs";
}

async function flushHistoryRequests() {
  await new Promise((resolve) => setImmediate(resolve));
  await new Promise((resolve) => setImmediate(resolve));
}

function createFakeDomOutput(initialCount) {
  const children = [];
  const makeChild = () => {
    const child = {};
    child.remove = () => {
      const index = children.indexOf(child);
      if (index >= 0) {
        children.splice(index, 1);
      }
    };
    return child;
  };
  for (let index = 0; index < initialCount; index += 1) {
    children.push(makeChild());
  }
  return {
    children,
    get childElementCount() {
      return children.length;
    },
    get firstElementChild() {
      return children[0] ?? null;
    },
    append(count = 1) {
      for (let index = 0; index < count; index += 1) {
        children.push(makeChild());
      }
    },
  };
}

test("tijdelijke SSE-fout laat de source staan voor browser-reconnect", (t) => {
  const originalWindow = globalThis.window;
  const originalMounted = state.mounted;
  t.after(() => {
    globalThis.window = originalWindow;
    state.mounted = originalMounted;
    cancelWebServerLogReconnect();
  });

  seedLifecycleState();
  globalThis.window = { location: { pathname: "/" } };
  const source = createFakeLogSource({ readyState: SSE_CONNECTING });
  state.webServerLogSource = source;
  state.webServerLogEnabled = true;
  state.webServerLogConnected = true;

  handleWebServerLogError({ currentTarget: source });

  assert.equal(source.closeCount, 0);
  assert.equal(state.webServerLogSource, source);
  assert.notEqual(state.webServerLogEnabled, false);
  assert.equal(state.webServerLogConnected, false);
  assert.match(state.webServerLogError, /Opnieuw verbinden/);
  assert.equal(state.webServerLogNeedsBackfill, true);
  assert.equal(state.webServerLogReconnectTimer, null);
});

test("error gevolgd door open herstelt met precies een backfill-fetch", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
    cancelWebServerLogReconnect();
  });

  seedLifecycleState();
  state.webServerLogEntries = [];
  state.webServerLogHistoryRequestToken = 0;
  const historyRequests = [];
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async (url) => {
      historyRequests.push(url);
      return {
        ok: true,
        status: 200,
        json: async () => ({ enabled: true, csrf_token: "test-csrf-token", entries: [] }),
      };
    },
  };
  const source = createFakeLogSource({ readyState: SSE_CONNECTING });
  state.webServerLogSource = source;
  state.webServerLogConnected = true;

  handleWebServerLogError({ currentTarget: source });
  assert.equal(historyRequests.length, 0);

  handleWebServerLogOpen({ currentTarget: source });
  await flushHistoryRequests();

  assert.equal(state.webServerLogConnected, true);
  assert.equal(state.webServerLogError, "");
  assert.equal(state.webServerLogNeedsBackfill, false);
  assert.equal(state.webServerLogReconnectAttempt, 0);
  assert.deepEqual(historyRequests, ["/openquatt/logs/recent"]);
});

test("terminale SSE-fout plant maximaal een reconnect zonder direct nieuwe source", (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
    cancelWebServerLogReconnect();
  });

  seedLifecycleState();
  const scheduled = stubReconnectTimers(t);
  let constructed = 0;
  class FakeEventSource {
    constructor() {
      constructed += 1;
      this.readyState = SSE_CONNECTING;
      this.listeners = {};
    }

    addEventListener() {}

    close() {}
  }
  globalThis.window = {
    location: { pathname: "/" },
    EventSource: FakeEventSource,
  };
  const source = createFakeLogSource({ readyState: SSE_CLOSED });
  state.webServerLogSource = source;

  handleWebServerLogError({ currentTarget: source });
  handleWebServerLogError({ currentTarget: source });

  assert.equal(scheduled.length, 1);
  assert.equal(state.webServerLogReconnectTimer, scheduled[0]);
  assert.equal(state.webServerLogSource, source);
  assert.equal(source.closeCount, 0);
  assert.equal(constructed, 0);
  assert.ok(scheduled[0].delay >= WEB_SERVER_LOG_RECONNECT_DELAYS_MS[0]);

  scheduled[0].callback();
  assert.equal(source.closeCount, 1);
  assert.equal(constructed, 1);
});

test("reconnecttimer wordt geannuleerd bij sluiten van het modal", (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
    cancelWebServerLogReconnect();
  });

  seedLifecycleState();
  const scheduled = stubReconnectTimers(t);
  globalThis.window = { location: { pathname: "/" } };
  state.webServerLogSource = createFakeLogSource({ readyState: SSE_CLOSED });

  scheduleWebServerLogReconnect();
  assert.equal(scheduled.length, 1);

  state.systemModal = null;
  syncWebServerLogStream();

  assert.equal(state.webServerLogReconnectTimer, null);
  assert.equal(scheduled.length, 0);
  assert.equal(state.webServerLogSource, null);
});

test("events van een verouderde source veranderen de actuele verbinding niet", (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
    cancelWebServerLogReconnect();
  });

  seedLifecycleState();
  globalThis.window = { location: { pathname: "/" } };
  const current = createFakeLogSource({ readyState: 1 });
  const stale = createFakeLogSource({ readyState: SSE_CLOSED });
  state.webServerLogSource = current;
  state.webServerLogConnected = true;
  state.webServerLogError = "";
  state.webServerLogEntries = [];

  assert.equal(isCurrentWebServerLogSourceEvent({ currentTarget: current }), true);
  assert.equal(isCurrentWebServerLogSourceEvent({ currentTarget: stale }), false);

  handleWebServerLogError({ currentTarget: stale });
  assert.equal(state.webServerLogConnected, true);
  assert.equal(state.webServerLogSource, current);
  assert.equal(current.closeCount, 0);

  handleWebServerLogOpen({ currentTarget: stale });
  assert.equal(state.webServerLogConnected, true);

  handleWebServerLogPing({ currentTarget: stale });
  assert.equal(state.webServerLogConnected, true);

  handleWebServerLogMessage({ currentTarget: stale, data: "stale line" });
  assert.deepEqual(state.webServerLogEntries, []);
});

test("normale modalopening haalt de historie precies een keer op", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
    cancelWebServerLogReconnect();
  });

  seedLogState();
  state.systemModal = null;
  state.webServerLogEntries = [];
  state.webServerLogHistoryRequestToken = 0;
  const historyRequests = [];
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async (url) => {
      if (String(url).includes("/openquatt/logs/recent")) {
        historyRequests.push(url);
        return {
          ok: true,
          status: 200,
          json: async () => ({ enabled: true, csrf_token: "test-csrf-token", entries: [] }),
        };
      }
      return {
        ok: true,
        status: 200,
        json: async () => ({ entities: {}, missing: [] }),
      };
    },
  };

  openWebServerLogsModal();
  await flushHistoryRequests();

  const source = createFakeLogSource({ readyState: 1 });
  state.webServerLogSource = source;
  handleWebServerLogOpen({ currentTarget: source });
  await flushHistoryRequests();

  assert.deepEqual(historyRequests, ["/openquatt/logs/recent"]);
});

test("identieke tekst met verschillende seq blijft behouden", () => {
  seedLogState();
  const first = createWebServerLogEntry("zelfde melding", { receivedAt: 1000, seq: 1 });
  const sameSeq = createWebServerLogEntry("zelfde melding", { receivedAt: 1500, seq: 1 });
  const otherSeq = createWebServerLogEntry("zelfde melding", { receivedAt: 1500, seq: 2 });

  assert.equal(isDuplicateWebServerLogEntry(sameSeq, first), true);
  assert.equal(isDuplicateWebServerLogEntry(otherSeq, first), false);

  state.webServerLogEntries = [];
  mergeWebServerLogEntries([first, otherSeq]);
  assert.equal(state.webServerLogEntries.length, 2);
});

test("500 live regels begrenzen state en DOM tot 250", () => {
  seedLogState();
  state.webServerLogEntries = [];
  const batch = [];
  for (let index = 1; index <= 500; index += 1) {
    batch.push(createWebServerLogEntry(`regel ${index}`, { receivedAt: index, seq: index }));
  }
  mergeWebServerLogEntries(batch);
  assert.equal(state.webServerLogEntries.length, WEB_SERVER_LOG_MAX_ENTRIES);

  const output = createFakeDomOutput(500);
  trimWebServerLogEntries(output);
  assert.equal(output.childElementCount, WEB_SERVER_LOG_MAX_ENTRIES);
});
