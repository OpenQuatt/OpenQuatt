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
  state.debugRecordingError = "";
  state.debugRecordingNotice = "";
  state.debugRecordingDownloadRange = 15;
  state.systemModal = "debug-recording";
  const originalSetTimeout = window.setTimeout;
  const originalClearTimeout = window.clearTimeout;
  window.setTimeout = () => 0;
  window.clearTimeout = () => {};
  return () => {
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
  assert.match(markup, /Download diagnosebestand/);
  assert.match(markup, /Kopieer gegevens/);
  assert.match(markup, /Kopieer &amp; open analyser/);
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

test("kopieer-en-open gebruikt het bereik en opent de analyser-URL direct", async (t) => {
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
  const copied = await copyDebugRecordingAndOpenAnalyser();

  assert.equal(copied, true);
  // URL wordt synchroon uit de klik geopend (popup-veilig, geen handle nodig
  // dankzij noopener), pas daarna loopt de kopie.
  assert.deepEqual(calls[0], {
    kind: "open",
    url: "https://openheatpumps.nl",
    target: "_blank",
    features: "noopener,noreferrer",
  });
  assert.match(calls[1].url, /download-range\?last_minutes=60/);
  assert.ok(copiedText.includes("recording"));
});

test("kopieer-en-open zonder opname opent geen tab", async (t) => {
  const restoreTimers = seedFeatureStatus({ sample_count: 0 });
  const originalOpen = window.open;
  t.after(() => {
    window.open = originalOpen;
    restoreTimers();
  });
  let opened = 0;
  window.open = () => {
    opened += 1;
    return null;
  };

  const { copyDebugRecordingAndOpenAnalyser } = await import("../js/src/features/debug-recording.js");
  const copied = await copyDebugRecordingAndOpenAnalyser();

  assert.equal(copied, false);
  assert.equal(opened, 0);
  assert.match(state.debugRecordingError, /nog geen opname/);
});
