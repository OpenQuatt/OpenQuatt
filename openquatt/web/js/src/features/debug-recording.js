import { hasEntity, isEntityActive } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { copyTextToClipboard, downloadTextFile } from "../core/browser-utils.js";
import { DEBUG_RECORDING_BUSY_RETRY_MS, DEBUG_RECORDING_DURATION_OPTIONS, DEBUG_RECORDING_EVENT_LIMIT, DEBUG_RECORDING_KEYS, DEBUG_RECORDING_LOG_LIMIT, DEBUG_RECORDING_SAMPLE_INTERVAL_MS, ENTITY_DEFS, FAST_VIEW_ENTITY_REFRESH_CONCURRENCY } from "../core/config.js";
import { getEntityValue, parseLooseNumber } from "../core/entity-store.js";
import { buildBulkEntityChunks, refreshEntities } from "../core/entity-sync.js";
import { updateDebugRecordingState } from "../core/feature-state.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { getFirmwareDeviceLabel, getInstallationLabel, getInstallationTopology } from "./device-context.js";
import { getFirmwareCurrentVersion } from "./firmware-update.js";
import { escapeHtml } from "../core/html.js";
import { render } from "../core/render-scheduler.js";
import { renderModalShell } from "../core/modal-shell.js";

export function getDebugRecordingSampleCount() {
  if (state.debugRecordingDeviceStatus) {
    return Math.max(0, Number(state.debugRecordingDeviceStatus.sample_count || 0));
  }
  return Array.isArray(state.debugRecordingSamples) ? state.debugRecordingSamples.length : 0;
}

export function isDebugRecordingRolling(status = state.debugRecordingDeviceStatus) {
  return status?.rolling === true || String(status?.mode || "").toLowerCase() === "rolling";
}

export function isDebugRecordingFrozen(status = state.debugRecordingDeviceStatus) {
  return isDebugRecordingRolling(status) && status?.frozen === true && !status?.active;
}

export function formatDebugRecordingDuration(valueMs) {
  const totalSeconds = Math.max(0, Math.round(Number(valueMs || 0) / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  if (hours > 0) {
    return `${hours}u ${String(minutes).padStart(2, "0")}m`;
  }
  if (minutes > 0) {
    return `${minutes}m ${String(seconds).padStart(2, "0")}s`;
  }
  return `${seconds}s`;
}

export function getDebugRecordingRetainedDurationMs() {
  if (state.debugRecordingDeviceStatus) {
    return Math.max(0, Number(state.debugRecordingDeviceStatus.retained_duration_s || 0) * 1000);
  }
  return getDebugRecordingDurationMs();
}

export function getDebugRecordingDurationMs() {
  if (state.debugRecordingDeviceStatus) {
    return Math.max(0, Number(state.debugRecordingDeviceStatus.elapsed_s || 0) * 1000);
  }
  if (!state.debugRecordingStartedAt) {
    return 0;
  }
  const endAt = state.debugRecordingActive ? Date.now() : Number(state.debugRecordingLastSampleAt || Date.now());
  return Math.max(0, endAt - Number(state.debugRecordingStartedAt || endAt));
}

export function getDebugRecordingStatusLabel() {
  if (state.debugRecordingDeviceStatus && state.debugRecordingDeviceStatus.available === false) {
    return "Niet beschikbaar";
  }
  if (isDebugRecordingFrozen()) {
    return "Rolling gestopt";
  }
  if (state.debugRecordingActive && isDebugRecordingRolling()) {
    return "Rolling actief";
  }
  if (state.debugRecordingActive) {
    return "Bezig met opnemen";
  }
  const count = getDebugRecordingSampleCount();
  if (count > 0) {
    return "Voltooid";
  }
  return "Niet gestart";
}

export function getDebugRecordingStatusCopy() {
  if (isDebugRecordingFrozen()) {
    return "Rolling debug is gestopt. De recente samples blijven bewaard tot je downloadt, kopieert, hervat of een nieuwe opname start.";
  }
  if (state.debugRecordingActive && isDebugRecordingRolling()) {
    return "Rolling debug bewaart continu de recente samples. Download of kopieer maakt een momentopname; rolling blijft daarna doorlopen.";
  }
  if (state.debugRecordingActive) {
    return "De opname loopt in apparaatgeheugen. Je kunt deze pagina sluiten en later het bestand downloaden.";
  }
  if (getDebugRecordingSampleCount() > 0) {
    return "De opname is klaar. Download het supportbestand en voeg dit toe aan je supportverzoek.";
  }
  if (state.debugRecordingDeviceStatus && state.debugRecordingDeviceStatus.available === false) {
    return "Debugopname in apparaatgeheugen is niet beschikbaar op deze firmware.";
  }
  return "Neem tijdelijk supportgegevens op voor analyse. De opname wordt lokaal in het apparaatgeheugen opgeslagen. Er wordt niets automatisch verzonden.";
}

export function getDebugRecordingHubStatusLabel() {
  if (isDebugRecordingFrozen()) {
    return "Gestopt";
  }
  if (state.debugRecordingActive && isDebugRecordingRolling()) {
    return `Rolling · ${formatDebugRecordingDuration(getDebugRecordingRetainedDurationMs())}`;
  }
  if (state.debugRecordingActive) {
    return `Loopt · ${formatDebugRecordingDuration(getDebugRecordingRemainingMs())}`;
  }
  if (getDebugRecordingSampleCount() > 0) {
    return "Klaar";
  }
  return getDebugRecordingStatusLabel();
}

export function getDebugRecordingSelectedMinutes() {
  const selected = Number(state.debugRecordingSelectedMinutes || 15);
  const allowed = DEBUG_RECORDING_DURATION_OPTIONS.map((option) => Number(option.minutes));
  return allowed.includes(selected) ? selected : Number(DEBUG_RECORDING_DURATION_OPTIONS[0]?.minutes || 15);
}

export function setDebugRecordingSelectedMinutes(minutes) {
  if (state.debugRecordingActive) {
    return;
  }
  updateDebugRecordingState({
    debugRecordingSelectedMinutes: Math.max(1, Number(minutes) || 15),
    debugRecordingNotice: "",
    debugRecordingError: "",
  });
  render();
}

export function getDebugRecordingRemainingMs() {
  if (isDebugRecordingRolling()) {
    return 0;
  }
  if (state.debugRecordingDeviceStatus) {
    return Math.max(0, Number(state.debugRecordingDeviceStatus.remaining_s || 0) * 1000);
  }
  if (!state.debugRecordingActive) {
    return 0;
  }
  return Math.max(0, Number(state.debugRecordingEndsAt || 0) - Date.now());
}

export function getDebugRecordingProgressPercent() {
  if (state.debugRecordingDeviceStatus) {
    if (isDebugRecordingRolling()) {
      const sampleCapacity = Math.max(1, Number(state.debugRecordingDeviceStatus.sample_capacity || 0));
      return Math.max(0, Math.min(100, (getDebugRecordingSampleCount() / sampleCapacity) * 100));
    }
    const duration = Math.max(1, Number(state.debugRecordingDeviceStatus.duration_s || 0));
    const elapsed = Math.max(0, Number(state.debugRecordingDeviceStatus.elapsed_s || 0));
    if (!state.debugRecordingActive && getDebugRecordingSampleCount() > 0) {
      return 100;
    }
    return Math.max(0, Math.min(100, (elapsed / duration) * 100));
  }
  if (!state.debugRecordingActive || !state.debugRecordingStartedAt || !state.debugRecordingEndsAt) {
    return getDebugRecordingSampleCount() > 0 ? 100 : 0;
  }
  const totalMs = Math.max(1, Number(state.debugRecordingEndsAt) - Number(state.debugRecordingStartedAt));
  const elapsedMs = Math.max(0, Date.now() - Number(state.debugRecordingStartedAt));
  return Math.max(0, Math.min(100, (elapsedMs / totalMs) * 100));
}

export function getDebugRecordingId(source = state.debugRecordingDeviceStatus) {
  return String(source?.recording_id ?? source?.recording?.recording_id ?? "").trim();
}

export function getStoredDebugRecordingAcknowledgedId() {
  try {
    return String(window.localStorage.getItem("oq-debug-recording-acknowledged-id") || "");
  } catch (_error) {
    return "";
  }
}

export function acknowledgeDebugRecording(bundle) {
  if (bundle?.recording?.active) {
    return;
  }
  const recordingId = getDebugRecordingId(bundle);
  if (!recordingId) {
    return;
  }
  state.debugRecordingAcknowledgedId = recordingId;
  try {
    window.localStorage.setItem("oq-debug-recording-acknowledged-id", recordingId);
  } catch (_error) {
    // The acknowledgement still applies for the current browser session.
  }
}

export function renderDebugRecordingHeaderStatus() {
  const status = state.debugRecordingDeviceStatus;
  const sampleCount = Math.max(0, Number(status?.sample_count || 0));
  if (!status || status.available === false || (!status.active && sampleCount === 0)) {
    return "";
  }

  const active = Boolean(status.active);
  if (!active && getDebugRecordingId(status) === state.debugRecordingAcknowledgedId) {
    return "";
  }
  const rolling = isDebugRecordingRolling(status);
  const retained = formatDebugRecordingDuration(Math.max(0, Number(status.retained_duration_s || 0)) * 1000);
  const remaining = formatDebugRecordingDuration(Math.max(0, Number(status.remaining_s || 0)) * 1000);
  const label = active
    ? rolling ? `Rolling debug · ${retained}` : `Debug loopt · ${remaining}`
    : rolling ? "Rolling gestopt" : "Debug klaar";
  const title = active
    ? rolling ? `Rolling debug loopt, laatste ${retained} beschikbaar` : `Debugopname loopt, nog ${remaining}`
    : rolling ? "Rolling debug gestopt; recente buffer klaar om te downloaden" : "Debugopname klaar om te downloaden";
  return `
    <button
      class="oq-debug-recording-header-status${active ? " oq-debug-recording-header-status--active" : " oq-debug-recording-header-status--ready"}"
      type="button"
      data-oq-action="open-debug-recording-modal"
      aria-label="${escapeHtml(title)}"
      title="${escapeHtml(title)}"
    >
      <span class="oq-debug-recording-header-status-dot" aria-hidden="true"></span>
      <span>${escapeHtml(label)}</span>
    </button>
  `;
}

export function patchDebugRecordingHeaderStatus() {
  if (!state.root) {
    return;
  }
  if (state.interfacePanelOpen) {
    render();
    return;
  }
  const actions = state.root.querySelector(".oq-helper-hub--collapsed .oq-helper-hub-head-actions");
  if (!actions) {
    return;
  }
  const current = actions.querySelector(".oq-debug-recording-header-status");
  const markup = renderDebugRecordingHeaderStatus();
  if (!markup) {
    current?.remove();
    return;
  }
  if (current) {
    current.outerHTML = markup;
    return;
  }
  actions.insertAdjacentHTML("afterbegin", markup);
}

export function patchDebugRecordingSettingsStatus() {
  if (!state.root) {
    return;
  }
  const row = state.root.querySelector('[data-oq-diagnostics-row="debugRecording"]');
  if (!row) {
    return;
  }
  const value = row.querySelector(".oq-settings-system-row-value");
  const note = row.querySelector(".oq-settings-system-row-note");
  if (value) {
    value.textContent = getDebugRecordingStatusLabel();
  }
  if (note) {
    note.textContent = getDebugRecordingStatusCopy();
  }
}

export function renderDebugRecordingSvgIcon(name) {
  const icons = {
    activity: '<svg viewBox="0 0 24 24" focusable="false"><path d="M3 12h4l2-7 4 14 2-7h6"/></svg>',
    status: '<svg viewBox="0 0 24 24" focusable="false"><circle cx="12" cy="12" r="4"/></svg>',
    clock: '<svg viewBox="0 0 24 24" focusable="false"><circle cx="12" cy="12" r="8"/><path d="M12 7v5l3 2"/></svg>',
    samples: '<svg viewBox="0 0 24 24" focusable="false"><path d="M4 16h3l2-7 4 9 2-5h5"/></svg>',
    changes: '<svg viewBox="0 0 24 24" focusable="false"><path d="M18 8a7 7 0 1 0 1 7"/><path d="M18 4v4h-4"/></svg>',
    file: '<svg viewBox="0 0 24 24" focusable="false"><path d="M7 3h7l4 4v14H7z"/><path d="M14 3v5h5"/></svg>',
    storage: '<svg viewBox="0 0 24 24" focusable="false"><ellipse cx="12" cy="6" rx="7" ry="3"/><path d="M5 6v6c0 1.7 3.1 3 7 3s7-1.3 7-3V6"/><path d="M5 12v6c0 1.7 3.1 3 7 3s7-1.3 7-3v-6"/></svg>',
    play: '<svg viewBox="0 0 24 24" focusable="false"><path d="M8 5v14l11-7z"/></svg>',
    stop: '<svg viewBox="0 0 24 24" focusable="false"><path d="M7 7h10v10H7z"/></svg>',
    download: '<svg viewBox="0 0 24 24" focusable="false"><path d="M12 4v10"/><path d="m8 10 4 4 4-4"/><path d="M5 19h14"/></svg>',
    copy: '<svg viewBox="0 0 24 24" focusable="false"><rect x="8" y="8" width="10" height="10" rx="2"/><path d="M6 14H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h6a2 2 0 0 1 2 2v1"/></svg>',
    check: '<svg viewBox="0 0 24 24" focusable="false"><path d="m5 13 4 4L19 7"/></svg>',
    alert: '<svg viewBox="0 0 24 24" focusable="false"><path d="M12 8v5"/><path d="M12 17h.01"/><path d="M10.3 4.7 2.8 18a2 2 0 0 0 1.7 3h15a2 2 0 0 0 1.7-3L13.7 4.7a2 2 0 0 0-3.4 0z"/></svg>',
  };
  return icons[name] || icons.status;
}

export function renderDebugRecordingIcon(name) {
  return `<span class="oq-debug-recording-icon" aria-hidden="true">${renderDebugRecordingSvgIcon(name)}</span>`;
}

export function renderDebugRecordingButtonIcon(name) {
  return `<span class="oq-debug-recording-button-icon" aria-hidden="true">${renderDebugRecordingSvgIcon(name)}</span>`;
}

export function getDebugRecordingColumnSchema() {
  return [...DEBUG_RECORDING_KEYS];
}

export function getDebugRecordingUnits() {
  const units = [];
  DEBUG_RECORDING_KEYS.forEach((key, index) => {
    const payload = state.entities?.[key] || {};
    const uom = String(payload.uom ?? payload.unit_of_measurement ?? "").trim();
    if (uom) {
      units.push([index, uom]);
    }
  });
  return units;
}

export function encodeDebugRecordingInitialValues(values) {
  if (!Array.isArray(values)) {
    return [];
  }
  return values
    .map((value, index) => (value === null ? null : [index, value]))
    .filter(Boolean);
}

export function isDebugRecordingMissingValue(rawValue) {
  const value = String(rawValue ?? "").trim().toLowerCase();
  return !value || value === "unknown" || value === "unavailable" || value === "nan" || value === "invalid";
}

export function roundDebugRecordingNumber(value, unit) {
  const normalizedUnit = String(unit || "").trim().toLowerCase();
  let decimals = 3;
  if (normalizedUnit === "w" || normalizedUnit === "l/h" || normalizedUnit === "s") {
    decimals = 0;
  } else if (normalizedUnit === "hz" || normalizedUnit === "%") {
    decimals = 1;
  } else if (normalizedUnit === "°c" || normalizedUnit === "c" || normalizedUnit === "k") {
    decimals = 2;
  }
  return Number(Number(value).toFixed(decimals));
}

export function normalizeDebugRecordingValue(key) {
  const entity = ENTITY_DEFS[key];
  const payload = state.entities?.[key] || null;
  if (!entity || !payload) {
    return null;
  }

  if (entity.domain === "switch" || entity.domain === "binary_sensor") {
    return isEntityActive(key);
  }

  const rawValue = getEntityValue(key);
  const preserveUnknown = (key === "hp1Generation" || key === "hp2Generation")
    && String(rawValue ?? "").trim().toLowerCase() === "unknown";
  if (preserveUnknown) {
    return "Unknown";
  }
  if (isDebugRecordingMissingValue(rawValue)) {
    return null;
  }

  if (entity.domain === "sensor" || entity.domain === "number") {
    const numeric = parseLooseNumber(rawValue);
    if (Number.isFinite(numeric)) {
      const uom = String(payload.uom ?? payload.unit_of_measurement ?? "").trim();
      return roundDebugRecordingNumber(numeric, uom);
    }
  }

  return String(rawValue).trim();
}

export function areDebugRecordingValuesEqual(left, right) {
  return Object.is(left, right);
}

export function isDebugRecordingEventValue(key, value) {
  const domain = ENTITY_DEFS[key]?.domain || "";
  return domain === "switch" || domain === "binary_sensor" || domain === "select" || domain === "text_sensor" || typeof value === "string";
}

export function addDebugRecordingEvents(elapsedSeconds, changes, previousValues, nextValues) {
  const events = Array.isArray(state.debugRecordingEvents) ? [...state.debugRecordingEvents] : [];
  changes.forEach(([index]) => {
    const key = DEBUG_RECORDING_KEYS[index];
    const nextValue = nextValues[index];
    if (!isDebugRecordingEventValue(key, nextValue)) {
      return;
    }
    events.push([elapsedSeconds, index, previousValues[index] ?? null, nextValue ?? null]);
  });
  state.debugRecordingEvents = events.slice(-DEBUG_RECORDING_EVENT_LIMIT);
}

export function buildDebugRecordingSample() {
  const now = Date.now();
  const elapsedMs = state.debugRecordingStartedAt ? Math.max(0, now - Number(state.debugRecordingStartedAt)) : 0;
  const elapsedSeconds = Math.round(elapsedMs / 1000);
  const values = DEBUG_RECORDING_KEYS.map((key) => normalizeDebugRecordingValue(key));
  const hasInitial = Array.isArray(state.debugRecordingInitialValues);
  const previousValues = Array.isArray(state.debugRecordingLastValues)
    ? state.debugRecordingLastValues
    : values.map(() => null);
  const changes = [];

  values.forEach((value, index) => {
    if (!hasInitial) {
      return;
    }
    if (!areDebugRecordingValuesEqual(value, previousValues[index])) {
      changes.push([index, value]);
    }
  });

  if (!hasInitial) {
    state.debugRecordingInitialValues = values;
  } else if (changes.length > 0) {
    addDebugRecordingEvents(elapsedSeconds, changes, previousValues, values);
  }
  state.debugRecordingLastValues = values;

  return {
    seq: Number(state.debugRecordingSequence || 0) + 1,
    ts: now,
    sample: [elapsedSeconds, changes],
  };
}

export function clearDebugRecordingTimer() {
  if (state.debugRecordingTimer) {
    window.clearTimeout(state.debugRecordingTimer);
    state.debugRecordingTimer = null;
  }
}

export function clearDebugRecordingDevicePollTimer() {
  if (state.debugRecordingDevicePollTimer) {
    window.clearTimeout(state.debugRecordingDevicePollTimer);
    state.debugRecordingDevicePollTimer = null;
  }
}

export function getDebugRecordingEndpoint(path) {
  return `${getBasePath()}/openquatt/debug-recording/${path}`;
}

export function applyDebugRecordingDeviceStatus(payload) {
  const status = payload && typeof payload === "object" ? payload : {};
  state.debugRecordingDeviceStatus = status;
  state.debugRecordingActive = Boolean(status.active);
  state.debugRecordingStartedAt = status.active || Number(status.sample_count || 0) > 0
    ? Date.now() - Math.max(0, Number(status.elapsed_s || 0) * 1000)
    : 0;
  state.debugRecordingEndsAt = status.active
    ? Date.now() + Math.max(0, Number(status.remaining_s || 0) * 1000)
    : 0;
  state.debugRecordingLastSampleAt = Number(status.sample_count || 0) > 0 ? Date.now() : 0;
}

export function applyDebugRecordingDeviceUnavailableStatus() {
  applyDebugRecordingDeviceStatus({
    ok: false,
    available: false,
    active: false,
    mode: "manual",
    rolling: false,
    frozen: false,
    storage: "unavailable",
    interval_s: 0,
    duration_s: 0,
    elapsed_s: 0,
    remaining_s: 0,
    sample_count: 0,
    sample_capacity: 0,
    estimated_size: 0,
    buffer: "unavailable",
  });
}

export async function fetchDebugRecordingDeviceStatus() {
  const response = await window.fetch(getDebugRecordingEndpoint("status"), {
    cache: "no-store",
    headers: { "Cache-Control": "no-store" },
  });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  const payload = await response.json();
  applyDebugRecordingDeviceStatus(payload);
  return payload;
}

export function scheduleDebugRecordingDeviceStatusPoll(delayMs = 2000) {
  clearDebugRecordingDevicePollTimer();
  if (!state.debugRecordingActive) {
    return;
  }
  state.debugRecordingDevicePollTimer = window.setTimeout(() => {
    void refreshDebugRecordingDeviceStatus({ silent: true });
  }, Math.max(0, Number(state.systemModal === "debug-recording" ? delayMs : 5000) || 0));
}

export async function refreshDebugRecordingDeviceStatus(options = {}) {
  if (!options.silent) {
    state.debugRecordingBusy = true;
    state.debugRecordingError = "";
    render();
  }
  try {
    await fetchDebugRecordingDeviceStatus();
    if (!state.debugRecordingActive && options.silent) {
      state.debugRecordingNotice = "Debugopname is afgerond.";
    }
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    applyDebugRecordingDeviceUnavailableStatus();
    state.debugRecordingError = `Status kon niet worden opgehaald. ${error.message || String(error)}`;
  } finally {
    if (!options.silent) {
      state.debugRecordingBusy = false;
    }
    if (!options.silent || state.systemModal === "debug-recording") {
      render();
    } else {
      patchDebugRecordingHeaderStatus();
      patchDebugRecordingSettingsStatus();
    }
  }
}

export function scheduleDebugRecordingSample(delayMs = DEBUG_RECORDING_SAMPLE_INTERVAL_MS) {
  clearDebugRecordingTimer();
  if (!state.debugRecordingActive) {
    return;
  }
  state.debugRecordingTimer = window.setTimeout(() => {
    void captureDebugRecordingSample();
  }, Math.max(0, Number(delayMs) || 0));
}

export async function captureDebugRecordingSample() {
  if (!state.debugRecordingActive) {
    return;
  }
  if (state.debugRecordingBusy) {
    scheduleDebugRecordingSample(DEBUG_RECORDING_BUSY_RETRY_MS);
    return;
  }

  state.debugRecordingBusy = true;
  state.debugRecordingError = "";
  render();

  try {
    await refreshEntities(DEBUG_RECORDING_KEYS, "all", { concurrency: FAST_VIEW_ENTITY_REFRESH_CONCURRENCY });
    const sample = buildDebugRecordingSample();
    state.debugRecordingSequence = sample.seq;
    state.debugRecordingLastSampleAt = sample.ts;
    state.debugRecordingSamples = [...(state.debugRecordingSamples || []), sample.sample];
    if (Date.now() >= Number(state.debugRecordingEndsAt || 0)) {
      void stopDebugRecording({ completed: true });
      return;
    }
    scheduleDebugRecordingSample();
  } catch (error) {
    state.debugRecordingError = `Meting kon niet worden opgehaald. ${error.message || String(error)}`;
    scheduleDebugRecordingSample(DEBUG_RECORDING_SAMPLE_INTERVAL_MS);
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export async function configureDebugRecordingDevice() {
  const chunks = buildBulkEntityChunks(DEBUG_RECORDING_KEYS, "state");
  let status = null;
  for (let index = 0; index < chunks.length; index += 1) {
    const response = await window.fetch(
      getDebugRecordingEndpoint(`configure?reset=${index === 0 ? "1" : "0"}`),
      {
        method: "POST",
        cache: "no-store",
        headers: {
          "Cache-Control": "no-store",
          "Content-Type": "application/x-www-form-urlencoded",
        },
        body: chunks[index].body,
      }
    );
    if (!response.ok) {
      throw new Error(`configuratie HTTP ${response.status}`);
    }
    status = await response.json();
  }

  if (Number(status?.entity_field_count || 0) !== DEBUG_RECORDING_KEYS.length) {
    throw new Error(`onvolledige debugset (${Number(status?.entity_field_count || 0)}/${DEBUG_RECORDING_KEYS.length})`);
  }
  return status;
}

export async function startDebugRecording(durationMinutes) {
  const minutes = Math.max(1, Number(durationMinutes) || 15);
  clearDebugRecordingTimer();
  clearDebugRecordingDevicePollTimer();
  updateDebugRecordingState({
    debugRecordingBusy: true,
    debugRecordingError: "",
    debugRecordingNotice: "",
    debugRecordingSamples: [],
    debugRecordingEvents: [],
    debugRecordingInitialValues: null,
    debugRecordingLastValues: null,
    debugRecordingDeviceBundle: null,
    debugRecordingLastSampleAt: 0,
    debugRecordingSequence: 0,
  });
  render();
  try {
    await configureDebugRecordingDevice();
    const response = await window.fetch(getDebugRecordingEndpoint(`start?duration_s=${encodeURIComponent(minutes * 60)}`), {
      method: "POST",
      cache: "no-store",
      headers: { "Cache-Control": "no-store" },
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    const payload = await response.json();
    applyDebugRecordingDeviceStatus(payload);
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    applyDebugRecordingDeviceUnavailableStatus();
    state.debugRecordingError = `Debugopname kon niet worden gestart. ${error.message || String(error)}`;
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export async function startRollingDebugRecording() {
  clearDebugRecordingTimer();
  clearDebugRecordingDevicePollTimer();
  updateDebugRecordingState({
    debugRecordingBusy: true,
    debugRecordingError: "",
    debugRecordingNotice: "",
    debugRecordingSamples: [],
    debugRecordingEvents: [],
    debugRecordingInitialValues: null,
    debugRecordingLastValues: null,
    debugRecordingDeviceBundle: null,
    debugRecordingLastSampleAt: 0,
    debugRecordingSequence: 0,
  });
  render();
  try {
    await configureDebugRecordingDevice();
    const response = await window.fetch(getDebugRecordingEndpoint("start?rolling=1"), {
      method: "POST",
      cache: "no-store",
      headers: { "Cache-Control": "no-store" },
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    const payload = await response.json();
    applyDebugRecordingDeviceStatus(payload);
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    applyDebugRecordingDeviceUnavailableStatus();
    state.debugRecordingError = `Rolling debug kon niet worden gestart. ${error.message || String(error)}`;
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export async function requestDebugRecordingFreeze() {
  const response = await window.fetch(getDebugRecordingEndpoint("freeze"), {
    method: "POST",
    cache: "no-store",
    headers: { "Cache-Control": "no-store" },
  });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  const payload = await response.json();
  applyDebugRecordingDeviceStatus(payload);
  clearDebugRecordingDevicePollTimer();
  return payload;
}

export async function freezeDebugRecording() {
  clearDebugRecordingTimer();
  state.debugRecordingBusy = true;
  state.debugRecordingError = "";
  render();
  try {
    await requestDebugRecordingFreeze();
    state.debugRecordingNotice = "Rolling debug is gestopt. De recente buffer blijft bewaard.";
  } catch (error) {
    state.debugRecordingError = `Rolling debug kon niet worden gestopt. ${error.message || String(error)}`;
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export async function stopDebugRecording(options = {}) {
  clearDebugRecordingTimer();
  clearDebugRecordingDevicePollTimer();
  state.debugRecordingBusy = true;
  state.debugRecordingError = "";
  render();
  try {
    const response = await window.fetch(getDebugRecordingEndpoint("stop"), {
      method: "POST",
      cache: "no-store",
      headers: { "Cache-Control": "no-store" },
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    const payload = await response.json();
    applyDebugRecordingDeviceStatus(payload);
    state.debugRecordingNotice = options.completed ? "Debugopname is afgerond." : "Debugopname is gestopt.";
  } catch (error) {
    state.debugRecordingError = `Debugopname kon niet worden gestopt. ${error.message || String(error)}`;
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export function buildDebugRecordingCorePayload(extra = {}) {
  const endedAt = state.debugRecordingActive ? Date.now() : Number(state.debugRecordingLastSampleAt || Date.now());
  return {
    format: "openquatt-debug-v2",
    schema_version: 2,
    kind: "openquatt_debug_recording",
    encoding: "column-delta-json-v2",
    exported_at: new Date().toISOString(),
    source: extra.source || {},
    recording: {
      started_at: state.debugRecordingStartedAt ? new Date(Number(state.debugRecordingStartedAt)).toISOString() : "",
      ended_at: endedAt ? new Date(endedAt).toISOString() : "",
      active: Boolean(state.debugRecordingActive),
      duration_s: state.debugRecordingStartedAt ? Math.round(Math.max(0, endedAt - Number(state.debugRecordingStartedAt)) / 1000) : 0,
      interval_s: Math.round(DEBUG_RECORDING_SAMPLE_INTERVAL_MS / 1000),
      sample_count: getDebugRecordingSampleCount(),
      column_count: DEBUG_RECORDING_KEYS.length,
      storage: "browser",
    },
    columns: getDebugRecordingColumnSchema(),
    units: getDebugRecordingUnits(),
    initial: encodeDebugRecordingInitialValues(state.debugRecordingInitialValues),
    samples: state.debugRecordingSamples || [],
    events: state.debugRecordingEvents || [],
    ...(extra.logs ? { logs: extra.logs } : {}),
  };
}

export function getDebugRecordingCompactJson(payload) {
  return JSON.stringify(payload);
}

export function getDebugRecordingEstimatedBytes() {
  const estimated = Number(state.debugRecordingDeviceStatus?.estimated_size || 0);
  if (estimated > 0) {
    return estimated;
  }
  try {
    return new Blob([getDebugRecordingCompactJson(buildDebugRecordingCorePayload())]).size;
  } catch (_error) {
    return getDebugRecordingCompactJson(buildDebugRecordingCorePayload()).length;
  }
}

export function formatDebugRecordingBytes(bytes) {
  const value = Math.max(0, Number(bytes) || 0);
  if (value >= 1024 * 1024) {
    return `${(value / 1024 / 1024).toFixed(1)} MB`;
  }
  if (value >= 1024) {
    return `${(value / 1024).toFixed(1)} kB`;
  }
  return `${Math.round(value)} B`;
}

export function getDebugRecordingFilename(bundle) {
  const exportedAt = bundle?.exported_at || (bundle?.exported_at_ms ? new Date(Number(bundle.exported_at_ms)).toISOString() : new Date().toISOString());
  const stamp = String(exportedAt)
    .replace(/[:.]/g, "-")
    .replace(/T/, "_")
    .replace(/Z$/, "Z");
  const installation = String(bundle?.source?.installation || "OpenQuatt").replace(/\s+/g, "-").toLowerCase();
  return `${installation}-debug-recording-${stamp}.oqdebug.json`;
}

export async function downloadDebugRecordingBundle() {
  if (getDebugRecordingSampleCount() === 0) {
    state.debugRecordingError = "Er is nog geen debugopname om te downloaden.";
    render();
    return;
  }
  state.debugRecordingBusy = true;
  state.debugRecordingError = "";
  const rollingActive = state.debugRecordingActive && isDebugRecordingRolling();
  render();
  try {
    const response = await window.fetch(getDebugRecordingEndpoint("download"), {
      cache: "no-store",
      headers: { "Cache-Control": "no-store" },
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    const bundle = await response.json();
    state.debugRecordingDeviceBundle = bundle;
    downloadTextFile(getDebugRecordingFilename(bundle), getDebugRecordingCompactJson(bundle), "application/json");
    acknowledgeDebugRecording(bundle);
    state.debugRecordingNotice = rollingActive ? "Momentopname gedownload. Rolling debug loopt door." : "Supportbestand gedownload.";
  } catch (error) {
    state.debugRecordingError = "Download mislukt. Probeer opnieuw of kopieer de data.";
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export async function copyDebugRecordingBundle() {
  if (getDebugRecordingSampleCount() === 0) {
    state.debugRecordingError = "Er is nog geen debugopname om te kopiëren.";
    render();
    return;
  }
  state.debugRecordingBusy = true;
  state.debugRecordingError = "";
  const rollingActive = state.debugRecordingActive && isDebugRecordingRolling();
  render();
  try {
    const response = await window.fetch(getDebugRecordingEndpoint("download"), {
      cache: "no-store",
      headers: { "Cache-Control": "no-store" },
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    const bundle = await response.json();
    state.debugRecordingDeviceBundle = bundle;
    const copied = await copyTextToClipboard(getDebugRecordingCompactJson(bundle));
    if (!copied) {
      throw new Error("Kopiëren naar het klembord is niet gelukt.");
    }
    acknowledgeDebugRecording(bundle);
    state.debugRecordingNotice = rollingActive ? "Momentopname gekopieerd. Rolling debug loopt door." : "Supportbestand gekopieerd.";
  } catch (error) {
    state.debugRecordingError = "Kopiëren mislukt. Probeer opnieuw of download het supportbestand.";
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

const debugRecordingActionHandlers = {
  "open-debug-recording-modal": () => {
    state.systemModal = "debug-recording";
    state.debugRecordingError = "";
    state.debugRecordingNotice = "";
    render();
    return refreshDebugRecordingDeviceStatus();
  },
  "start-debug-recording": (button) => startDebugRecording(button.dataset.debugMinutes || 15),
  "start-rolling-debug-recording": () => startRollingDebugRecording(),
  "select-debug-recording-duration": (button) => setDebugRecordingSelectedMinutes(button.dataset.debugMinutes || 15),
  "stop-debug-recording": () => stopDebugRecording(),
  "freeze-debug-recording": () => freezeDebugRecording(),
  "download-debug-recording": () => downloadDebugRecordingBundle(),
  "copy-debug-recording": () => copyDebugRecordingBundle(),
};

export function handleDebugRecordingAction(action, button) {
  return invokeActionMap(debugRecordingActionHandlers, action, button);
}

export function renderDebugRecordingModal() {
  const active = state.debugRecordingActive;
  const rolling = isDebugRecordingRolling();
  const frozen = isDebugRecordingFrozen();
  const sampleCount = getDebugRecordingSampleCount();
  const busy = state.debugRecordingBusy;
  const estimatedSize = formatDebugRecordingBytes(getDebugRecordingEstimatedBytes());
  const eventCount = Array.isArray(state.debugRecordingDeviceBundle?.events)
    ? state.debugRecordingDeviceBundle.events.length
    : Array.isArray(state.debugRecordingEvents) ? state.debugRecordingEvents.length : 0;
  const selectedMinutes = getDebugRecordingSelectedMinutes();
  const remainingMs = getDebugRecordingRemainingMs();
  const retainedMs = getDebugRecordingRetainedDurationMs();
  const progressPercent = getDebugRecordingProgressPercent();
  const hasRecording = sampleCount > 0;
  const progressWidth = `${progressPercent.toFixed(1)}%`;
  const stats = [
    { icon: "status", label: "Status", value: getDebugRecordingStatusLabel() },
    { icon: "clock", label: rolling ? "Retentie" : "Duur", value: formatDebugRecordingDuration(rolling ? retainedMs : getDebugRecordingDurationMs()) },
    { icon: "samples", label: "Samples", value: String(sampleCount) },
    { icon: "changes", label: "Statuswijzigingen", value: String(eventCount) },
    { icon: "file", label: "Geschatte grootte", value: `± ${estimatedSize}` },
    { icon: "storage", label: "Opslag", value: state.debugRecordingDeviceStatus?.available === false ? "Niet beschikbaar" : "Apparaatgeheugen" },
  ];
  const feedback = state.debugRecordingError
    ? { kind: "error", icon: "alert", text: state.debugRecordingError }
    : state.debugRecordingNotice
      ? { kind: "success", icon: "check", text: state.debugRecordingNotice }
      : null;
  return renderModalShell({
    id: "system",
    titleId: "oq-debug-recording-modal-title",
    kicker: "Diagnostiek",
    title: "Debugopname",
    copy: getDebugRecordingStatusCopy(),
    className: "oq-debug-recording-modal",
    closeAction: "close-system-modal",
    closeLabel: "Sluit debugopname",
    body: `
        <section class="oq-debug-recording-card" aria-label="Opname">
          <div class="oq-debug-recording-card-head">
            <span class="oq-debug-recording-heading-icon" aria-hidden="true">${renderDebugRecordingSvgIcon("activity")}</span>
            <h3>Opname</h3>
          </div>
          ${active ? `
            <div class="oq-debug-recording-progress">
              <div class="oq-debug-recording-progress-head">
                <span>${escapeHtml(rolling ? `Laatste ${formatDebugRecordingDuration(retainedMs)} bewaard` : `Nog ${formatDebugRecordingDuration(remainingMs)}`)}</span>
                <strong>${escapeHtml(rolling ? `${sampleCount}/${Number(state.debugRecordingDeviceStatus?.sample_capacity || 0)}` : `${Math.round(progressPercent)}%`)}</strong>
              </div>
              <div class="oq-debug-recording-progress-track" aria-hidden="true">
                <span class="oq-debug-recording-progress-fill" style="width: ${escapeHtml(progressWidth)}"></span>
              </div>
            </div>
          ` : ""}
          <dl class="oq-debug-recording-stats">
            ${stats.map((item) => `
              <div class="oq-debug-recording-stat">
                <dt>${renderDebugRecordingIcon(item.icon)}${escapeHtml(item.label)}</dt>
                <dd>${escapeHtml(item.value)}</dd>
              </div>
            `).join("")}
          </dl>
        </section>
        ${active && rolling ? `
          <section class="oq-debug-recording-duration" aria-label="Rolling debug">
            <h3>Rolling debug</h3>
            <p class="oq-helper-modal-copy">Download of kopieer maakt een momentopname van de huidige buffer. Stop rolling zet de buffer vast.</p>
          </section>
        ` : `
          <section class="oq-debug-recording-duration" aria-label="Duur">
            <h3>Duur</h3>
            <div class="oq-debug-recording-segments" role="group" aria-label="Kies opnameduur">
              ${DEBUG_RECORDING_DURATION_OPTIONS.map((option) => {
                const selected = Number(option.minutes) === selectedMinutes;
                return `
                  <button
                    class="oq-debug-recording-segment${selected ? " oq-debug-recording-segment--selected" : ""}"
                    type="button"
                    data-oq-action="select-debug-recording-duration"
                    data-debug-minutes="${option.minutes}"
                    aria-pressed="${selected ? "true" : "false"}"
                    ${active || busy ? "disabled" : ""}
                  >
                    ${escapeHtml(option.label)}
                  </button>
                `;
              }).join("")}
            </div>
          </section>
        `}
        <div class="oq-debug-recording-actions">
          ${active && rolling ? `
            <button class="oq-helper-button oq-helper-button--warning oq-debug-recording-primary" type="button" data-oq-action="freeze-debug-recording" ${busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("stop")}Stop rolling</button>
          ` : active ? `
            <button class="oq-helper-button oq-helper-button--warning oq-debug-recording-primary" type="button" data-oq-action="stop-debug-recording" ${busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("stop")}Stop opname</button>
          ` : frozen ? `
            <button class="oq-helper-button oq-helper-button--primary oq-debug-recording-primary" type="button" data-oq-action="start-debug-recording" data-debug-minutes="${selectedMinutes}" ${busy || state.debugRecordingDeviceStatus?.available === false ? "disabled" : ""}>${renderDebugRecordingButtonIcon("play")}Start opname</button>
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="start-rolling-debug-recording" ${busy || state.debugRecordingDeviceStatus?.available === false ? "disabled" : ""}>${renderDebugRecordingButtonIcon("activity")}Hervat rolling</button>
          ` : `
            <button class="oq-helper-button oq-helper-button--primary oq-debug-recording-primary" type="button" data-oq-action="start-debug-recording" data-debug-minutes="${selectedMinutes}" ${busy || state.debugRecordingDeviceStatus?.available === false ? "disabled" : ""}>${renderDebugRecordingButtonIcon("play")}Start opname</button>
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="start-rolling-debug-recording" ${busy || state.debugRecordingDeviceStatus?.available === false ? "disabled" : ""}>${renderDebugRecordingButtonIcon("activity")}Start rolling</button>
          `}
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="download-debug-recording" ${!hasRecording || busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("download")}${active && rolling ? "Download tot nu toe" : "Download supportbestand"}</button>
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="copy-debug-recording" ${!hasRecording || busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("copy")}${active && rolling ? "Kopieer tot nu toe" : "Kopieer data"}</button>
          ${feedback ? `
            <p class="oq-debug-recording-feedback oq-debug-recording-feedback--${feedback.kind}" role="status">
              ${renderDebugRecordingButtonIcon(feedback.icon)}
              <span>${escapeHtml(feedback.text)}</span>
            </p>
          ` : ""}
        </div>`,
  });
}
