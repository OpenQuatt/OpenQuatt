import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/dev.html" },
  setTimeout: (...args) => setTimeout(...args),
  clearTimeout: (...args) => clearTimeout(...args),
  localStorage: { getItem: () => null, setItem: () => {} },
  sessionStorage: { getItem: () => null, setItem: () => {}, removeItem: () => {} },
  navigator: {},
  isSecureContext: true,
};

const {
  applyDebugRecordingDeviceStatus,
  prepareDebugRecordingCopy,
  clearPreparedDebugRecordingCopy,
  copyDebugRecordingBundle,
  downloadDebugRecordingRange,
  exportDebugRecordingBundle,
  getDebugRecordingDownloadEndpoint,
  getDebugRecordingRangeLabel,
  getDebugRecordingStatusLabel,
  getSystemRecorderIntroHtml,
  handleDebugRecordingAction,
  renderDebugRecordingHeaderStatus,
  renderDebugRecordingModal,
  restartRollingDebugRecording,
  setDebugRecordingDownloadRange,
  setSystemRecorderEnabled,
} = await import("../js/src/features/debug-recording.js");
const { state } = await import("../js/src/core/state.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const { DEBUG_RECORDING_DOWNLOAD_RANGE_OPTIONS } = await import("../js/src/core/config.js");

// ---------------------------------------------------------------------------
// Mock-device driver (firmware behavior mirror)
// ---------------------------------------------------------------------------

async function loadMockRecorder() {
  const webDir = new URL("../js/", import.meta.url);
  const read = (name) => readFile(new URL(name, webDir), "utf8");
  const [configSource, scenarioSource, incidentScenarioSource, fixtureSource, mockSource] = await Promise.all([
    read("src/core/config.js"),
    read("mock-scenarios.js"),
    read("mock-incident-scenarios.js"),
    read("mock-fixtures.js"),
    read("mock-device.js"),
  ]);
  const configModule = await import(`data:text/javascript;base64,${Buffer.from(configSource).toString("base64")}`);
  const entityDefinitions = Object.values(configModule.ENTITY_DEFS).map(({ domain, name }) => [domain, name]);
  const context = {
    URL,
    URLSearchParams,
    Blob,
    TextEncoder,
    Object,
    Array,
    Math,
    JSON,
    Date,
    Promise,
    Error,
    setTimeout,
    clearTimeout,
    window: {
      __OQ_MOCK_ENTITY_DEFS__: Object.freeze(entityDefinitions),
      crypto: webcrypto,
      location: { href: "http://localhost/dev.html", pathname: "/dev.html", search: "" },
      localStorage: { getItem: () => null, setItem: () => {} },
      sessionStorage: { getItem: () => null, setItem: () => {}, removeItem: () => {} },
      addEventListener: () => {},
      setInterval: () => 0,
      setTimeout,
      clearTimeout,
    },
    document: {
      querySelector: () => null,
      createElement: () => ({
        style: {},
        setAttribute: () => {},
        click: () => {},
        remove: () => {},
        select: () => {},
        focus: () => {},
      }),
      body: { appendChild: () => {}, removeChild: () => {} },
      activeElement: null,
      execCommand: () => false,
    },
  };
  context.window.window = context.window;
  vm.createContext(context);
  vm.runInContext(scenarioSource, context, { filename: "mock-scenarios.js" });
  vm.runInContext(incidentScenarioSource, context, { filename: "mock-incident-scenarios.js" });
  vm.runInContext(fixtureSource, context, { filename: "mock-fixtures.js" });
  vm.runInContext(mockSource, context, { filename: "mock-device.js" });
  const fetch = context.window.fetch;
  assert.equal(typeof fetch, "function", "mock installeert een fetch-mock");

  const call = async (path, init) => {
    const response = await fetch(`http://localhost${path}`, init);
    return { status: response.status, ok: response.ok, body: await response.json() };
  };
  const getStatus = () => call("/openquatt/debug-recording/status");
  const post = (path, params = {}) => call(path, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(params).toString(),
  });
  return { call, getStatus, post };
}

async function mockCsrf(mock) {
  const status = await mock.getStatus();
  assert.equal(status.status, 200);
  return String(status.body.csrf_token || "");
}

// ---------------------------------------------------------------------------
// Koude boot: zonder webapp wordt automatisch historie opgebouwd
// ---------------------------------------------------------------------------

test("koude boot zonder webapp start automatisch een rolling opname", async () => {
  const mock = await loadMockRecorder();
  const status = await mock.getStatus();
  assert.equal(status.status, 200);
  assert.equal(status.body.available, true);
  assert.equal(status.body.enabled, true);
  assert.equal(status.body.active, true);
  assert.equal(status.body.mode, "rolling");
  assert.ok(status.body.sample_count > 100, `verwacht opgebouwde historie, kreeg ${status.body.sample_count}`);
  assert.ok(status.body.retained_duration_s > 3600, "verwacht meer dan een uur retentie na 2u13m mock-uptime");
  assert.ok(!("frozen" in status.body), "frozen is verwijderd uit het statusmodel");
});

// ---------------------------------------------------------------------------
// Range-export: zelfstandig rebased venster
// ---------------------------------------------------------------------------

test("download van de laatste 15 min is een zelfstandig gerebased venster", async () => {
  const mock = await loadMockRecorder();
  const full = (await mock.call("/openquatt/debug-recording/download")).body;
  const range = (await mock.call("/openquatt/debug-recording/download-range?last_minutes=15")).body;

  assert.equal(range.format, "openquatt-debug-device-v1");
  // Exacte 10s-stappen in de mock: 900s venster + rand = 91 samples.
  assert.equal(range.recording.sample_count, 91);
  assert.equal(range.samples.length, 91);
  assert.equal(range.samples[0][0], 0, "offsets zijn gerebased vanaf het vensterbegin");
  assert.equal(range.samples[range.samples.length - 1][0], 900);
  for (let index = 1; index < range.samples.length; index += 1) {
    assert.equal(range.samples[index][0] - range.samples[index - 1][0], 10);
  }
  // Nieuwe initial hoort bij het vensterbegin, niet bij de opnamestart.
  const fullFirstOffset = full.samples[full.samples.length - 91][0];
  assert.equal(range.recording.started_at_ms, full.recording.started_at_ms + fullFirstOffset * 1000);
  assert.ok(range.recording.started_at_ms > full.recording.started_at_ms);
  // Reconstructie: initial + delta's moeten de laatste samplewaarden opleveren.
  const columns = range.columns.length;
  const reconstructed = new Array(columns).fill(null);
  for (const [column, value] of range.initial) {
    reconstructed[column] = value;
  }
  for (const [, deltas] of range.samples) {
    for (const [column, value] of deltas) {
      reconstructed[column] = value;
    }
  }
  const fullValues = new Array(full.columns.length).fill(null);
  for (const [column, value] of full.initial) {
    fullValues[column] = value;
  }
  for (const [, deltas] of full.samples) {
    for (const [column, value] of deltas) {
      fullValues[column] = value;
    }
  }
  assert.deepEqual(reconstructed, fullValues, "venster reconstrueert dezelfde eindtoestand als de volledige buffer");
  assert.equal(range.recording.retained_duration_s, 900);
  assert.ok(range.recording.event_count <= full.recording.event_count);
});

test("meer vragen dan beschikbaar exporteert gewoon alles", async () => {
  const mock = await loadMockRecorder();
  const csrf = await mockCsrf(mock);
  await mock.post("/openquatt/debug-recording/restart", { csrf_token: csrf });
  const full = (await mock.call("/openquatt/debug-recording/download")).body;
  const range = (await mock.call("/openquatt/debug-recording/download-range?last_minutes=15")).body;
  assert.ok(full.recording.sample_count < 10, "nieuwe opname heeft nog nauwelijks samples");
  assert.equal(range.recording.sample_count, full.recording.sample_count);
  assert.equal(range.recording.started_at_ms, full.recording.started_at_ms);
  if (range.samples.length) {
    assert.equal(range.samples[0][0], 0);
  }
});

// ---------------------------------------------------------------------------
// Opt-out: stoppen zonder te wissen, opnieuw inschakelen start schoon
// ---------------------------------------------------------------------------

test("uitschakelen stopt sampling maar de buffer blijft downloadbaar", async () => {
  const mock = await loadMockRecorder();
  const csrf = await mockCsrf(mock);
  const before = (await mock.getStatus()).body;
  assert.equal(before.active, true);
  const disabled = (await mock.post("/openquatt/debug-recording/enabled", { enabled: "0", csrf_token: csrf })).body;
  assert.equal(disabled.enabled, false);
  assert.equal(disabled.active, false);
  const after = (await mock.getStatus()).body;
  assert.equal(after.enabled, false);
  assert.equal(after.active, false);
  assert.equal(after.sample_count, before.sample_count, "geen nieuwe samples na uitschakelen");
  const download = await mock.call("/openquatt/debug-recording/download");
  assert.equal(download.status, 200);
  assert.equal(download.body.recording.sample_count, before.sample_count);
});

test("opnieuw inschakelen start een nieuwe opname en wist de oude buffer", async () => {
  const mock = await loadMockRecorder();
  const csrf = await mockCsrf(mock);
  const before = (await mock.getStatus()).body;
  await mock.post("/openquatt/debug-recording/enabled", { enabled: "0", csrf_token: csrf });
  const enabled = (await mock.post("/openquatt/debug-recording/enabled", { enabled: "1", csrf_token: csrf })).body;
  assert.equal(enabled.enabled, true);
  assert.equal(enabled.active, true);
  assert.notEqual(enabled.recording_id, before.recording_id, "nieuwe recording_id");
  assert.ok(enabled.sample_count < before.sample_count, "oude buffer is gewist");
});

test("oude start-route respecteert de opt-out", async () => {
  const mock = await loadMockRecorder();
  const csrf = await mockCsrf(mock);
  await mock.post("/openquatt/debug-recording/enabled", { enabled: "0", csrf_token: csrf });
  const rolling = await mock.post("/openquatt/debug-recording/start?rolling=1", { csrf_token: csrf });
  assert.equal(rolling.status, 409);
  assert.equal(rolling.body.error, "recorder_disabled");
  const after = (await mock.getStatus()).body;
  assert.equal(after.enabled, false);
  assert.equal(after.active, false);
});

test("tijdgebonden start is verwijderd en laat de rolling historie intact", async () => {
  const mock = await loadMockRecorder();
  const csrf = await mockCsrf(mock);
  const before = (await mock.getStatus()).body;
  const result = await mock.post("/openquatt/debug-recording/start?duration_s=900", { csrf_token: csrf });
  assert.equal(result.status, 410);
  assert.equal(result.body.error, "timed_recording_removed");
  const after = (await mock.getStatus()).body;
  assert.equal(after.active, true);
  assert.equal(after.mode, "rolling");
  assert.equal(after.recording_id, before.recording_id);
  assert.equal(after.sample_count, before.sample_count);
  assert.equal(after.buffer_size, 2 * 1024 * 1024);
  assert.equal(Object.hasOwn(after, "duration_s"), false);
  assert.equal(Object.hasOwn(after, "remaining_s"), false);
});

test("2 en 6 uur export blijven zelfstandige vensters, begrensd tot beschikbare historie", async () => {
  const mock = await loadMockRecorder();
  const twoHours = (await mock.call("/openquatt/debug-recording/download-range?last_minutes=120")).body;
  const sixHours = (await mock.call("/openquatt/debug-recording/download-range?last_minutes=360")).body;
  assert.equal(twoHours.recording.duration_s, 7200);
  assert.equal(twoHours.samples.length, 721);
  assert.deepEqual(twoHours.samples[0], [0, []]);
  assert.ok(sixHours.recording.duration_s >= twoHours.recording.duration_s);
  assert.ok(sixHours.recording.duration_s < 21600);
  assert.deepEqual(sixHours.samples[0], [0, []]);
});

test("dubbel inschakelen is idempotent en wist niets", async () => {
  const mock = await loadMockRecorder();
  const csrf = await mockCsrf(mock);
  const before = (await mock.getStatus()).body;
  assert.ok(before.sample_count > 100);
  const again = (await mock.post("/openquatt/debug-recording/enabled", { enabled: "1", csrf_token: csrf })).body;
  assert.equal(again.enabled, true);
  assert.equal(again.active, true);
  assert.equal(again.recording_id, before.recording_id, "geen nieuwe opname");
  assert.equal(again.sample_count, before.sample_count, "buffer ongewijzigd");
});

test("herstarten respecteert de opt-out", async () => {
  const mock = await loadMockRecorder();
  const csrf = await mockCsrf(mock);
  const before = (await mock.getStatus()).body;
  await mock.post("/openquatt/debug-recording/enabled", { enabled: "0", csrf_token: csrf });
  const restarted = await mock.post("/openquatt/debug-recording/restart", { csrf_token: csrf });
  assert.equal(restarted.status, 409);
  const after = (await mock.getStatus()).body;
  assert.equal(after.enabled, false);
  assert.equal(after.active, false, "geen sampling na geweigerde restart");
  assert.equal(after.sample_count, before.sample_count, "buffer ongewijzigd");
});

test("volledige export beschrijft het bewaarde venster, niet de looptijd", async () => {
  const mock = await loadMockRecorder();
  const full = (await mock.call("/openquatt/debug-recording/download")).body;
  assert.ok(full.recording.sample_count > 100);
  assert.equal(full.recording.duration_s, full.recording.retained_duration_s);
  assert.equal(
    full.recording.duration_s,
    full.samples[full.samples.length - 1][0] - full.samples[0][0],
  );
});

test("nieuwe opname starten wist historie zonder browserconfiguratie", async () => {
  const mock = await loadMockRecorder();
  const csrf = await mockCsrf(mock);
  const before = (await mock.getStatus()).body;
  assert.ok(before.sample_count > 100);
  const restarted = (await mock.post("/openquatt/debug-recording/restart", { csrf_token: csrf })).body;
  assert.equal(restarted.active, true);
  assert.notEqual(restarted.recording_id, before.recording_id);
  assert.ok(restarted.sample_count <= 2, `buffer gewist, kreeg ${restarted.sample_count} samples`);
});

// ---------------------------------------------------------------------------
// Webapp: range wordt centraal naar het device gestuurd
// ---------------------------------------------------------------------------

function seedFeatureStatus(overrides = {}) {
  clearPreparedDebugRecordingCopy();
  state.debugRecordingDeviceStatus = {
    ok: true,
    available: true,
    enabled: true,
    active: true,
    mode: "rolling",
    rolling: true,
    recording_id: 7,
    sample_count: 500,
    retained_duration_s: 4500,
    sample_capacity: 1400,
    estimated_size: 9000,
    event_count: 12,
    csrf_token: "feature-csrf-token",
    ...overrides,
  };
  state.debugRecordingActive = Boolean(state.debugRecordingDeviceStatus.active);
  state.debugRecordingBusy = false;
  state.debugRecordingPendingEnabled = null;
  state.debugRecordingError = "";
  state.debugRecordingNotice = "";
  state.debugRecordingDownloadRange = 15;
  state.systemModal = "debug-recording";
  const originalSetTimeout = window.setTimeout;
  const originalClearTimeout = window.clearTimeout;
  window.setTimeout = () => 0;
  window.clearTimeout = () => {};
  return () => {
    clearPreparedDebugRecordingCopy();
    window.setTimeout = originalSetTimeout;
    window.clearTimeout = originalClearTimeout;
  };
}

test("download- en kopieer-endpoints gebruiken het geselecteerde bereik", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  const originalClipboard = window.navigator.clipboard;
  t.after(() => {
    window.fetch = originalFetch;
    window.navigator.clipboard = originalClipboard;
    restoreTimers();
  });
  const requestedUrls = [];
  let copiedText = "";
  window.navigator.clipboard = { writeText: async (text) => { copiedText = text; } };
  window.isSecureContext = true;
  window.fetch = async (url) => {
    requestedUrls.push(String(url));
    return { ok: true, status: 200, json: async () => ({ recording: { active: true, recording_id: 7 } }) };
  };

  setDebugRecordingDownloadRange(30);
  await exportDebugRecordingBundle("download");
  await prepareDebugRecordingCopy();
  await exportDebugRecordingBundle("copy");

  assert.equal(requestedUrls.length, 2);
  for (const url of requestedUrls) {
    assert.match(url, /download-range\?last_minutes=30/, `bereik naar device gestuurd: ${url}`);
  }
  assert.ok(copiedText.includes("recording"), "kopie bevat de bundle");
  assert.equal(getDebugRecordingDownloadEndpoint(0).endsWith("/download"), true);
  assert.match(getDebugRecordingDownloadEndpoint(60), /download-range\?last_minutes=60/);
});

test("herstart en toggle gebruiken CSRF-beveiligde device-actions", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  t.after(() => {
    window.fetch = originalFetch;
    restoreTimers();
  });
  const posted = [];
  window.fetch = async (url, options = {}) => {
    if (options.method === "POST") {
      posted.push({ url: String(url), body: String(options.body) });
      return { ok: true, status: 200, json: async () => ({ ok: true, available: true, enabled: false, active: false, csrf_token: "feature-csrf-token" }) };
    }
    return { ok: true, status: 200, json: async () => ({ ok: true }) };
  };

  await restartRollingDebugRecording();
  assert.match(state.debugRecordingNotice, /Nieuwe opname gestart/);
  await setSystemRecorderEnabled(false);
  assert.match(state.debugRecordingNotice, /uitgeschakeld/);

  assert.equal(posted.length, 2);
  assert.match(posted[0].url, /\/restart$/);
  assert.match(posted[1].url, /\/enabled$/);
  assert.match(posted[1].body, /enabled=0/);
  for (const { body } of posted) {
    assert.match(body, /csrf_token=feature-csrf-token/);
  }
});

// ---------------------------------------------------------------------------
// UX: Systeemrecorder-copy, headerindicator en analyser
// ---------------------------------------------------------------------------

test("normaal actieve recorder geeft geen headerbadge, afwijking wel", () => {
  seedFeatureStatus({ available: true, enabled: true, active: true });
  applyDebugRecordingDeviceStatus(state.debugRecordingDeviceStatus);
  assert.equal(renderDebugRecordingHeaderStatus(), "");

  applyDebugRecordingDeviceStatus({ ...state.debugRecordingDeviceStatus, enabled: false, active: false });
  assert.match(renderDebugRecordingHeaderStatus(), /Systeemrecorder uitgeschakeld/);

  applyDebugRecordingDeviceStatus({ ...state.debugRecordingDeviceStatus, enabled: true, active: false });
  assert.match(renderDebugRecordingHeaderStatus(), /Systeemrecorder niet actief/);

  applyDebugRecordingDeviceStatus({ available: false });
  assert.match(renderDebugRecordingHeaderStatus(), /Systeemrecorder niet beschikbaar/);

  // Afwijkende toestanden zijn warnings, geen groene success-stijl.
  applyDebugRecordingDeviceStatus({ ...state.debugRecordingDeviceStatus, enabled: false, active: false });
  assert.doesNotMatch(renderDebugRecordingHeaderStatus(), /--ready/);
});

test("modal spreekt Systeemrecorder met export als hoofdactie", () => {
  seedFeatureStatus({ available: true, enabled: true, active: true });
  applyDebugRecordingDeviceStatus(state.debugRecordingDeviceStatus);
  const markup = renderDebugRecordingModal();
  assert.match(markup, /Systeemrecorder/);
  assert.match(markup, /Doorlopende opname/);
  assert.match(markup, /role="switch"/);
  assert.match(markup, /Beschikbaar/);
  assert.match(markup, /Lokaal opgeslagen · elke 10 s/);
  assert.match(markup, /oq-debug-recording-samples/);
  assert.match(markup, /Kies hoeveel van de beschikbare historie je wilt exporteren/);
  assert.match(markup, /Laatste 15 minuten/);
  assert.match(markup, /Download bestand<\/button>/);
  assert.match(markup, /Kopieer gegevens<\/button>/);
  assert.match(markup, /Kopieer \+ analyser<\/button>/);
  assert.match(
    markup,
    /oq-debug-recording-exportactions">[\s\S]*?Download bestand<\/button>[\s\S]*?Kopieer gegevens<\/button>[\s\S]*?Kopieer \+ analyser<\/button>/,
  );
  assert.match(markup, /Kopieer \+ analyser<\/button>/);
  assert.match(markup, /data-oq-action="copy-debug-recording-analyser"/);
  assert.doesNotMatch(markup, /oq-debug-recording-switchlabel/);
  assert.match(markup, /<details class="oq-debug-recording-manage">/);
  assert.match(markup, /Recorderbeheer/);
  assert.match(markup, /Nieuwe opname starten/);
  assert.match(markup, /Wist de huidige historie en start een nieuwe opname/);
  // Analyser is een inline link in de intro, geen losse knop.
  assert.match(markup, /<a href="https:\/\/openheatpumps\.nl" target="_blank" rel="noopener noreferrer">/);
  assert.match(markup, /OpenHeatPumps analyser/);
  assert.match(markup, /niet automatisch verzonden/);
  assert.doesNotMatch(markup, /Open analyser<\/a>|Open analyser<\/button>/);
  assert.doesNotMatch(markup, /Debugopname/);
  assert.doesNotMatch(markup, /Start rolling/);
  assert.doesNotMatch(markup, /Statuswijzigingen/);
  assert.doesNotMatch(markup, /freeze-debug-recording/);
  assert.doesNotMatch(markup, /toggle-system-recorder/);
  for (const option of DEBUG_RECORDING_DOWNLOAD_RANGE_OPTIONS) {
    assert.ok(markup.includes(`data-last-minutes="${option.minutes}"`), `bereik ${option.minutes} aanwezig`);
  }
  assert.equal(getSystemRecorderIntroHtml().includes("https://openheatpumps.nl"), true);
});

test("uitschakelen vraagt eerst om bevestiging", () => {
  seedFeatureStatus({ available: true, enabled: true, active: true });
  applyDebugRecordingDeviceStatus(state.debugRecordingDeviceStatus);
  state.debugRecordingConfirmDisable = false;

  handleDebugRecordingAction("request-disable-system-recorder");
  assert.equal(state.debugRecordingConfirmDisable, true);
  const markup = renderDebugRecordingModal();
  assert.match(markup, /Doorlopende opname uitschakelen\?/);
  assert.match(markup, /De huidige historie blijft beschikbaar/);
  assert.match(markup, /Annuleren/);
  assert.match(markup, /Uitschakelen/);

  handleDebugRecordingAction("cancel-disable-system-recorder");
  assert.equal(state.debugRecordingConfirmDisable, false);
  assert.doesNotMatch(renderDebugRecordingModal(), /Doorlopende opname uitschakelen\?/);
});

test("recorderbeheer klapt state-gestuurd open en blijft open bij re-render", () => {
  seedFeatureStatus({ available: true, enabled: true, active: true });
  applyDebugRecordingDeviceStatus(state.debugRecordingDeviceStatus);
  state.debugRecordingManageOpen = false;
  assert.doesNotMatch(renderDebugRecordingModal(), /<details class="oq-debug-recording-manage" open>/);

  let prevented = false;
  const summary = {
    closest: (selector) => (selector === ".oq-debug-recording-manage" ? { hasAttribute: () => false } : null),
  };
  handleDebugRecordingAction("toggle-recorder-manage", summary, { preventDefault: () => { prevented = true; } });
  assert.equal(prevented, true);
  assert.equal(state.debugRecordingManageOpen, true);
  // Re-render (zoals de statuspoll doet) moet de open toestand behouden.
  assert.match(renderDebugRecordingModal(), /<details class="oq-debug-recording-manage" open>/);
  assert.match(renderDebugRecordingModal(), /Nieuwe opname starten/);

  const openSummary = {
    closest: (selector) => (selector === ".oq-debug-recording-manage" ? { hasAttribute: () => true } : null),
  };
  handleDebugRecordingAction("toggle-recorder-manage", openSummary, { preventDefault: () => {} });
  assert.equal(state.debugRecordingManageOpen, false);
  assert.doesNotMatch(renderDebugRecordingModal(), /<details class="oq-debug-recording-manage" open>/);
});

test("rangebeschrijving benoemt het venster voluit", () => {
  seedFeatureStatus({ available: true, enabled: true, active: true, retained_duration_s: 4500 });
  applyDebugRecordingDeviceStatus(state.debugRecordingDeviceStatus);
  assert.equal(getDebugRecordingRangeLabel(15), "Laatste 15 minuten");
  assert.equal(getDebugRecordingRangeLabel(30), "Laatste 30 minuten");
  assert.match(getDebugRecordingRangeLabel(0), /Volledige beschikbare historie · /);
});

test("stille poll rendert de open modal niet opnieuw maar patcht de getallen", async (t) => {
  const restoreTimers = seedFeatureStatus({
    available: true,
    enabled: true,
    active: true,
    mode: "rolling",
    rolling: true,
    sample_count: 500,
    retained_duration_s: 4500,
  });
  applyDebugRecordingDeviceStatus(state.debugRecordingDeviceStatus);
  const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
  const originalFetch = window.fetch;
  const originalRoot = state.root;
  t.after(() => {
    window.fetch = originalFetch;
    state.root = originalRoot;
    setRenderCallback(null);
    restoreTimers();
  });
  let renders = 0;
  setRenderCallback(() => { renders += 1; });
  const retainedEl = { textContent: "oud" };
  const samplesEl = { textContent: "oud" };
  const availabilityEl = {
    textContent: "oud",
    querySelector: (inner) => {
      if (inner === "[data-oq-recorder-retained]") return retainedEl;
      if (inner === "[data-oq-recorder-samples]") return samplesEl;
      return null;
    },
  };
  const rangeEl = { textContent: "oud" };
  state.root = {
    querySelector: (selector) => (selector === ".oq-debug-recording-modal"
      ? { querySelector: (inner) => (inner === "[data-oq-recorder-availability]" ? availabilityEl : rangeEl) }
      : null),
  };
  window.fetch = async () => ({
    ok: true,
    status: 200,
    json: async () => ({
      ...state.debugRecordingDeviceStatus,
      sample_count: 520,
      retained_duration_s: 4700,
    }),
  });

  const { refreshDebugRecordingDeviceStatus } = await import("../js/src/features/debug-recording.js");
  await prepareDebugRecordingCopy();
  renders = 0;
  await refreshDebugRecordingDeviceStatus({ silent: true });

  assert.equal(renders, 0, "geen volledige re-render bij ongewijzigde toestand");
  assert.equal(retainedEl.textContent, "1u 18m");
  assert.equal(samplesEl.textContent, "(520 samples)");
  assert.equal(rangeEl.textContent, "Laatste 15 minuten");
});

test("stille poll rendert wel opnieuw bij een toestandswissel", async (t) => {
  const restoreTimers = seedFeatureStatus({ available: true, enabled: true, active: true });
  applyDebugRecordingDeviceStatus(state.debugRecordingDeviceStatus);
  const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
  const originalFetch = window.fetch;
  t.after(() => {
    window.fetch = originalFetch;
    setRenderCallback(null);
    restoreTimers();
  });
  let renders = 0;
  setRenderCallback(() => { renders += 1; });
  window.fetch = async () => ({
    ok: true,
    status: 200,
    json: async () => ({ ...state.debugRecordingDeviceStatus, enabled: false, active: false }),
  });

  const { refreshDebugRecordingDeviceStatus } = await import("../js/src/features/debug-recording.js");
  await refreshDebugRecordingDeviceStatus({ silent: true });

  assert.ok(renders > 0, "wel opnieuw renderen bij toestandswissel");
  assert.equal(state.debugRecordingActive, false);
});

for (const enabled of [false, true]) {
  for (const failure of ["persistence_failed", "antwoord verloren"]) {
    test(`onbevestigde recorderwaarde ${enabled} blijft een fout na ${failure}, ook bij passende runtime`, async (t) => {
      const restoreTimers = seedFeatureStatus({ enabled, active: enabled });
      const originalFetch = window.fetch;
      t.after(() => {
        window.fetch = originalFetch;
        state.debugRecordingDevicePollTimer = null;
        restoreTimers();
      });
      let confirmed = false;
      let posts = 0;
      const delays = [];
      window.setTimeout = (_callback, delay) => {
        delays.push(delay);
        return 7;
      };
      window.fetch = async (_url, options = {}) => {
        if (options.method === "POST") {
          posts += 1;
          if (!confirmed) {
            if (failure === "antwoord verloren") throw new Error(failure);
            return { ok: false, status: 500, json: async () => ({ ok: false, error: failure }) };
          }
        }
        return {
          ok: true,
          status: 200,
          json: async () => ({ ...state.debugRecordingDeviceStatus, enabled, active: enabled }),
        };
      };

      for (let attempt = 0; attempt < 2; attempt += 1) {
        await setSystemRecorderEnabled(enabled);
        assert.equal(state.debugRecordingNotice, "", "runtime is geen bewijs van duurzame opslag");
        assert.match(state.debugRecordingError, /kon niet worden bevestigd.*Probeer opnieuw/);
        assert.equal(state.debugRecordingBusy, false);
        const { refreshDebugRecordingDeviceStatus } = await import("../js/src/features/debug-recording.js");
        await refreshDebugRecordingDeviceStatus({ silent: true });
        assert.equal(state.debugRecordingNotice, "");
        assert.match(state.debugRecordingError, /kon niet worden bevestigd/);
      }
      assert.equal(posts, 2, "geen automatische opslagretry");
      assert.ok(delays.includes(4000), "status blijft na een fout begrensd verversen");

      confirmed = true;
      await setSystemRecorderEnabled(enabled);
      assert.equal(posts, 3);
      assert.equal(state.debugRecordingError, "");
      assert.match(state.debugRecordingNotice, enabled ? /ingeschakeld/ : /uitgeschakeld/);
      if (enabled) assert.doesNotMatch(state.debugRecordingNotice, /nieuwe opname|buffer.*gewist/i);
    });
  }
}

function clickRecorderButton(action) {
  const button = renderDebugRecordingModal().match(new RegExp(`<button\\b[^>]*data-oq-action="${action}"[^>]*>`));
  assert.ok(button, `zichtbare actie ${action}`);
  assert.doesNotMatch(button[0], /\bdisabled\b/, `actie ${action} is bedienbaar`);
  assert.equal(handleDebugRecordingAction(action), true);
}

const settleRecorderAction = () => new Promise((resolve) => setImmediate(resolve));

for (const enabled of [false, true]) {
  for (const applied of [false, true]) {
    test(`zichtbare retry bewaart keuze ${enabled} na verloren ACK, server toegepast=${applied}`, async (t) => {
      const restoreTimers = seedFeatureStatus({ enabled: !enabled, active: !enabled });
      const originalFetch = window.fetch;
      const server = { ...state.debugRecordingDeviceStatus };
      const posted = [];
      t.after(() => {
        window.fetch = originalFetch;
        state.debugRecordingPendingEnabled = null;
        setLocale("nl", { persist: false, applyDocument: false, notify: false });
        restoreTimers();
      });
      const apply = (desired) => {
        if (desired && !server.enabled) {
          server.recording_id += 1;
          server.sample_count = 1;
        }
        server.enabled = desired;
        server.active = desired;
      };
      window.fetch = async (url, options = {}) => {
        if (options.method === "POST") {
          assert.match(String(url), /\/enabled$/);
          const desired = new URLSearchParams(options.body).get("enabled") === "1";
          posted.push(desired);
          if (posted.length === 1) {
            if (applied) apply(desired);
            throw new Error("antwoord verloren");
          }
          if (posted.length === 2) {
            return { ok: false, status: 500, json: async () => ({ ok: false, error: "persistence_failed" }) };
          }
          apply(desired);
        }
        return { ok: true, status: 200, json: async () => ({ ...server }) };
      };

      clickRecorderButton("request-disable-system-recorder");
      if (!enabled) clickRecorderButton("confirm-disable-system-recorder");
      assert.doesNotMatch(renderDebugRecordingModal(), /data-oq-action="retry-system-recorder-enabled"/, "geen fout/retrybox tijdens de eerste lopende POST");
      await settleRecorderAction();
      assert.deepEqual(posted, [enabled]);
      assert.equal(state.debugRecordingPendingEnabled, enabled);
      const historyAfterLostAck = [server.recording_id, server.sample_count];
      const { refreshDebugRecordingDeviceStatus } = await import("../js/src/features/debug-recording.js");
      await refreshDebugRecordingDeviceStatus({ silent: true });
      assert.equal(state.debugRecordingDeviceStatus.enabled, server.enabled);
      assert.equal(state.debugRecordingPendingEnabled, enabled, "runtimepoll bevestigt de opslagkeuze niet");
      assert.equal(state.debugRecordingNotice, "");
      assert.deepEqual(posted, [enabled], "polling herhaalt geen POST");
      assert.match(renderDebugRecordingModal(), new RegExp(`data-oq-action="request-disable-system-recorder"[^>]*disabled`));
      for (const action of ["request-disable-system-recorder", "confirm-disable-system-recorder", "restart-rolling-debug-recording"]) {
        handleDebugRecordingAction(action); // Ook een oude DOM-knop mag geen omgekeerde write/reset starten.
      }
      await settleRecorderAction();
      assert.deepEqual(posted, [enabled]);
      assert.deepEqual([server.recording_id, server.sample_count], historyAfterLostAck);

      setLocale("en", { persist: false, applyDocument: false, notify: false });
      assert.match(renderDebugRecordingModal(), /Retry<\/button>/);
      assert.match(renderDebugRecordingModal(), enabled ? /Enabling continuous recording could not be confirmed/ : /Disabling continuous recording could not be confirmed/);
      setLocale("nl", { persist: false, applyDocument: false, notify: false });
      clickRecorderButton("retry-system-recorder-enabled");
      handleDebugRecordingAction("retry-system-recorder-enabled"); // Dubbele klik tijdens POST.
      await settleRecorderAction();
      assert.deepEqual(posted, [enabled, enabled], "retry verstuurt exact de oorspronkelijke boolean");
      assert.equal(state.debugRecordingPendingEnabled, enabled, "ook een mislukte expliciete retry bewaart de keuze");
      assert.match(state.debugRecordingError, /kon niet worden bevestigd/);
      await refreshDebugRecordingDeviceStatus({ silent: true });
      assert.deepEqual(posted, [enabled, enabled], "ook na herhaald falen geen automatische POST");
      clickRecorderButton("retry-system-recorder-enabled");
      await settleRecorderAction();
      assert.deepEqual(posted, [enabled, enabled, enabled]);
      assert.equal(state.debugRecordingPendingEnabled, null);
      assert.equal(state.debugRecordingDeviceStatus.enabled, enabled);
      assert.doesNotMatch(renderDebugRecordingModal(), /data-oq-action="retry-system-recorder-enabled"/);
      if (applied || !enabled) {
        assert.deepEqual([server.recording_id, server.sample_count], historyAfterLostAck, "idempotente retry wist geen historie");
      } else {
        assert.equal(server.recording_id, historyAfterLostAck[0] + 1, "alleen de bedoelde enabletransitie begint een nieuwe opname");
      }
    });
  }
}

for (const enabled of [false, true]) {
  test(`een HTTP200 met andere recorderwaarde bevestigt keuze ${enabled} niet`, async (t) => {
    const restoreTimers = seedFeatureStatus({ enabled: !enabled, active: !enabled });
    const originalFetch = window.fetch;
    t.after(() => {
      window.fetch = originalFetch;
      state.debugRecordingPendingEnabled = null;
      restoreTimers();
    });
    let posts = 0;
    window.fetch = async () => {
      posts += 1;
      return { ok: true, status: 200, json: async () => ({ ...state.debugRecordingDeviceStatus }) };
    };
    clickRecorderButton("request-disable-system-recorder");
    if (!enabled) clickRecorderButton("confirm-disable-system-recorder");
    await settleRecorderAction();
    assert.equal(posts, 1);
    assert.equal(state.debugRecordingPendingEnabled, enabled);
    assert.equal(state.debugRecordingNotice, "");
    assert.match(state.debugRecordingError, /bevestiging bevat niet de gekozen recorderwaarde/);
    assert.match(renderDebugRecordingModal(), /data-oq-action="retry-system-recorder-enabled"/);
  });
}

test("lopende keuze overleeft sluiten, heropenen en een verouderde poll zonder dubbele write", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  t.after(() => {
    window.fetch = originalFetch;
    state.debugRecordingPendingEnabled = null;
    restoreTimers();
  });
  let releasePoll, rejectPost;
  let gets = 0;
  const posted = [];
  window.fetch = (url, options = {}) => {
    if (options.method === "POST") {
      posted.push(new URLSearchParams(options.body).get("enabled"));
      return new Promise((_resolve, reject) => { rejectPost = reject; });
    }
    gets += 1;
    return new Promise((resolve) => { releasePoll = resolve; });
  };
  const { refreshDebugRecordingDeviceStatus } = await import("../js/src/features/debug-recording.js");
  const oldPoll = refreshDebugRecordingDeviceStatus({ silent: true });
  clickRecorderButton("request-disable-system-recorder");
  clickRecorderButton("confirm-disable-system-recorder");
  state.systemModal = "";
  handleDebugRecordingAction("open-debug-recording-modal");
  await settleRecorderAction();
  assert.equal(gets, 1, "heropenen begint geen concurrerende poll tijdens de write");
  assert.equal(state.debugRecordingBusy, true);
  releasePoll({ ok: true, status: 200, json: async () => ({ ...state.debugRecordingDeviceStatus, sample_count: 999 }) });
  await oldPoll;
  assert.equal(state.debugRecordingDeviceStatus.sample_count, 500, "oude poll overschrijft geen mutatiecontext");
  assert.equal(state.debugRecordingBusy, true);
  handleDebugRecordingAction("retry-system-recorder-enabled");
  handleDebugRecordingAction("confirm-disable-system-recorder");
  assert.deepEqual(posted, ["0"]);
  state.systemModal = "";
  rejectPost(new Error("antwoord verloren"));
  await settleRecorderAction();
  assert.equal(state.systemModal, "", "late response opent de gesloten modal niet");
  assert.equal(state.debugRecordingPendingEnabled, false);
  window.fetch = async () => ({ ok: true, status: 200, json: async () => ({
    ...state.debugRecordingDeviceStatus, enabled: false, active: false,
  }) });
  handleDebugRecordingAction("open-debug-recording-modal");
  await settleRecorderAction();
  assert.equal(state.debugRecordingPendingEnabled, false);
  assert.match(renderDebugRecordingModal(), /data-oq-action="retry-system-recorder-enabled"/);
  assert.deepEqual(posted, ["0"], "heropenen verifieert alleen runtime en herhaalt de keuze niet automatisch");
});

test("een verloren restartantwoord wordt via status gereconcilieerd", async (t) => {
  const restoreTimers = seedFeatureStatus({ active: true, rolling: true, recording_id: 41, sample_count: 60 });
  const originalFetch = window.fetch;
  const originalSetTimeout = window.setTimeout;
  const originalClearTimeout = window.clearTimeout;
  t.after(() => {
    window.fetch = originalFetch;
    window.setTimeout = originalSetTimeout;
    window.clearTimeout = originalClearTimeout;
    state.debugRecordingDevicePollTimer = null;
    restoreTimers();
  });
  window.setTimeout = () => 0;
  window.clearTimeout = () => {};
  window.fetch = async (_url, options = {}) => {
    if (options.method === "POST") throw new Error("antwoord verloren");
    return {
      ok: true,
      status: 200,
      json: async () => ({
        ok: true,
        available: true,
        enabled: true,
        active: true,
        mode: "rolling",
        rolling: true,
        recording_id: 42,
        sample_count: 1,
        csrf_token: "feature-csrf-token",
      }),
    };
  };

  await restartRollingDebugRecording();

  assert.equal(state.debugRecordingActive, true);
  assert.equal(state.debugRecordingError, "");
  assert.match(state.debugRecordingNotice, /bevestiging was vertraagd/);
});

test("mislukte eerste statusfetch plant een begrensde retry", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  const originalSetTimeout = window.setTimeout;
  const originalClearTimeout = window.clearTimeout;
  t.after(() => {
    window.fetch = originalFetch;
    window.setTimeout = originalSetTimeout;
    window.clearTimeout = originalClearTimeout;
    state.debugRecordingDevicePollTimer = null;
    state.debugRecordingDeviceStatus = null;
    restoreTimers();
  });
  state.debugRecordingDeviceStatus = null;
  state.debugRecordingActive = false;
  state.systemModal = "";
  const delays = [];
  window.fetch = async () => {
    throw new Error("boot-transient");
  };
  window.setTimeout = (_callback, delay) => {
    delays.push(delay);
    return 7;
  };
  window.clearTimeout = () => {};

  const { refreshDebugRecordingDeviceStatus } = await import("../js/src/features/debug-recording.js");
  await refreshDebugRecordingDeviceStatus({ silent: true });

  assert.equal(state.debugRecordingDeviceStatus.available, false);
  assert.match(state.debugRecordingError, /boot-transient/);
  assert.deepEqual(delays, [4000], "één begrensde retry na initieel falen");
});

test("statuslabels volgen enabled/active zonder frozen-toestanden", () => {
  seedFeatureStatus({ available: true, enabled: true, active: true });
  applyDebugRecordingDeviceStatus(state.debugRecordingDeviceStatus);
  assert.equal(getDebugRecordingStatusLabel(), "Actief");

  applyDebugRecordingDeviceStatus({ ...state.debugRecordingDeviceStatus, enabled: false, active: false });
  assert.equal(getDebugRecordingStatusLabel(), "Uitgeschakeld");

  applyDebugRecordingDeviceStatus({
    ...state.debugRecordingDeviceStatus,
    enabled: true,
    active: false,
    sample_count: 120,
  });
  state.debugRecordingActive = false;
  assert.equal(getDebugRecordingStatusLabel(), "Niet actief");
  assert.match(renderDebugRecordingModal(), /De opname is momenteel niet actief/);

  applyDebugRecordingDeviceStatus({ available: false });
  assert.equal(getDebugRecordingStatusLabel(), "Niet beschikbaar");
});

test("kopieer-en-open gebruikt het bereik en opent daarna de analyser", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  const originalClipboard = window.navigator.clipboard;
  const originalOpen = window.open;
  t.after(() => {
    window.fetch = originalFetch;
    window.navigator.clipboard = originalClipboard;
    window.open = originalOpen;
    restoreTimers();
  });
  const calls = [];
  let copiedText = "";
  window.navigator.clipboard = { writeText: async (text) => { copiedText = text; } };
  window.isSecureContext = true;
  window.open = (url, target, features) => {
    calls.push({ kind: "open", url: String(url), target, features });
    return null;
  };
  window.fetch = async (url) => {
    calls.push({ kind: "fetch", url: String(url) });
    return { ok: true, status: 200, json: async () => ({ recording: { active: true, recording_id: 7 } }) };
  };

  const { copyDebugRecordingAndOpenAnalyser } = await import("../js/src/features/debug-recording.js");
  setDebugRecordingDownloadRange(60);
  await prepareDebugRecordingCopy();
  const copied = await copyDebugRecordingAndOpenAnalyser();

  assert.equal(copied, true);
  // Eerst kopiëren (document houdt focus), pas daarna openen: anders weigert
  // de browser de clipboard-write vanuit een niet-gefocust document.
  assert.match(calls[0].url, /download-range\?last_minutes=60/);
  assert.ok(copiedText.includes("recording"));
  assert.deepEqual(calls[1], {
    kind: "open",
    url: "https://openheatpumps.nl",
    target: "_blank",
    features: "noopener,noreferrer",
  });
});

test("kopieer-en-open opent geen tab bij een mislukte kopie", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  const originalClipboard = window.navigator.clipboard;
  const originalOpen = window.open;
  const originalDocument = globalThis.document;
  t.after(() => {
    window.fetch = originalFetch;
    window.navigator.clipboard = originalClipboard;
    window.open = originalOpen;
    globalThis.document = originalDocument;
    restoreTimers();
  });
  window.navigator.clipboard = { writeText: async () => { throw new Error("geen klembord"); } };
  window.isSecureContext = true;
  globalThis.document = undefined;
  let opened = 0;
  window.open = () => {
    opened += 1;
    return null;
  };
  window.fetch = async () => ({
    ok: true, status: 200, json: async () => ({ recording: { active: true, recording_id: 7 } }),
  });

  const { copyDebugRecordingAndOpenAnalyser } = await import("../js/src/features/debug-recording.js");
  await prepareDebugRecordingCopy();
  const copied = await copyDebugRecordingAndOpenAnalyser();

  assert.equal(copied, false);
  assert.equal(opened, 0);
  assert.match(state.debugRecordingError, /Kopiëren mislukt/);
});


test("grote export wordt vooraf opgehaald en kopieert met één klik zonder fetch of async onderbreking", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  const originalClipboard = window.navigator.clipboard;
  const originalOpen = window.open;
  t.after(() => {
    window.fetch = originalFetch;
    window.navigator.clipboard = originalClipboard;
    window.open = originalOpen;
    restoreTimers();
  });
  let fetches = 0;
  let copiedText = "";
  let opened = 0;
  const bundle = { exported_at_ms: 1793356171000, data: "x".repeat(742000) };
  window.fetch = async () => { fetches += 1; return { ok: true, json: async () => bundle }; };
  window.navigator.clipboard = { writeText: async (text) => { copiedText = text; } };
  window.open = () => { opened += 1; };
  const { copyDebugRecordingAndOpenAnalyser } = await import("../js/src/features/debug-recording.js");
  for (const range of [120, 360, 0]) {
    for (const copy of [copyDebugRecordingBundle, copyDebugRecordingAndOpenAnalyser]) {
      setDebugRecordingDownloadRange(range);
      assert.match(renderDebugRecordingModal(), /data-oq-action="copy-debug-recording" disabled/);
      const beforeFetch = fetches;
      const preparing = prepareDebugRecordingCopy();
      assert.match(renderDebugRecordingModal(), /Kopie voorbereiden/);
      await preparing;
      assert.match(renderDebugRecordingModal(), /Momentopname voor kopiëren/);
      assert.doesNotMatch(renderDebugRecordingModal(), /data-oq-action="copy-debug-recording" disabled/);
      copiedText = "";
      const copying = copy();
      assert.equal(copiedText, JSON.stringify(bundle), "clipboard wordt vóór de eerste await aangeroepen");
      assert.equal(await copying, true);
      assert.equal(fetches, beforeFetch + 1, "kopieerklik haalt niets meer op");
    }
  }
  assert.equal(opened, 3);
});

test("bereikwissel en sluiten annuleren voorbereiding; late responses kunnen geen verkeerde kopie activeren", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  const originalClipboard = window.navigator.clipboard;
  t.after(() => { window.fetch = originalFetch; window.navigator.clipboard = originalClipboard; restoreTimers(); });
  const requests = [];
  window.fetch = (url, options) => new Promise((resolve) => { requests.push({ url, options, resolve }); });
  let text = "";
  window.navigator.clipboard = { writeText: async (value) => { text = value; } };
  setDebugRecordingDownloadRange(120);
  const old = prepareDebugRecordingCopy();
  setDebugRecordingDownloadRange(360);
  const current = prepareDebugRecordingCopy();
  assert.equal(requests[0].options.signal.aborted, true);
  requests[1].resolve({ ok: true, json: async () => ({ window: 360 }) });
  assert.equal(await current, true);
  requests[0].resolve({ ok: true, json: async () => ({ window: 120 }) });
  assert.equal(await old, false);
  assert.equal(await copyDebugRecordingBundle(), true);
  assert.equal(text, '{"window":360}');
  applyDebugRecordingDeviceStatus({ ...state.debugRecordingDeviceStatus, recording_id: 8 });
  assert.match(renderDebugRecordingModal(), /data-oq-action="copy-debug-recording" disabled/);
  const closing = prepareDebugRecordingCopy();
  clearPreparedDebugRecordingCopy();
  state.systemModal = "";
  assert.equal(requests[2].options.signal.aborted, true);
  requests[2].resolve({ ok: true, json: async () => ({ window: "closed" }) });
  assert.equal(await closing, false);
});

test("mislukte voorbereiding houdt kopiëren uitgeschakeld en herhaalt niet bij iedere poll", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  t.after(() => { window.fetch = originalFetch; restoreTimers(); });
  let fetches = 0;
  window.fetch = async () => { fetches += 1; return { ok: false, status: 503 }; };
  setDebugRecordingDownloadRange(360);
  assert.equal(await prepareDebugRecordingCopy(), false);
  assert.equal(await prepareDebugRecordingCopy(), false);
  assert.equal(fetches, 1);
  assert.match(renderDebugRecordingModal(), /Kopie voorbereiden mislukt/);
  assert.match(renderDebugRecordingModal(), /data-oq-action="copy-debug-recording" disabled/);
  window.fetch = async () => { fetches += 1; return { ok: true, json: async () => ({ data: "retry" }) }; };
  assert.equal(await handleDebugRecordingAction("select-debug-recording-range", { dataset: { lastMinutes: "360" } }), true);
  assert.equal(fetches, 2);
});

test("voorbereiding afgerond na sluiten via geschiedenis blokkeert heropenen niet", async (t) => {
  const restoreTimers = seedFeatureStatus();
  const originalFetch = window.fetch;
  t.after(() => { window.fetch = originalFetch; restoreTimers(); });
  let resolve;
  window.fetch = () => new Promise((done) => { resolve = done; });
  setDebugRecordingDownloadRange(360);
  const preparing = prepareDebugRecordingCopy();
  state.systemModal = "";
  resolve({ ok: true, json: async () => ({ data: "old" }) });
  assert.equal(await preparing, false);
  state.systemModal = "debug-recording";
  window.fetch = async () => ({ ok: true, json: async () => ({ exported_at_ms: 1793356171000, data: "new" }) });
  assert.equal(await prepareDebugRecordingCopy(), true);
  const { formatDateTime } = await import("../js/src/i18n/index.js");
  assert.ok(renderDebugRecordingModal().includes(formatDateTime(1793356171000, { dateStyle: "short", timeStyle: "medium" })));
});
