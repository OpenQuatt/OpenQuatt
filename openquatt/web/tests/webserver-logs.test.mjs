import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;

const {
  WEB_SERVER_LOG_MAX_ENTRIES,
  WEB_SERVER_LOG_POLL_INTERVAL_MS,
  WEB_SERVER_LOG_POLL_RETRY_DELAYS_MS,
  WEB_SERVER_LOG_REQUEST_TIMEOUT_MS,
  buildWebServerLogCopyText,
  cancelWebServerLogPoll,
  clearWebServerLogHistory,
  closeWebServerLogStream,
  createWebServerLogEntry,
  getWebServerLogClearUrl,
  normalizeRecentWebServerLogPayload,
  openWebServerLogsModal,
  refreshWebServerLogHistory,
  renderWebServerLogHistoryControls,
  renderWebServerLoggerLevelControl,
  scheduleWebServerLogPoll,
  syncWebServerLogStream,
} =
  await import("../js/src/features/webserver-logs.js");
const { state } = await import("../js/src/core/state.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");

function seedLogState() {
  if (state.webServerLogPollTimer) {
    try {
      globalThis.clearTimeout(state.webServerLogPollTimer);
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
  state.webServerLogPollTimer = null;
  state.webServerLogPollFailureCount = 0;
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

test("refreshWebServerLogHistory replaces stale rows from the authoritative history", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLogState();
  state.systemModal = "webserver-logs";
  state.webServerLogHistoryNeedsReconcile = true;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => {
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
  assert.deepEqual(state.webServerLogEntries.map(({ raw }) => raw), ["authoritative history"]);
  assert.equal(state.webServerLogHistoryNeedsReconcile, false);
});

test("background-refresh rendert opnieuw wanneer firmware timestamps herijkt", async (t) => {
  const originalWindow = globalThis.window;
  let renderCount = 0;
  t.after(() => {
    globalThis.window = originalWindow;
    setRenderCallback(null);
  });

  seedLogState();
  state.systemModal = "webserver-logs";
  state.webServerLogEntries = [createWebServerLogEntry("zelfde regel", { receivedAt: 1000, seq: 1 })];
  setRenderCallback(() => {
    renderCount += 1;
  });
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => ({
      ok: true,
      status: 200,
      json: async () => ({
        enabled: true,
        csrf_token: "test-csrf-token",
        entries: [{ raw: "zelfde regel", ts: 1757000000000, seq: 1 }],
      }),
    }),
  };

  assert.equal(await refreshWebServerLogHistory({ background: true }), true);
  assert.equal(state.webServerLogEntries[0].receivedAt, 1757000000000);
  assert.equal(renderCount, 1);
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

function stubPollTimers(t) {
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
    cancelWebServerLogPoll();
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

async function fireNextPoll(scheduled) {
  const timer = scheduled.shift();
  assert.ok(timer);
  await timer.callback();
  return timer;
}

test("logboek gebruikt history-polling zonder extra EventSource", (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLifecycleState();
  const scheduled = stubPollTimers(t);
  let eventSourceConstructed = 0;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => ({ ok: true, json: async () => ({ enabled: true, entries: [] }) }),
    EventSource: class {
      constructor() {
        eventSourceConstructed += 1;
      }
    },
  };

  syncWebServerLogStream();
  syncWebServerLogStream();

  assert.equal(eventSourceConstructed, 0);
  assert.equal(state.webServerLogSource, null);
  assert.equal(scheduled.length, 1);
  assert.equal(scheduled[0].delay, WEB_SERVER_LOG_POLL_INTERVAL_MS);
  assert.equal(state.webServerLogPollTimer, scheduled[0]);
});

test("geslaagde poll voegt historie toe en plant precies één volgende poll", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLifecycleState();
  state.webServerLogEntries = [];
  const scheduled = stubPollTimers(t);
  const historyRequests = [];
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async (url) => {
      historyRequests.push(url);
      return {
        ok: true,
        status: 200,
        json: async () => ({
          enabled: true,
          csrf_token: "new-token",
          entries: [{ raw: "nieuwe logregel", ts: 2000, seq: 2 }],
        }),
      };
    },
  };

  syncWebServerLogStream();
  const firstTimer = await fireNextPoll(scheduled);

  assert.equal(firstTimer.delay, WEB_SERVER_LOG_POLL_INTERVAL_MS);
  assert.deepEqual(historyRequests, ["/openquatt/logs/recent"]);
  assert.deepEqual(state.webServerLogEntries.map(({ raw }) => raw), ["nieuwe logregel"]);
  assert.equal(state.webServerLogCsrfToken, "new-token");
  assert.equal(state.webServerLogConnected, true);
  assert.equal(state.webServerLogPollFailureCount, 0);
  assert.equal(scheduled.length, 1);
  assert.equal(scheduled[0].delay, WEB_SERVER_LOG_POLL_INTERVAL_MS);
});

test("mislukte polls gebruiken back-off en herstellen zonder paginarefresh", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLifecycleState();
  state.webServerLogEntries = [];
  const scheduled = stubPollTimers(t);
  let requestCount = 0;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => {
      requestCount += 1;
      if (requestCount === 1) {
        throw new TypeError("connection closed");
      }
      return {
        ok: true,
        status: 200,
        json: async () => ({
          enabled: true,
          csrf_token: "recovered-token",
          entries: [{ raw: "gemist tijdens storing", ts: 3000, seq: 3 }],
        }),
      };
    },
  };

  scheduleWebServerLogPoll();
  await fireNextPoll(scheduled);

  assert.equal(state.webServerLogConnected, false);
  assert.match(state.webServerLogError, /Nieuwe poging/);
  assert.equal(state.webServerLogPollFailureCount, 1);
  assert.equal(scheduled.length, 1);
  assert.equal(scheduled[0].delay, WEB_SERVER_LOG_POLL_RETRY_DELAYS_MS[0]);

  await fireNextPoll(scheduled);

  assert.equal(state.webServerLogConnected, true);
  assert.equal(state.webServerLogError, "");
  assert.equal(state.webServerLogPollFailureCount, 0);
  assert.deepEqual(state.webServerLogEntries.map(({ raw }) => raw), ["gemist tijdens storing"]);
  assert.equal(scheduled.length, 1);
  assert.equal(scheduled[0].delay, WEB_SERVER_LOG_POLL_INTERVAL_MS);
});

test("vastgelopen history-request wordt afgebroken en opnieuw geprobeerd", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLifecycleState();
  const scheduled = stubPollTimers(t);
  globalThis.window = {
    location: { pathname: "/" },
    fetch: (_url, options = {}) => new Promise((_resolve, reject) => {
      options.signal?.addEventListener("abort", () => {
        const error = new Error("request timeout");
        error.name = "AbortError";
        reject(error);
      });
    }),
  };

  scheduleWebServerLogPoll();
  const pollTimer = scheduled.shift();
  const pollPromise = pollTimer.callback();
  await flushHistoryRequests();

  assert.equal(scheduled.length, 1);
  assert.equal(scheduled[0].delay, WEB_SERVER_LOG_REQUEST_TIMEOUT_MS);
  const requestTimeout = scheduled.shift();
  requestTimeout.callback();
  await pollPromise;

  assert.equal(state.webServerLogConnected, false);
  assert.match(state.webServerLogError, /Nieuwe poging/);
  assert.equal(state.webServerLogPollFailureCount, 1);
  assert.equal(scheduled.length, 1);
  assert.equal(scheduled[0].delay, WEB_SERVER_LOG_POLL_RETRY_DELAYS_MS[0]);
});

test("polltimer wordt geannuleerd bij sluiten van het modal", (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLifecycleState();
  const scheduled = stubPollTimers(t);
  globalThis.window = { location: { pathname: "/" }, fetch: async () => ({ ok: true }) };

  scheduleWebServerLogPoll();
  assert.equal(scheduled.length, 1);

  state.systemModal = null;
  syncWebServerLogStream();

  assert.equal(state.webServerLogPollTimer, null);
  assert.equal(scheduled.length, 0);
});

test("late pollresponse na sluiten verandert het logboek niet", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLifecycleState();
  state.webServerLogEntries = [];
  const scheduled = stubPollTimers(t);
  let resolveFetch;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: () => new Promise((resolve) => {
      resolveFetch = resolve;
    }),
  };

  scheduleWebServerLogPoll();
  const timer = scheduled.shift();
  const pollPromise = timer.callback();
  await flushHistoryRequests();

  state.systemModal = null;
  syncWebServerLogStream();
  resolveFetch({
    ok: true,
    status: 200,
    json: async () => ({ enabled: true, entries: [{ raw: "te laat", ts: 4000, seq: 4 }] }),
  });
  await pollPromise;

  assert.deepEqual(state.webServerLogEntries, []);
  assert.equal(state.webServerLogPollTimer, null);
  assert.equal(scheduled.length, 0);
});

test("sluiten invalideert een directe history-request zonder loading-state achter te laten", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLifecycleState();
  state.webServerLogEntries = [];
  let resolveFetch;
  globalThis.window = {
    location: { pathname: "/" },
    fetch: () => new Promise((resolve) => {
      resolveFetch = resolve;
    }),
  };

  const refreshPromise = refreshWebServerLogHistory();
  await flushHistoryRequests();
  assert.equal(state.webServerLogHistoryLoading, true);

  state.systemModal = null;
  closeWebServerLogStream();
  assert.equal(state.webServerLogHistoryLoading, false);

  resolveFetch({
    ok: true,
    status: 200,
    json: async () => ({ enabled: true, entries: [{ raw: "te laat", ts: 4000, seq: 4 }] }),
  });
  assert.equal(await refreshPromise, false);
  assert.deepEqual(state.webServerLogEntries, []);
  assert.equal(state.webServerLogHistoryLoading, false);
});

test("normale modalopening haalt de historie direct precies één keer op", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
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

  assert.deepEqual(historyRequests, ["/openquatt/logs/recent"]);
});

test("identieke tekst met verschillende seq blijft behouden", () => {
  const entries = normalizeRecentWebServerLogPayload({
    enabled: true,
    entries: [
      { raw: "zelfde melding", ts: 1000, seq: 1 },
      { raw: "zelfde melding", ts: 1500, seq: 2 },
    ],
  });

  assert.deepEqual(entries.map(({ seq }) => seq), [1, 2]);
});

test("history-refresh begrenst 500 firmware-regels tot 250", async (t) => {
  const originalWindow = globalThis.window;
  t.after(() => {
    globalThis.window = originalWindow;
  });

  seedLifecycleState();
  globalThis.window = {
    location: { pathname: "/" },
    fetch: async () => ({
      ok: true,
      status: 200,
      json: async () => ({
        enabled: true,
        entries: Array.from({ length: 500 }, (_entry, index) => ({
          raw: `regel ${index + 1}`,
          ts: index + 1,
          seq: index + 1,
        })),
      }),
    }),
  };

  await refreshWebServerLogHistory();
  assert.equal(state.webServerLogEntries.length, WEB_SERVER_LOG_MAX_ENTRIES);
  assert.equal(state.webServerLogEntries[0].seq, 251);
});
