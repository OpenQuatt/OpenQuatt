import { invokeActionMap } from "../core/action-router.js";
import { copyTextToClipboard, downloadTextFile } from "../core/browser-utils.js";
import {
  DEBUG_RECORDING_DOWNLOAD_RANGE_OPTIONS,
  SYSTEM_RECORDER_ANALYSER_URL,
} from "../core/config.js";
import { formatNumber, t } from "../i18n/index.js";
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
    return t("debugRecording.statusStarting");
  }
  if (state.debugRecordingDeviceStatus.available === false) {
    return t("debugRecording.statusUnavailable");
  }
  if (!isSystemRecorderEnabled()) {
    return t("debugRecording.statusDisabled");
  }
  if (!state.debugRecordingActive) {
    return t("debugRecording.statusInactive");
  }
  if (isDebugRecordingRolling()) {
    return t("debugRecording.statusActive");
  }
  return t("debugRecording.statusRecording");
}

export function getDebugRecordingStatusCopy() {
  if (state.debugRecordingDeviceStatus && state.debugRecordingDeviceStatus.available === false) {
    return t("debugRecording.copyUnavailable");
  }
  if (!isSystemRecorderEnabled()) {
    return t("debugRecording.copyDisabled");
  }
  return t("debugRecording.copyActive");
}

export function getDebugRecordingHubStatusLabel() {
  if (state.debugRecordingDeviceStatus && state.debugRecordingDeviceStatus.available === false) {
    return t("debugRecording.statusUnavailable");
  }
  if (!isSystemRecorderEnabled()) {
    return t("debugRecording.statusDisabled");
  }
  if (state.debugRecordingActive) {
    return t("debugRecording.statusActive");
  }
  if (!state.debugRecordingDeviceStatus) {
    return t("debugRecording.statusStarting");
  }
  return t("debugRecording.statusInactive");
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

export function getDebugRecordingId(source = state.debugRecordingDeviceStatus) {
  return String(source?.recording_id ?? source?.recording?.recording_id ?? "").trim();
}

// An active recorder is the normal situation, so no permanent header badge.
// Only a deviating state (disabled / unavailable) is surfaced.
export function renderDebugRecordingHeaderStatus() {
  const status = state.debugRecordingDeviceStatus;
  if (!status) {
    return "";
  }
  if (status.available === false) {
    const title = t("debugRecording.hubUnavailable");
    return `
    <button
      class="oq-debug-recording-header-status"
      type="button"
      data-oq-action="open-debug-recording-modal"
      aria-label="${escapeHtml(title)}"
      title="${escapeHtml(title)}"
    >
      <span>${escapeHtml(t("debugRecording.headerUnavailable"))}</span>
    </button>
  `;
  }
  if (status.enabled === false) {
    const title = t("debugRecording.headerDisabledTitle");
    return `
    <button
      class="oq-debug-recording-header-status"
      type="button"
      data-oq-action="open-debug-recording-modal"
      aria-label="${escapeHtml(title)}"
      title="${escapeHtml(title)}"
    >
      <span>${escapeHtml(t("debugRecording.headerDisabled"))}</span>
    </button>
  `;
  }
  if (status.active === false) {
    const title = t("debugRecording.headerInactiveTitle");
    return `
    <button
      class="oq-debug-recording-header-status"
      type="button"
      data-oq-action="open-debug-recording-modal"
      aria-label="${escapeHtml(title)}"
      title="${escapeHtml(title)}"
    >
      <span>${escapeHtml(t("debugRecording.headerInactive"))}</span>
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

export function scheduleDebugRecordingDeviceStatusPoll(delayMs = 2000, options = {}) {
  clearDebugRecordingDevicePollTimer();
  if (!options.force && !state.debugRecordingActive && state.systemModal !== "debug-recording") {
    return;
  }
  const cadenceMs = options.force
    ? Math.max(0, Number(delayMs) || 0)
    : Math.max(0, Number(state.systemModal === "debug-recording" ? delayMs : 5000) || 0);
  state.debugRecordingDevicePollTimer = window.setTimeout(() => {
    void refreshDebugRecordingDeviceStatus({ silent: true });
  }, cadenceMs);
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
    if (String(state.debugRecordingError || "").startsWith(t("debugRecording.statusFetchPrefix"))) {
      state.debugRecordingError = "";
    }
    if (options.silent && state.systemModal === "debug-recording"
      && getDebugRecordingChromeSignature() === chromeBefore) {
      modalPatched = patchDebugRecordingModalNumbers();
    }
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    debugRecordingStatusFailureCount += 1;
    const hadStatus = Boolean(state.debugRecordingDeviceStatus);
    if (!hadStatus) {
      applyDebugRecordingDeviceUnavailableStatus();
    }
    state.debugRecordingError = t("debugRecording.statusFetchFailed", { error: error.message || String(error) });
    // A failed first fetch must not freeze the recorder as permanently
    // unavailable: retry boundedly so boot transients recover on their own.
    const initialRetry = !hadStatus && debugRecordingStatusFailureCount <= 5;
    if (state.debugRecordingActive || state.systemModal === "debug-recording" || initialRetry) {
      scheduleDebugRecordingDeviceStatusPoll(
        Math.min(30000, 2000 * (2 ** debugRecordingStatusFailureCount)),
        { force: initialRetry },
      );
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
    throw new Error(t("debugRecording.csrfMissing"));
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
      ? t("debugRecording.enabledNotice")
      : t("debugRecording.disabledNotice");
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    const reconciled = await reconcileDebugRecordingMutation(
      (status) => (status?.enabled !== false) === enabled,
    );
    if (reconciled) {
      state.debugRecordingNotice = enabled
        ? t("debugRecording.enabledReconciled")
        : t("debugRecording.disabledReconciled");
    } else {
      state.debugRecordingError = t(enabled ? "debugRecording.enableFailed" : "debugRecording.disableFailed", { error: error.message || String(error) });
    }
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
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
    state.debugRecordingNotice = t("debugRecording.restartDone");
    scheduleDebugRecordingDeviceStatusPoll();
  } catch (error) {
    const reconciled = await reconcileDebugRecordingMutation((status) => (
      Boolean(status?.active)
      && getDebugRecordingId(status) !== previousRecordingId
    ));
    if (reconciled) {
      state.debugRecordingNotice = t("debugRecording.restartReconciled");
    } else {
      state.debugRecordingError = t("debugRecording.restartFailed", { error: error.message || String(error) });
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
    return t("debugRecording.rangeFull", { duration: formatDebugRecordingDuration(getDebugRecordingRetainedDurationMs()) });
  }
  return t("debugRecording.rangeLast", { minutes: formatNumber(range, { maximumFractionDigits: 0 }) });
}

export function getSystemRecorderIntroHtml() {
  return `${escapeHtml(t("debugRecording.introPre"))} <a href="${escapeHtml(SYSTEM_RECORDER_ANALYSER_URL)}" target="_blank" rel="noopener noreferrer">${escapeHtml(t("debugRecording.introLink"))}${renderDebugRecordingButtonIcon("external")}</a>.`;
}

export async function exportDebugRecordingBundle(mode, rangeMinutes = getDebugRecordingDownloadRange()) {
  if (getDebugRecordingSampleCount() === 0) {
    state.debugRecordingError = t("debugRecording.exportEmpty", { action: mode === "copy" ? t("debugRecording.exportCopy") : t("debugRecording.exportDownload") });
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
    if (mode === "copy") {
      const copied = await copyTextToClipboard(getDebugRecordingCompactJson(bundle));
      if (!copied) {
        throw new Error(t("debugRecording.copyFailClipboard"));
      }
    } else {
      downloadTextFile(getDebugRecordingFilename(bundle), getDebugRecordingCompactJson(bundle), "application/json");
    }
    const action = mode === "copy" ? t("debugRecording.exportCopied") : t("debugRecording.exportDownloaded");
    state.debugRecordingNotice = t("debugRecording.exportDone", { range: rangeLabel, action });
    return true;
  } catch (error) {
    state.debugRecordingError = mode === "copy"
      ? t("debugRecording.exportCopyFail")
      : t("debugRecording.exportDownloadFail");
    return false;
  } finally {
    state.debugRecordingBusy = false;
    render();
  }
}

export function downloadDebugRecordingRange() {
  return exportDebugRecordingBundle("download", getDebugRecordingDownloadRange());
}

export function copyDebugRecordingBundle(rangeMinutes) {
  return exportDebugRecordingBundle("copy", rangeMinutes);
}

export async function copyDebugRecordingAndOpenAnalyser(rangeMinutes) {
  // Copy first, then open: opening the analyser tab upfront steals document
  // focus, after which browsers refuse the clipboard write. Opening the URL
  // directly needs no window handle, so `noopener` returning null is fine.
  // Popup-wise this relies on transient click-activation surviving the fast
  // local export fetch.
  const copied = await exportDebugRecordingBundle("copy", rangeMinutes);
  if (copied && typeof window.open === "function") {
    window.open(SYSTEM_RECORDER_ANALYSER_URL, "_blank", "noopener,noreferrer");
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
  "restart-rolling-debug-recording": () => restartRollingDebugRecording(),
  "request-disable-system-recorder": () => requestSystemRecorderDisable(),
  "cancel-disable-system-recorder": () => cancelSystemRecorderDisable(),
  "confirm-disable-system-recorder": () => confirmSystemRecorderDisable(),
  "toggle-recorder-manage": (button, event) => toggleRecorderManage(event, button),
  "select-debug-recording-range": (button) => setDebugRecordingDownloadRange(button.dataset.lastMinutes || 0),
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
          text: t("debugRecording.stringOverflow"),
        }
        : null;
  return renderModalShell({
    id: "system",
    titleId: "oq-debug-recording-modal-title",
    kicker: t("debugRecording.modalKicker"),
    title: t("debugRecording.modalTitle"),
    copy: "",
    className: "oq-debug-recording-modal",
    closeAction: "close-system-modal",
    closeLabel: t("debugRecording.modalClose"),
    body: `
        <p class="oq-helper-modal-copy">${getSystemRecorderIntroHtml()}</p>
        <section class="oq-debug-recording-card" aria-label="${escapeHtml(t("debugRecording.cardAria"))}">
          <div class="oq-debug-recording-card-head">
            <span class="oq-debug-recording-heading-icon" aria-hidden="true">${renderDebugRecordingSvgIcon("activity")}</span>
            <h3>${escapeHtml(t("debugRecording.cardTitle"))}</h3>
            <span class="oq-debug-recording-switchwrap">
              <button
                class="oq-settings-toggle-switch${enabled ? " is-on" : ""}"
                type="button"
                role="switch"
                aria-checked="${enabled ? "true" : "false"}"
                aria-label="${escapeHtml(t("debugRecording.cardAria"))}"
                data-oq-action="request-disable-system-recorder"
                ${busy || deviceUnavailable ? "disabled" : ""}
              >
                <span class="oq-settings-toggle-switch-track" aria-hidden="true"><span class="oq-settings-toggle-switch-knob"></span></span>
              </button>
            </span>
          </div>
          ${deviceUnavailable ? `
            <p class="oq-debug-recording-subtle">${escapeHtml(t("debugRecording.deviceUnavailable"))}</p>
          ` : `
            <div class="oq-debug-recording-availability">
              <span>${escapeHtml(t("debugRecording.availableLabel"))}</span>
              <strong data-oq-recorder-availability><span data-oq-recorder-retained>${escapeHtml(formatDebugRecordingDuration(retainedMs))}</span> <span class="oq-debug-recording-samples" data-oq-recorder-samples>${escapeHtml(t("debugRecording.samples", { count: sampleCount }))}</span></strong>
            </div>
            <p class="oq-debug-recording-subtle">${escapeHtml(t("debugRecording.storedLocal", { seconds: formatNumber(intervalS, { maximumFractionDigits: 0 }) }))}</p>
            ${!enabled ? `
              <p class="oq-debug-recording-subtle">${escapeHtml(t("debugRecording.notStoring"))}</p>
            ` : ""}
            ${enabled && !state.debugRecordingActive ? `
              <p class="oq-debug-recording-subtle">${escapeHtml(t("debugRecording.notActive"))}</p>
            ` : ""}
          `}
          ${confirmDisable && enabled ? `
            <div class="oq-debug-recording-confirm" role="group" aria-label="${escapeHtml(t("debugRecording.confirmTitle"))}">
              <p><strong>${escapeHtml(t("debugRecording.confirmTitle"))}</strong> ${escapeHtml(t("debugRecording.confirmCopy"))}</p>
              <div class="oq-debug-recording-confirm-actions">
                <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="cancel-disable-system-recorder" ${busy ? "disabled" : ""}>${escapeHtml(t("common.cancel"))}</button>
                <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-disable-system-recorder" ${busy ? "disabled" : ""}>${escapeHtml(t("common.disable"))}</button>
              </div>
            </div>
          ` : ""}
        </section>
        <section class="oq-debug-recording-export" aria-label="${escapeHtml(t("debugRecording.exportAria"))}">
          <h3>${escapeHtml(t("debugRecording.exportTitle"))}</h3>
          <p class="oq-helper-modal-copy">${escapeHtml(t("debugRecording.exportCopy"))}</p>
          <div class="oq-debug-recording-segments" role="group" aria-label="${escapeHtml(t("debugRecording.exportRangeAria"))}">
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
                  ${escapeHtml(option.labelKey ? t(option.labelKey) : option.label)}
                </button>
              `;
            }).join("")}
          </div>
          <p class="oq-debug-recording-rangelabel"><strong data-oq-recorder-range>${escapeHtml(getDebugRecordingRangeLabel(downloadRange))}</strong></p>
          <div class="oq-debug-recording-exportactions">
            <button class="oq-helper-button oq-helper-button--primary oq-debug-recording-primary" type="button" data-oq-action="download-debug-recording-range" ${!hasRecording || busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("download")}${escapeHtml(t("debugRecording.downloadFile"))}</button>
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="copy-debug-recording" ${!hasRecording || busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("copy")}${escapeHtml(t("debugRecording.copyData"))}</button>
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="copy-debug-recording-analyser" ${!hasRecording || busy ? "disabled" : ""}>${renderDebugRecordingButtonIcon("external")}${escapeHtml(t("debugRecording.copyAnalyser"))}</button>
          </div>
          ${feedback ? `
            <p class="oq-debug-recording-feedback oq-debug-recording-feedback--${feedback.kind}" role="status">
              ${renderDebugRecordingButtonIcon(feedback.icon)}
              <span>${escapeHtml(feedback.text)}</span>
            </p>
          ` : ""}
        </section>
        <details class="oq-debug-recording-manage"${state.debugRecordingManageOpen ? " open" : ""}>
          <summary data-oq-action="toggle-recorder-manage">${escapeHtml(t("debugRecording.manageTitle"))}</summary>
          <p class="oq-debug-recording-subtle">${escapeHtml(t("debugRecording.manageCopy"))}</p>
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="restart-rolling-debug-recording" ${busy || deviceUnavailable || !enabled ? "disabled" : ""}>${renderDebugRecordingButtonIcon("activity")}${escapeHtml(t("debugRecording.manageStart"))}</button>
        </details>`,
  });
}
