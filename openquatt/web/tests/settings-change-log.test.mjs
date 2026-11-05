import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/" },
  localStorage: { getItem: () => null },
  setTimeout: (...args) => globalThis.setTimeout(...args),
  clearTimeout: (...args) => globalThis.clearTimeout(...args),
};

const { state } = await import("../js/src/core/state.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const {
  CHANGE_LOG_TEXT_LIMITS,
  clipUtf8,
  formatChangeLogValue,
  getSettingsChangeLogEntries,
  prepareChangeLogEntry,
  recordSettingsChange,
  refreshSettingsChangeLog,
  renderSettingsChangeLogSection,
} = await import("../js/src/features/settings-change-log.js");

setLocale("nl");
const bytes = (text) => new TextEncoder().encode(text).length;
const jsonResponse = (status, payload) => ({ ok: status >= 200 && status < 300, status, json: async () => payload });

function stubFetch(handler) {
  const calls = [];
  const original = globalThis.fetch;
  globalThis.fetch = async (url, options = {}) => {
    calls.push({ url: String(url), options });
    return handler(String(url), options);
  };
  return { calls, restore: () => { globalThis.fetch = original; } };
}

test("text is clipped on whole characters within the firmware byte budget", () => {
  assert.equal(clipUtf8("  Stille modus ", 20), "Stille modus");
  const clipped = clipUtf8("°".repeat(30), 20);
  assert.ok(bytes(clipped) <= 20);
  assert.ok(clipped.endsWith("…"));
  assert.equal(clipped.replace("…", ""), "°".repeat((20 - bytes("…")) / 2));
  assert.equal(clipUtf8(null, 10), "");
});

test("entries keep values short and give the label the rest of the record", () => {
  const entry = prepareChangeLogEntry("Minimale koel-aanvoer", "18.0 °C", "20.0 °C");
  assert.deepEqual(entry, { label: "Minimale koel-aanvoer", from: "18.0 °C", to: "20.0 °C" });

  const long = prepareChangeLogEntry("Een heel lange naam van een instelling ".repeat(4), "x".repeat(40), "y".repeat(40));
  assert.ok(bytes(long.from) <= CHANGE_LOG_TEXT_LIMITS.value && bytes(long.to) <= CHANGE_LOG_TEXT_LIMITS.value);
  assert.ok(bytes(long.label) + bytes(long.from) + bytes(long.to) <= CHANGE_LOG_TEXT_LIMITS.text);
  assert.ok(long.label.endsWith("…"));
});

test("values are shown the way the settings page shows them", () => {
  assert.equal(formatChangeLogValue("statusLedsEnabled", true), "Aan");
  assert.equal(formatChangeLogValue("statusLedsEnabled", false), "Uit");
  assert.equal(formatChangeLogValue("statusLedsEnabled", ""), "—");
  assert.equal(formatChangeLogValue("unknownKey", "Schema"), "Schema");
});

test("unchanged values are not logged and failed writes do not throw", async () => {
  const fetchStub = stubFetch(() => jsonResponse(500, {}));
  try {
    assert.equal(await recordSettingsChange("Stille modus", "Uit", "Uit"), false);
    assert.equal(fetchStub.calls.length, 0);
    assert.equal(await recordSettingsChange("Stille modus", "Uit", "Schema"), false);
    assert.equal(fetchStub.calls.length, 1);
  } finally {
    fetchStub.restore();
  }
});

test("a change is posted as a CSRF-protected form with clipped text", async () => {
  state.appView = "overview";
  const fetchStub = stubFetch((url) => (url.endsWith("/auth/status")
    ? jsonResponse(200, { csrf_token: "token-1" })
    : jsonResponse(200, { ok: true, seq: 4 })));
  try {
    assert.equal(await recordSettingsChange("Stille modus", "Uit", "x".repeat(40)), true);
  } finally {
    fetchStub.restore();
  }
  assert.equal(fetchStub.calls.length, 2);
  const post = fetchStub.calls[1];
  assert.equal(post.url, "/openquatt/change-log");
  assert.equal(post.options.method, "POST");
  const body = new URLSearchParams(post.options.body);
  assert.equal(body.get("csrf_token"), "token-1");
  assert.equal(body.get("label"), "Stille modus");
  assert.equal(body.get("from"), "Uit");
  assert.ok(bytes(body.get("to")) <= CHANGE_LOG_TEXT_LIMITS.value);
  assert.match(body.get("at"), /^\d{10}$/);
});

test("different values that shorten to the same text are still logged", async () => {
  state.appView = "overview";
  const fetchStub = stubFetch((url) => (url.endsWith("/auth/status")
    ? jsonResponse(200, { csrf_token: "token-1" })
    : jsonResponse(200, { ok: true, seq: 5 })));
  try {
    assert.equal(await recordSettingsChange("Venster", "Dagelijks tijdvenster", "Dagelijks tijdvenster + handmatig"), true);
  } finally {
    fetchStub.restore();
  }
  const body = new URLSearchParams(fetchStub.calls[1].options.body);
  assert.equal(body.get("from"), body.get("to"));
});

test("the log renders newest first, escapes text and reports older firmware", async () => {
  let renders = 0;
  setRenderCallback(() => { renders += 1; });
  let fetchStub = stubFetch(() => jsonResponse(200, {
    ok: true,
    entries: [
      { seq: 1, at: 1762329600, label: "Oud", from: "1", to: "2" },
      { seq: 2, at: 1762329700, label: "<b>Stille modus</b>", from: "Uit", to: "Schema" },
      { seq: 3, at: 1762329800, label: "" },
    ],
  }));
  try {
    await refreshSettingsChangeLog();
  } finally {
    fetchStub.restore();
  }
  assert.ok(renders > 0);
  assert.deepEqual(getSettingsChangeLogEntries().map((entry) => entry.seq), [2, 1]);
  const markup = renderSettingsChangeLogSection();
  assert.match(markup, /<table class="oq-settings-change-log-table">/);
  assert.match(markup, /&lt;b&gt;Stille modus&lt;\/b&gt;/);
  assert.ok(markup.indexOf("Stille modus") < markup.indexOf("Oud"));

  fetchStub = stubFetch(() => jsonResponse(200, { ok: true, entries: [] }));
  try {
    await refreshSettingsChangeLog();
  } finally {
    fetchStub.restore();
  }
  assert.match(renderSettingsChangeLogSection(), /Nog geen wijzigingen vastgelegd\./);

  fetchStub = stubFetch(() => jsonResponse(404, {}));
  try {
    await refreshSettingsChangeLog();
  } finally {
    fetchStub.restore();
  }
  assert.match(renderSettingsChangeLogSection(), /Deze firmware bewaart nog geen wijzigingslogboek\./);
  setRenderCallback(null);
});
