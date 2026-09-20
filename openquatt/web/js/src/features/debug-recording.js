import { invokeActionMap } from "../core/action-router.js";
import { copyTextToClipboard, downloadTextFile } from "../core/browser-utils.js";
import {
  DEBUG_RECORDING_DOWNLOAD_RANGE_OPTIONS,
  DEBUG_RECORDING_DURATION_OPTIONS,
  DEBUG_RECORDING_KEYS,
  SYSTEM_RECORDER_ANALYSER_URL,
} from "../core/config.js";
import { buildBulkEntityChunks } from "../core/entity-sync.js";
import { updateDebugRecordingState } from "../core/feature-state.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { escapeHtml } from "../core/html.js";
import { render } from "../core/render-scheduler.js";
import { renderModalShell } from "../core/modal-shell.js";

let debugRecordingMutationGeneration = 0;
let debugRecordingStatusFailureCount = 0;

export function getDebugRecordingSampleCount() {
  return Math.max(0, Number(state.debugRecordingDeviceStatus?.sample_count || 0));
}

export function isDebugRecordingRolling(status = state.debugRecordingDeviceStatus) {
  return status?.rolling === true || String(status?.mode || "").toLowerCase() === "rolling";
}

// The recorder is enabled by default (firmware auto-starts on boot). Only an
// explicit user opt-out disables it, so an unknown status is treated as
// enabled until the device reports otherwise.
export function isSystemRecorderEnabled(status = state.debugRecordingDeviceStatus) {
  if (!status) {
    return true;
  }
  return status.enabled !== false;
}

export function isSystemRecorderAvailable(status = state.debugRecordingDeviceStatus) {
  if (!status) {
    return true;
  }
  return status.available !== false;
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
  return Math.max(0, Number(state.debugRecordingDeviceStatus?.elapsed_s || 0) * 1000);
}

export function getDebugRecordingStatusLabel() {
  if (!state.debugRecordingDeviceStatus) {
    return "Starten…";
  }
  if (state.debugRecordingDeviceStatus.available === false) {
    return "Niet beschikbaar";
  }
  if (!isSystemRecorderEnabled()) {
    return "Uitgeschakeld";
  }
  if (!state.debugRecordingActive) {
    return "Niet actief";
  }
  if (isDebugRecordingRolling()) {
    return "Actief";
  }
  return "Bezig met opnemen";
}

export function getDebugRecordingStatusCopy() {
  if (state.debugRecordingDeviceStatus && state.debugRecordingDeviceStatus.available === false) {
    return "Systeemrecorder in apparaatgeheugen is niet beschikbaar op deze firmware.";
  }
  if (!isSystemRecorderEnabled()) {
    return "Doorlopende opname uitgeschakeld — Er worden momenteel geen nieuwe systeemgegevens opgeslagen. De huidige opname blijft beschikbaar totdat een nieuwe opname wordt gestart of het apparaat opnieuw opstart.";
  }
  return "De Systeemrecorder bewaart continu recente systeemgegevens voor diagnose en analyse. De gegevens blijven lokaal op het apparaat en worden niet automatisch verzonden.";
}

export function getDebugRecordingHubStatusLabel() {
  if (state.debugRecordingDeviceStatus && state.debugRecordingDeviceStatus.available === false) {
    return "Niet beschikbaar";
  }
  if (!isSystemRecorderEnabled()) {
    return "Uitgeschakeld";
  }
  if (state.debugRecordingActive) {
    return "Actief";
  }
  if (!state.debugRecordingDeviceStatus) {
    return "Starten…";
  }
  return "Niet actief";
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

export function getDebugRecordingDownloadRange() {
  const selected = Number(state.debugRecordingDownloadRange ?? 15);
  const allowed = DEBUG_RECORDING_DOWNLOAD_RANGE_OPTIONS.map((option) => Number(option.minutes));
  return allowed.includes(selected) ? selected : 15;
}

export function setDebugRecordingDownloadRange(minutes) {
  updateDebugRecordingState({
    debugRecordingDownloadRange: Math.max(0, Number(minutes) || 0),
    debugRecordingNotice: "",
    debugRecordingError: "",
  });
  render();
}

export function getDebugRecordingRemainingMs() {
  if (isDebugRecordingRolling()) {
    return 0;
  }
  return Math.max(0, Number(state.debugRecordingDeviceStatus?.remaining_s || 0) * 1000);
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
  return getDebugRecordingSampleCount() > 0 ? 100 : 0;
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

// An active recorder is the normal situation, so no permanent header badge.
// Only a deviating state (disabled / unavailable) is surfaced.
export function renderDebugRecordingHeaderStatus() {
  const status = state.debugRecordingDeviceStatus;
  if (!status) {
    return "";
  }
  if (status.available === false) {
    const title = "Systeemrecorder niet beschikbaar op deze firmware";
    return `
    <button
      class="oq-debug-recording-header-status oq-debug-recording-header-status--ready"
      type="button"
      data-oq-action="open-debug-recording-modal"
      aria-label="${escapeHtml(title)}"
      title="${escapeHtml(title)}"
    >
      <span>Systeemrecorder niet beschikbaar</span>
    </button>
  `;
  }
  if (status.enabled === false) {
    const title = "Systeemrecorder uitgeschakeld — er worden geen nieuwe systeemgegevens opgeslagen";
    return `
    <button
      class="oq-debug-recording-header-status oq-debug-recording-header-status--ready"
      type="button"
      data-oq-action="open-debug-recording-modal"
      aria-label="${escapeHtml(title)}"
      title="${escapeHtml(title)}"
    >
      <span>Systeemrecorder uitgeschakeld</span>
    </button>
  `;
  }
  if (status.active === false) {
    const title = "Systeemrecorder niet actief — er wordt momenteel niets opgenomen";
    return `
    <button
      class="oq-debug-recording-header-status oq-debug-recording-header-status--ready"
      type="button"
      data-oq-action="open-debug-recording-modal"
      aria-label="${escapeHtml(title)}"
      title="${escapeHtml(title)}"
    >
      <span>Systeemrecorder niet actief</span>
    </button>
  `;
  }
  return "";
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

// Silent status polls must not re-render the open modal: a full render
// replaces every DOM node, which visibly flickers hovered buttons and steals
// focus. Only the running numbers are patched in place; structural changes
// (enabled/active/available/overflow) still trigger a full render.
export function getDebugRecordingChromeSignature(status = state.debugRecordingDeviceStatus) {
  return [
    status?.available !== false,
    status?.enabled !== false,
    Boolean(status?.active),
    isDebugRecordingRolling(status),
    status?.string_overflow === true,
  ].join("|");
}

export function patchDebugRecordingModalNumbers() {
  if (!state.root || state.systemModal !== "debug-recording") {
    return false;
  }
  const modal = state.root.querySelector(".oq-debug-recording-modal");
  if (!modal) {
    return false;
  }
  const availability = modal.querySelector("[data-oq-recorder-availability]");
  if (availability) {
    const retained = availability.querySelector("[data-oq-recorder-retained]");
    const samples = availability.querySelector("[data-oq-recorder-samples]");
    if (retained) {
      retained.textContent = formatDebugRecordingDuration(getDebugRecordingRetainedDurationMs());
    }
    if (samples) {
      samples.textContent = `(${getDebugRecordingSampleCount()} samples)`;
    }
    if (!retained && !samples) {
      availability.textContent = `${formatDebugRecordingDuration(getDebugRecordingRetainedDurationMs())} (${getDebugRecordingSampleCount()} samples)`;
    }
  }
  const range = modal.querySelector("[data-oq-recorder-range]");
  if (range) {
    range.textContent = getDebugRecordingRangeLabel();
  }
  return true;
}

const DEBUG_RECORDING_ICONS = {
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
  external: '<svg viewBox="0 0 24 24" focusable="false"><path d="M14 4h6v6"/><path d="M20 4 10 14"/><path d="M20 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1h5"/></svg>',
  check: '<svg viewBox="0 0 24 24" focusable="false"><path d="m5 13 4 4L19 7"/></svg>',
  alert: '<svg viewBox="0 0 24 24" focusable="false"><path d="M12 8v5"/><path d="M12 17h.01"/><path d="M10.3 4.7 2.8 18a2 2 0 0 0 1.7 3h15a2 2 0 0 0 1.7-3L13.7 4.7a2 2 0 0 0-3.4 0z"/></svg>',
};

export function renderDebugRecordingSvgIcon(name) {
  return DEBUG_RECORDING_ICONS[name] || DEBUG_RECORDING_ICONS.status;
}

export function renderDebugRecordingButtonIcon(name) {
  return `<span class="oq-debug-recording-button-icon" aria-hidden="true">${renderDebugRecordingSvgIcon(name)}</span>`;
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
}

export function applyDebugRecordingDeviceUnavailableStatus() {
  applyDebugRecordingDeviceStatus({
    ok: false,
    available: false,
    enabled: false,
    active: false,
    mode: "manual",
    rolling: false,
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
  const requestGeneration = debugRecordingMutationGeneration;
  const response = await window.fetch(getDebugRecordingEndpoint("status"), {
    cache: "no-store",
    headers: { "Cache-Control": "no-store" },
  });
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  const payload = await response.json();
  if (requestGeneration === debugRecordingMutationGeneration) {
    applyDebugRecordingDeviceStatus(payload);
  }
  return payload;
}

export function scheduleDebugRecordingDeviceStatusPoll(delayMs = 2000) {
  clearDebugRecordingDevicePollTimer();
  if (!state.debugRecordingActive && state.systemModal !== "debug-recording") {
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
  const chromeBefore = getDebugRecordingChromeSignature();
  let modalPatched = false;
  try {
    await fetchDebugRecordingDeviceStatus();
    debugRecordingStatusFailureCount = 0;
    if (String(state.debugRecordingError || "").startsWith("Status kon niet worden opgehaald.")) {
      state.debugRecordingError = "";
    }
    if (options.silent && state.systemModal === "debug-recording"
      && getDebugRecordingChromeSignature() === chromeBefore) {
      modalPatched = patchDebugRecordingModalNumbers();
    }
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    debugRecordingStatusFailureCount += 1;
    if (!state.debugRecordingDeviceStatus) {
      applyDebugRecordingDeviceUnavailableStatus();
    }
    state.debugRecordingError = `Status kon niet worden opgehaald. ${error.message || String(error)}`;
    if (state.debugRecordingActive || state.systemModal === "debug-recording") {
      scheduleDebugRecordingDeviceStatusPoll(Math.min(30000, 2000 * (2 ** debugRecordingStatusFailureCount)));
    }
  } finally {
    if (!options.silent) {
      state.debugRecordingBusy = false;
    }
    if (!options.silent || (state.systemModal === "debug-recording" && !modalPatched)) {
      render();
    } else if (state.systemModal !== "debug-recording") {
      patchDebugRecordingHeaderStatus();
      patchDebugRecordingSettingsStatus();
    }
  }
}

export function getDebugRecordingCsrfToken() {
  return String(state.debugRecordingDeviceStatus?.csrf_token || "");
}

export async function ensureDebugRecordingCsrfToken() {
  const current = getDebugRecordingCsrfToken();
  if (current) {
    return current;
  }
  const status = await fetchDebugRecordingDeviceStatus();
  const csrfToken = String(status?.csrf_token || "");
  if (!csrfToken) {
    throw new Error("beveiligingstoken ontbreekt");
  }
  return csrfToken;
}

export async function postDebugRecordingDevice(path, parameters = {}) {
  let csrfToken = await ensureDebugRecordingCsrfToken();
  let response = null;
  for (let attempt = 0; attempt < 2; attempt += 1) {
    const body = new URLSearchParams(parameters);
    body.set("csrf_token", csrfToken);
    response = await window.fetch(getDebugRecordingEndpoint(path), {
      method: "POST",
      cache: "no-store",
      headers: {
        "Cache-Control": "no-store",
        "Content-Type": "application/x-www-form-urlencoded",
      },
      body: body.toString(),
    });
    if (response.status !== 403 || attempt > 0) break;
    const status = await fetchDebugRecordingDeviceStatus();
    csrfToken = String(status?.csrf_token || "");
    if (!csrfToken) break;
  }
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return response.json();
}

async function reconcileDebugRecordingMutation(predicate) {
  try {
    const status = await fetchDebugRecordingDeviceStatus();
    if (!predicate(status)) {
      scheduleDebugRecordingDeviceStatusPoll(4000);
      return false;
    }
    debugRecordingStatusFailureCount = 0;
    scheduleDebugRecordingDeviceStatusPoll();
    return true;
  } catch (_error) {
    if (state.debugRecordingActive) scheduleDebugRecordingDeviceStatusPoll(4000);
    return false;
  }
}

export async function configureDebugRecordingDevice() {
  const chunks = buildBulkEntityChunks(DEBUG_RECORDING_KEYS, "state");
  let status = null;
  for (let index = 0; index < chunks.length; index += 1) {
    status = await postDebugRecordingDevice(
      `configure?reset=${index === 0 ? "1" : "0"}`,
      new URLSearchParams(chunks[index].body),
    );
  }

  if (!status?.configuration_pending || Number(status?.pending_requested_field_count || 0) !== DEBUG_RECORDING_KEYS.length) {
    throw new Error(
      `onvolledige debugset (${Number(status?.pending_requested_field_count || 0)}/${DEBUG_RECORDING_KEYS.length})`,
    );
  }
  return status;
}

export async function startDebugRecordingMode({ rolling = false, durationMinutes = 15 } = {}) {
  const minutes = Math.max(1, Number(durationMinutes) || 15);
  const previousRecordingId = getDebugRecordingId();
  debugRecordingMutationGeneration += 1;
  clearDebugRecordingDevicePollTimer();
  updateDebugRecordingState({
    debugRecordingBusy: true,
    debugRecordingError: "",
    debugRecordingNotice: "",
    debugRecordingDeviceBundle: null,
  });
  render();
  try {
    await configureDebugRecordingDevice();
    const path = rolling ? "start?rolling=1" : `start?duration_s=${encodeURIComponent(minutes * 60)}`;
    const payload = await postDebugRecordingDevice(path);
    applyDebugRecordingDeviceStatus(payload);
    debugRecordingStatusFailureCount = 0;
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    const reconciled = await reconcileDebugRecordingMutation((status) => (
      Boolean(status?.active)
      && isDebugRecordingRolling(status) === rolling
      && getDebugRecordingId(status) !== previousRecordingId
    ));
    if (reconciled) {
      state.debugRecordingNotice = `${rolling ? "Doorlopende opname" : "Opname"} is gestart; alleen de bevestiging was vertraagd.`;
    } else {
      state.debugRecordingError = `${rolling ? "Doorlopende opname" : "Opname"} kon niet worden gestart. ${error.message || String(error)}`;
    }
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export function startDebugRecording(durationMinutes) {
  return startDebugRecordingMode({ durationMinutes });
}

export function startRollingDebugRecording() {
  return startDebugRecordingMode({ rolling: true });
}

export async function stopDebugRecording(options = {}) {
  debugRecordingMutationGeneration += 1;
  clearDebugRecordingDevicePollTimer();
  state.debugRecordingBusy = true;
  state.debugRecordingError = "";
  render();
  try {
    const payload = await postDebugRecordingDevice("stop");
    applyDebugRecordingDeviceStatus(payload);
    state.debugRecordingNotice = options.completed ? "Opname is afgerond." : "Opname is gestopt.";
  } catch (error) {
    const reconciled = await reconcileDebugRecordingMutation((status) => !status?.active);
    if (reconciled) {
      state.debugRecordingNotice = "Opname is gestopt; alleen de bevestiging was vertraagd.";
    } else {
      state.debugRecordingError = `Opname kon niet worden gestopt. ${error.message || String(error)}`;
    }
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export async function setSystemRecorderEnabled(enabled) {
  debugRecordingMutationGeneration += 1;
  clearDebugRecordingDevicePollTimer();
  state.debugRecordingBusy = true;
  state.debugRecordingError = "";
  state.debugRecordingNotice = "";
  state.debugRecordingConfirmDisable = false;
  render();
  try {
    const payload = await postDebugRecordingDevice("enabled", { enabled: enabled ? "1" : "0" });
    applyDebugRecordingDeviceStatus(payload);
    debugRecordingStatusFailureCount = 0;
    state.debugRecordingNotice = enabled
      ? "Doorlopende opname ingeschakeld. Er is een nieuwe opname gestart; de oude buffer is gewist."
      : "Doorlopende opname uitgeschakeld. De huidige opname blijft beschikbaar totdat een nieuwe opname wordt gestart of het apparaat opnieuw opstart.";
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    const reconciled = await reconcileDebugRecordingMutation(
      (status) => (status?.enabled !== false) === enabled,
    );
    if (reconciled) {
      state.debugRecordingNotice = enabled
        ? "Doorlopende opname is ingeschakeld; alleen de bevestiging was vertraagd."
        : "Doorlopende opname is uitgeschakeld; alleen de bevestiging was vertraagd.";
    } else {
      state.debugRecordingError = `Doorlopende opname kon niet worden ${enabled ? "ingeschakeld" : "uitgeschakeld"}. ${error.message || String(error)}`;
    }
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export function toggleSystemRecorder() {
  return setSystemRecorderEnabled(!isSystemRecorderEnabled());
}

export function requestSystemRecorderDisable() {
  if (!isSystemRecorderEnabled()) {
    return setSystemRecorderEnabled(true);
  }
  updateDebugRecordingState({
    debugRecordingConfirmDisable: true,
    debugRecordingError: "",
    debugRecordingNotice: "",
  });
  render();
}

export function cancelSystemRecorderDisable() {
  updateDebugRecordingState({ debugRecordingConfirmDisable: false });
  render();
}

export function confirmSystemRecorderDisable() {
  updateDebugRecordingState({ debugRecordingConfirmDisable: false });
  return setSystemRecorderEnabled(false);
}

export function toggleRecorderManage(event, button) {
  if (event && typeof event.preventDefault === "function") {
    event.preventDefault();
  }
  const details = button && typeof button.closest === "function"
    ? button.closest(".oq-debug-recording-manage")
    : null;
  updateDebugRecordingState({
    debugRecordingManageOpen: !(details && details.hasAttribute("open")),
  });
  render();
}

export async function restartRollingDebugRecording() {
  if (state.debugRecordingBusy) {
    return;
  }
  const previousRecordingId = getDebugRecordingId();
  debugRecordingMutationGeneration += 1;
  clearDebugRecordingDevicePollTimer();
  state.debugRecordingBusy = true;
  state.debugRecordingError = "";
  state.debugRecordingNotice = "";
  render();
  try {
    const payload = await postDebugRecordingDevice("restart");
    applyDebugRecordingDeviceStatus(payload);
    debugRecordingStatusFailureCount = 0;
    state.debugRecordingNotice = "Nieuwe opname gestart. De oude historie is gewist.";
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    const reconciled = await reconcileDebugRecordingMutation((status) => (
      Boolean(status?.active)
      && getDebugRecordingId(status) !== previousRecordingId
    ));
    if (reconciled) {
      state.debugRecordingNotice = "Nieuwe opname gestart; alleen de bevestiging was vertraagd.";
    } else {
      state.debugRecordingError = `Nieuwe opname starten mislukt. ${error.message || String(error)}`;
    }
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export function getDebugRecordingCompactJson(payload) {
  return JSON.stringify(payload);
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

export function getDebugRecordingDownloadEndpoint(rangeMinutes = getDebugRecordingDownloadRange()) {
  const range = Math.max(0, Number(rangeMinutes) || 0);
  return range > 0
    ? getDebugRecordingEndpoint(`download-range?last_minutes=${encodeURIComponent(range)}`)
    : getDebugRecordingEndpoint("download");
}

export function getDebugRecordingRangeLabel(rangeMinutes = getDebugRecordingDownloadRange()) {
  const range = Math.max(0, Number(rangeMinutes) || 0);
  if (range <= 0) {
    return `Volledige beschikbare historie · ${formatDebugRecordingDuration(getDebugRecordingRetainedDurationMs())}`;
  }
  return `Laatste ${range} minuten`;
}

export function getSystemRecorderIntroHtml() {
  return `De Systeemrecorder bewaart continu recente systeemgegevens voor diagnose en analyse. De gegevens blijven lokaal op het apparaat en worden niet automatisch verzonden. Een geëxporteerd diagnosebestand kun je verder bekijken met de <a href="${escapeHtml(SYSTEM_RECORDER_ANALYSER_URL)}" target="_blank" rel="noopener noreferrer">OpenHeatPumps analyser${renderDebugRecordingButtonIcon("external")}</a>.`;
}

export async function exportDebugRecordingBundle(mode, rangeMinutes = getDebugRecordingDownloadRange()) {
  if (getDebugRecordingSampleCount() === 0) {
    state.debugRecordingError = `Er is nog geen opname om te ${mode === "copy" ? "kopiëren" : "downloaden"}.`;
    render();
    return false;
  }
  state.debugRecordingBusy = true;
  state.debugRecordingError = "";
  const rangeLabel = getDebugRecordingRangeLabel(rangeMinutes);
  render();
  try {
    const response = await window.fetch(getDebugRecordingDownloadEndpoint(rangeMinutes), {
      cache: "no-store",
      headers: { "Cache-Control": "no-store" },
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    const bundle = await response.json();
    state.debugRecordingDeviceBundle = bundle;
    if (mode === "copy") {
      const copied = await copyTextToClipboard(getDebugRecordingCompactJson(bundle));
      if (!copied) {
        throw new Error("Kopiëren naar het klembord is niet gelukt.");
      }
    } else {
      downloadTextFile(getDebugRecordingFilename(bundle), getDebugRecordingCompactJson(bundle), "application/json");
    }
    acknowledgeDebugRecording(bundle);
    const action = mode === "copy" ? "gekopieerd" : "gedownload";
    state.debugRecordingNotice = `Diagnosebestand (${rangeLabel}) ${action}.`;
    return true;
  } catch (error) {
    state.debugRecordingError = mode === "copy"
      ? "Kopiëren mislukt. Probeer opnieuw of download het diagnosebestand."
      : "Download mislukt. Probeer opnieuw of kopieer de gegevens.";
    return false;
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export function downloadDebugRecordingBundle(rangeMinutes) {
  return exportDebugRecordingBundle("download", rangeMinutes);
}

export function downloadDebugRecordingRange() {
  return exportDebugRecordingBundle("download", getDebugRecordingDownloadRange());
}

export function copyDebugRecordingBundle(rangeMinutes) {
  return exportDebugRecordingBundle("copy", rangeMinutes);
}

export async function copyDebugRecordingAndOpenAnalyser(rangeMinutes) {
  // Open the tab synchronously from the click: opening it only after the
  // async export/copy may be blocked as a popup once click-activation expired.
  const analyserTab = typeof window.open === "function"
    ? window.open("about:blank", "_blank", "noopener,noreferrer")
    : null;
  const copied = await exportDebugRecordingBundle("copy", rangeMinutes);
  if (analyserTab && !analyserTab.closed) {
    if (copied) {
      analyserTab.location.href = SYSTEM_RECORDER_ANALYSER_URL;
    } else {
      analyserTab.close();
    }
  }
  return copied;
}

const debugRecordingActionHandlers = {
  "open-debug-recording-modal": () => {
    state.systemModal = "debug-recording";
    state.debugRecordingError = "";
    state.debugRecordingNotice = "";
    state.debugRecordingConfirmDisable = false;
    state.debugRecordingManageOpen = false;
    render();
    return refreshDebugRecordingDeviceStatus();
  },
  "start-debug-recording": (button) => startDebugRecording(button.dataset.debugMinutes || 15),
  "start-rolling-debug-recording": () => startRollingDebugRecording(),
  "restart-rolling-debug-recording": () => restartRollingDebugRecording(),
  "request-disable-system-recorder": () => requestSystemRecorderDisable(),
  "cancel-disable-system-recorder": () => cancelSystemRecorderDisable(),
  "confirm-disable-system-recorder": () => confirmSystemRecorderDisable(),
  "toggle-recorder-manage": (button, event) => toggleRecorderManage(event, button),
  "select-debug-recording-duration": (button) => setDebugRecordingSelectedMinutes(button.dataset.debugMinutes || 15),
  "select-debug-recording-range": (button) => setDebugRecordingDownloadRange(button.dataset.lastMinutes || 0),
  "stop-debug-recording": () => stopDebugRecording(),
  "download-debug-recording": () => downloadDebugRecordingRange(),
  "download-debug-recording-range": () => downloadDebugRecordingRange(),
  "copy-debug-recording": () => copyDebugRecordingBundle(),
  "copy-debug-recording-analyser": () => copyDebugRecordingAndOpenAnalyser(),
};

export function handleDebugRecordingAction(action, button, event) {
  return invokeActionMap(debugRecordingActionHandlers, action, button, event);
}

export function renderDebugRecordingModal() {
  const enabled = isSystemRecorderEnabled();
  const available = isSystemRecorderAvailable();
  const sampleCount = getDebugRecordingSampleCount();
  const busy = state.debugRecordingBusy;
  const stringOverflow = state.debugRecordingDeviceStatus?.string_overflow === true;
  const retainedMs = getDebugRecordingRetainedDurationMs();
  const downloadRange = getDebugRecordingDownloadRange();
  const hasRecording = sampleCount > 0;
  const deviceUnavailable = !available;
  const intervalS = Math.max(1, Number(state.debugRecordingDeviceStatus?.interval_s || 10));
  const confirmDisable = state.debugRecordingConfirmDisable === true;
  const feedback = state.debugRecordingError
    ? { kind: "error", icon: "alert", text: state.debugRecordingError }
    : state.debugRecordingNotice
      ? { kind: "success", icon: "check", text: state.debugRecordingNotice }
      : stringOverflow
        ? {
          kind: "warning",
          icon: "alert",
          text: "De stringopslag is volgelopen; enkele tekstwaarden ontbreken in de opname.",
        }
        : null;
  return renderModalShell({
    id: "system",
    titleId: "oq-debug-recording-modal-title",
    kicker: "Diagnostiek",
    title: "Systeemrecorder",
    copy: "",
    className: "oq-debug-recording-modal",
    closeAction: "close-system-modal",
    closeLabel: "Sluit Systeemrecorder",
    body: `
        <p class="oq-helper-modal-copy">${getSystemRecorderIntroHtml()}</p>
        <section class="oq-debug-recording-card" aria-label="Doorlopende opname">
          <div class="oq-debug-recording-card-head">
            <span class="oq-debug-recording-heading-icon" aria-hidden="true">${renderDebugRecordingSvgIcon("activity")}</span>
            <h3>Doorlopende opname</h3>
            <span class="oq-debug-recording-switchwrap">
              <button
                class="oq-settings-toggle-switch${enabled ? " is-on" : ""}"
                type="button"
                role="switch"
                aria-checked="${enabled ? "true" : "false"}"
                aria-label="Doorlopende opname"
                data-oq-action="request-disable-system-recorder"
                ${busy || deviceUnavailable ? "disabled" : ""}
              >
                <span class="oq-settings-toggle-switch-track" aria-hidden="true"><span class="oq-settings-toggle-switch-knob"></span></span>
              </button>
            </span>
          </div>
          ${deviceUnavailable ? `
            <p class="oq-debug-recording-subtle">Systeemrecorder niet beschikbaar op deze firmware.</p>
          ` : `
            <div class="oq-debug-recording-availability">
              <span>Beschikbaar</span>
              <strong data-oq-recorder-availability><span data-oq-recorder-retained>${escapeHtml(formatDebugRecordingDuration(retainedMs))}</span> <span class="oq-debug-recording-samples" data-oq-recorder-samples>(${sampleCount} samples)</span></strong>
            </div>
            <p class="oq-debug-recording-subtle">Lokaal opgeslagen · elke ${escapeHtml(String(intervalS))} s</p>
            ${!enabled ? `
              <p class="oq-debug-recording-subtle">Er worden momenteel geen nieuwe systeemgegevens opgeslagen.</p>
            ` : ""}
            ${enabled && !state.debugRecordingActive ? `
              <p class="oq-debug-recording-subtle">De opname is momenteel niet actief.</p>
            ` : ""}
          `}
          ${confirmDisable && enabled ? `
            <div class="oq-debug-recording-confirm" role="group" aria-label="Doorlopende opname uitschakelen">
              <p><strong>Doorlopende opname uitschakelen?</strong> Er worden geen nieuwe systeemgegevens meer opgeslagen. De huidige historie blijft beschikbaar.</p>
              <div class="oq-debug-recording-confirm-actions">
                <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="cancel-disable-system-recorder" ${busy ? "disabled" : ""}>Annuleren</button>
                <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-disable-system-recorder" ${busy ? "disabled" : ""}>Uitschakelen</button>
              </div>
            </div>
          ` : ""}
        </section>
        <section class="oq-debug-recording-export" aria-label="Exporteren">
          <h3>Exporteren</h3>
          <p class="oq-helper-modal-copy">Kies hoeveel van de beschikbare historie je wilt exporteren.</p>
          <div class="oq-debug-recording-segments" role="group" aria-label="Kies exportbereik">
            ${DEBUG_RECORDING_DOWNLOAD_RANGE_OPTIONS.map((option) => {
              const minutes = Number(option.minutes);
              const selected = minutes === downloadRange;
              return `
                <button
                  class="oq-debug-recording-segment${selected ? " oq-debug-recording-segment--selected" : ""}"
                  type="button"
                  data-oq-action="select-debug-recording-range"
                  data-last-minutes="${minutes}"
                  aria-pressed="${selected ? "true" : "false"}"
                  ${busy ? "disabled" : ""}
                >
                  ${escapeHtml(option.label)}
                </button>
              `;
            }).join("")}
          </div>
          <p class="oq-debug-recording-rangelabel"><strong data-oq-recorder-range>${escapeHtml(getDebugRecordingRangeLabel(downloadRange))}</strong></p>
          <div class="oq-debug-recording-exportactions">
            <button class="oq-helper-button oq-helper-button--primary oq-debug-recording-primary" type="button" data-oq-action="download-debug-recording-range" ${!hasRecording || busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("download")}Download diagnosebestand</button>
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="copy-debug-recording" ${!hasRecording || busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("copy")}Kopieer gegevens</button>
          </div>
          <button class="oq-debug-recording-linkaction" type="button" data-oq-action="copy-debug-recording-analyser" ${!hasRecording || busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("external")}Kopieer &amp; open analyser</button>
          ${feedback ? `
            <p class="oq-debug-recording-feedback oq-debug-recording-feedback--${feedback.kind}" role="status">
              ${renderDebugRecordingButtonIcon(feedback.icon)}
              <span>${escapeHtml(feedback.text)}</span>
            </p>
          ` : ""}
        </section>
        <details class="oq-debug-recording-manage"${state.debugRecordingManageOpen ? " open" : ""}>
          <summary data-oq-action="toggle-recorder-manage">Recorderbeheer</summary>
          <p class="oq-debug-recording-subtle">Wist de huidige historie en start een nieuwe opname.</p>
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="restart-rolling-debug-recording" ${busy || deviceUnavailable || !enabled ? "disabled" : ""}>${renderDebugRecordingButtonIcon("activity")}Nieuwe opname starten</button>
        </details>`,
  });
}
