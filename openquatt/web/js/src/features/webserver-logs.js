import { copyTextToClipboard } from "../core/browser-utils.js";
import { refreshEntities } from "../core/entity-sync.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { invokeActionMap } from "../core/action-router.js";
import { updateWebServerLogState } from "../core/feature-state.js";
import { setWebServerLogControls } from "../core/webserver-log-controls.js";
import { escapeHtml } from "../core/html.js";
import { render } from "../core/render-scheduler.js";
import { createScrollKeeper } from "../core/scroll-keeper.js";
import { renderModalShell } from "../core/modal-shell.js";
import { renderSettingsInfoToggle } from "../settings/controls.js";

export const WEB_SERVER_LOG_MAX_ENTRIES = 250;

export function getWebServerLogDemoEntries() {
  if (!__OQ_PREVIEW__ || typeof window === "undefined") {
    return [];
  }

  const source = window.__OQ_DEV_WEBSERVER_LOGS__;
  const values = typeof source === "function"
    ? source()
    : source;

  if (!Array.isArray(values)) {
    return [];
  }

  return values
    .map((entry) => String(entry || ""))
    .filter((entry) => entry.trim() !== "");
}

export function isWebServerLogDemoMode() {
  if (typeof window === "undefined") {
    return false;
  }

  return getWebServerLogDemoEntries().length > 0;
}

export function getWebServerLogUrl() {
  return `${getBasePath()}/events`;
}

export function getWebServerLogHistoryUrl() {
  return `${getBasePath()}/openquatt/logs/recent`;
}

export function getWebServerLogClearUrl() {
  return `${getBasePath()}/openquatt/logs/clear`;
}

export function getWebServerLogStatusLabel() {
  if (state.nativeOpen) {
    return "Niet beschikbaar";
  }
  if (isWebServerLogDemoMode()) {
    return "Voorbeeld";
  }
  if (state.webServerLogEnabled === false) {
    return "Niet beschikbaar";
  }
  return "Beschikbaar";
}

export function formatWebServerLogDuration(value) {
  const totalSeconds = Math.max(0, Math.floor(Number(value) / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return `${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

export function formatWebServerLogDateTime(value) {
  const numeric = Number(value) || 0;
  if (numeric > 946684800000) {
    const date = value instanceof Date ? value : new Date(numeric);
    const options = {
      day: "2-digit",
      month: "2-digit",
      year: "numeric",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    };
    try {
      return new Intl.DateTimeFormat("nl-NL", options).format(date);
    } catch (_error) {
      return date.toLocaleString("nl-NL", options);
    }
  }

  return formatWebServerLogDuration(numeric);
}

export function getWebServerLogTimeTooltip(value) {
  const numeric = Number(value) || 0;
  if (numeric > 946684800000) {
    return new Date(numeric).toLocaleString("nl-NL", {
      day: "numeric",
      month: "short",
      year: "numeric",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
  }

  const totalSeconds = Math.max(0, Math.floor(numeric / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return `Sinds opstart: ${hours}u ${minutes}m ${seconds}s`;
}

export function getWebServerLoggerLevelEntity() {
  return state.entities?.debugLevel || null;
}

export function getWebServerLoggerLevelOptions(entity = getWebServerLoggerLevelEntity()) {
  const options = Array.isArray(entity?.option)
    ? entity.option
    : Array.isArray(entity?.options)
      ? entity.options
      : [];
  return options.length ? options : ["NONE", "ERROR", "WARN", "INFO", "CONFIG", "DEBUG"];
}

export function getWebServerLoggerLevelValue(entity = getWebServerLoggerLevelEntity()) {
  const value = String(entity?.value ?? entity?.state ?? "").trim();
  const options = getWebServerLoggerLevelOptions(entity);
  return options.includes(value) ? value : (options.includes("INFO") ? "INFO" : options[0] || "");
}

export function isDuplicateWebServerLogEntry(candidate, existingEntry = null) {
  if (!candidate || !existingEntry) {
    return false;
  }

  const candidateSeq = Number(candidate.seq);
  const existingSeq = Number(existingEntry.seq);
  if (Number.isFinite(candidateSeq) && Number.isFinite(existingSeq) && candidateSeq === existingSeq) {
    return true;
  }

  const candidateRaw = String(candidate.raw ?? candidate.text ?? "").trim();
  const existingRaw = String(existingEntry.raw ?? existingEntry.text ?? "").trim();
  if (!candidateRaw || candidateRaw !== existingRaw) {
    return false;
  }

  const candidateReceivedAt = Number(candidate.receivedAt ?? candidate.ts ?? 0);
  const existingReceivedAt = Number(existingEntry.receivedAt ?? existingEntry.ts ?? 0);
  return Math.abs(candidateReceivedAt - existingReceivedAt) <= 2000;
}

export function compareWebServerLogEntries(left, right) {
  const leftTime = Number(left.receivedAt ?? left.ts ?? 0);
  const rightTime = Number(right.receivedAt ?? right.ts ?? 0);
  if (leftTime !== rightTime) {
    return leftTime - rightTime;
  }

  const leftSeq = Number(left.seq ?? 0);
  const rightSeq = Number(right.seq ?? 0);
  if (leftSeq !== rightSeq) {
    return leftSeq - rightSeq;
  }

  return String(left.raw ?? "").localeCompare(String(right.raw ?? ""));
}

export function mergeWebServerLogEntries(entries, { prepend = false } = {}) {
  if (!Array.isArray(entries) || entries.length === 0) {
    return;
  }

  const merged = prepend
    ? [...entries, ...state.webServerLogEntries]
    : [...state.webServerLogEntries, ...entries];
  merged.sort(compareWebServerLogEntries);

  const deduped = [];
  for (const entry of merged) {
    const previous = deduped[deduped.length - 1] || null;
    if (!isDuplicateWebServerLogEntry(entry, previous)) {
      deduped.push(entry);
    }
  }

  state.webServerLogEntries = deduped.slice(-WEB_SERVER_LOG_MAX_ENTRIES);
}

export function createWebServerLogEntry(raw, options = {}) {
  const text = stripAnsiSequences(raw).trimEnd();
  const receivedAt = Number(options.receivedAt);
  const seq = Number(options.seq);
  return {
    raw,
    text,
    tone: getWebServerLogTone(raw),
    receivedAt: Number.isFinite(receivedAt) ? receivedAt : Date.now(),
    seq: Number.isFinite(seq) ? seq : undefined,
  };
}

export function getDemoLogReceivedAt(index, total) {
  const spacingMs = 90 * 1000;
  const offset = Math.max(0, total - index - 1) * spacingMs;
  return Date.now() - offset;
}

export function seedWebServerLogDemoEntries() {
  const entries = getWebServerLogDemoEntries();
  const total = entries.length;
  return entries.map((entry, index) => createWebServerLogEntry(entry, {
    receivedAt: getDemoLogReceivedAt(index, total),
    seq: index + 1,
  }));
}

export function scrollWebServerLogToBottom() {
  const scroller = getWebServerLogScrollerElement();
  if (!scroller) {
    return;
  }
  scroller.scrollTop = scroller.scrollHeight;
}

const webServerLogScrollKeeper = createScrollKeeper({
  getScroller: getWebServerLogScrollerElement,
  getToken: () => state.webServerLogScrollRestoreToken,
  setToken: (token) => { state.webServerLogScrollRestoreToken = token; },
  isActive: () => state.systemModal === "webserver-logs",
  preserveGrowth: true,
  stickToBottom: true,
});

export const captureWebServerLogScrollState = webServerLogScrollKeeper.capture;
export const queueWebServerLogScrollRestore = webServerLogScrollKeeper.queue;

export function getCm100CommissioningModalScrollerElement() {
  if (!state.root) {
    return null;
  }
  return state.root.querySelector("[data-oq-cm100-commissioning-scroller]");
}

const cm100CommissioningScrollKeeper = createScrollKeeper({
  getScroller: getCm100CommissioningModalScrollerElement,
  getToken: () => state.cm100CommissioningScrollRestoreToken,
  setToken: (token) => { state.cm100CommissioningScrollRestoreToken = token; },
  isActive: () => state.systemModal === "cm100-commissioning",
  preserveGrowth: true,
  stickToBottom: true,
});

export const captureCm100CommissioningScrollState = cm100CommissioningScrollKeeper.capture;
export const queueCm100CommissioningScrollRestore = cm100CommissioningScrollKeeper.queue;

export function getServiceTaskModalScrollerElement() {
  if (!state.root) {
    return null;
  }
  return state.root.querySelector("[data-oq-service-task-scroller]");
}

const serviceTaskModalScrollKeeper = createScrollKeeper({
  getScroller: getServiceTaskModalScrollerElement,
  getToken: () => state.serviceTaskModalScrollRestoreToken,
  setToken: (token) => { state.serviceTaskModalScrollRestoreToken = token; },
  isActive: () => String(state.systemModal || "").startsWith("service-task-"),
});

export const captureServiceTaskModalScrollState = serviceTaskModalScrollKeeper.capture;
export const queueServiceTaskModalScrollRestore = serviceTaskModalScrollKeeper.queue;

export function getHistoryStorageModalScrollerElement() {
  if (!state.root) {
    return null;
  }
  return state.root.querySelector("[data-oq-history-storage-scroller]");
}

const historyStorageModalScrollKeeper = createScrollKeeper({
  getScroller: getHistoryStorageModalScrollerElement,
  getToken: () => state.historyStorageModalScrollRestoreToken,
  setToken: (token) => { state.historyStorageModalScrollRestoreToken = token; },
  isActive: () => state.systemModal === "history-storage",
});

export const captureHistoryStorageModalScrollState = historyStorageModalScrollKeeper.capture;
export const queueHistoryStorageModalScrollRestore = historyStorageModalScrollKeeper.queue;

export function getSettingsBackupRestoreModalScrollerElement() {
  if (!state.root) {
    return null;
  }
  return state.root.querySelector("[data-oq-settings-backup-restore-scroller]");
}

const settingsBackupRestoreModalScrollKeeper = createScrollKeeper({
  getScroller: getSettingsBackupRestoreModalScrollerElement,
  getToken: () => state.settingsBackupRestoreScrollRestoreToken,
  setToken: (token) => { state.settingsBackupRestoreScrollRestoreToken = token; },
  isActive: () => state.systemModal === "settings-backup-restore",
});

export const captureSettingsBackupRestoreModalScrollState = settingsBackupRestoreModalScrollKeeper.capture;
export const queueSettingsBackupRestoreModalScrollRestore = settingsBackupRestoreModalScrollKeeper.queue;

export async function refreshWebServerLogHistory(options = {}) {
  if (state.nativeOpen || typeof window.fetch !== "function") {
    return;
  }

  const scrollState = options.scrollState || captureWebServerLogScrollState();
  const replaceEntries = options.replaceEntries === true || state.webServerLogHistoryNeedsReconcile === true;
  const entriesBeforeRequest = replaceEntries ? new Set(state.webServerLogEntries) : null;
  const requestToken = Number(state.webServerLogHistoryRequestToken || 0) + 1;
  state.webServerLogHistoryRequestToken = requestToken;
  state.webServerLogHistoryLoading = true;
  state.webServerLogHistoryError = "";

  try {
    const response = await window.fetch(getWebServerLogHistoryUrl(), {
      headers: {
        "Cache-Control": "no-store",
      },
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const payload = await response.json();
    if (state.systemModal !== "webserver-logs" || state.webServerLogHistoryRequestToken !== requestToken) {
      return;
    }

    state.webServerLogCsrfToken = String(payload.csrf_token || "");
    const recentEntries = normalizeRecentWebServerLogPayload(payload);
    const liveEntries = replaceEntries
      ? state.webServerLogEntries.filter((entry) => !entriesBeforeRequest.has(entry))
      : [];
    if (replaceEntries) {
      state.webServerLogEntries = [];
      state.webServerLogRecentTail = [];
      state.webServerLogRecentAnchorAt = 0;
    }
    state.webServerLogHistoryLoaded = true;
    state.webServerLogHistoryNeedsReconcile = false;
    if (recentEntries.length > 0) {
      mergeWebServerLogEntries(recentEntries, { prepend: true });
      state.webServerLogRecentTail = recentEntries.slice(-4).map((entry) => String(entry.raw ?? entry.text ?? ""));
      state.webServerLogRecentAnchorAt = Date.now();
    }
    if (liveEntries.length > 0) {
      mergeWebServerLogEntries(liveEntries);
    }
  } catch (error) {
    if (state.systemModal === "webserver-logs" && state.webServerLogHistoryRequestToken === requestToken) {
      state.webServerLogHistoryError = error instanceof Error ? error.message : "Recente logs konden niet worden opgehaald.";
    }
  } finally {
    if (state.webServerLogHistoryRequestToken === requestToken) {
      state.webServerLogHistoryLoading = false;
    }
    if (state.systemModal === "webserver-logs" && state.webServerLogHistoryRequestToken === requestToken) {
      render();
      queueWebServerLogScrollRestore(scrollState);
    }
  }
}

export function normalizeRecentWebServerLogEntry(entry, fallbackSeq = 0) {
  if (!entry || typeof entry !== "object") {
    return null;
  }

  const raw = String(entry.raw ?? "").trim() || String(entry.message ?? "").trim();
  if (!raw) {
    return null;
  }

  return createWebServerLogEntry(raw, {
    receivedAt: Number(entry.ts ?? entry.timestamp_ms ?? entry.receivedAt ?? Date.now()),
    seq: Number(entry.seq ?? fallbackSeq),
  });
}

export function normalizeRecentWebServerLogPayload(payload) {
  if (!payload || typeof payload !== "object") {
    return [];
  }

  if (payload.enabled === false) {
    return [];
  }

  const entries = Array.isArray(payload.entries) ? payload.entries : [];
  return entries
    .map((entry, index) => normalizeRecentWebServerLogEntry(entry, index + 1))
    .filter((entry) => entry !== null);
}

export function shouldIgnoreLiveWebServerLogEntry(entry) {
  if (!entry || !Array.isArray(state.webServerLogRecentTail) || state.webServerLogRecentTail.length === 0) {
    return false;
  }

  const recentAgeMs = Date.now() - Number(state.webServerLogRecentAnchorAt || 0);
  if (recentAgeMs > 2500) {
    return false;
  }

  const raw = String(entry.raw ?? entry.text ?? "").trim();
  if (!raw) {
    return false;
  }

  return state.webServerLogRecentTail.includes(raw);
}

export function hasWebServerLogEntry(candidate, entries = state.webServerLogEntries) {
  if (!candidate || !Array.isArray(entries) || entries.length === 0) {
    return false;
  }

  return entries.some((existing) => isDuplicateWebServerLogEntry(candidate, existing));
}

export function openWebServerLogsModal() {
  if (isWebServerLogDemoMode() && state.webServerLogEntries.length === 0) {
    updateWebServerLogState({ webServerLogEntries: seedWebServerLogDemoEntries() });
  }
  updateWebServerLogState({ webServerLogCopyMessage: "", webServerLogCopyError: "" });
  state.settingsInfoOpen = "";
  state.systemModal = "webserver-logs";
  render();
  void refreshEntities(["debugLevel"], "all", { forceFast: true }).then(() => {
    if (state.systemModal !== "webserver-logs") {
      return;
    }
    const scrollState = captureWebServerLogScrollState();
    render();
    queueWebServerLogScrollRestore(scrollState);
  });
  scrollWebServerLogToBottom();
  if (!state.webServerLogHistoryLoaded || state.webServerLogEntries.length === 0) {
    void refreshWebServerLogHistory();
  }
}

export function clearWebServerLogOutput() {
  updateWebServerLogState({
    webServerLogEntries: [],
    webServerLogError: "",
    webServerLogHistoryError: "",
    webServerLogHistoryLoading: false,
    webServerLogHistoryLoaded: false,
    webServerLogHistoryNeedsReconcile: false,
    webServerLogCopyMessage: "",
    webServerLogCopyError: "",
    webServerLogHistoryRequestToken: state.webServerLogHistoryRequestToken + 1,
    webServerLogRecentTail: [],
    webServerLogRecentAnchorAt: 0,
  });
  webServerLogScrollKeeper.invalidate();
  if (state.systemModal === "webserver-logs") {
    render();
  }
}

export async function clearWebServerLogHistory() {
  if (state.busyAction) {
    return false;
  }

  if (state.nativeOpen || isWebServerLogDemoMode()) {
    clearWebServerLogOutput();
    return true;
  }

  if (typeof window.fetch !== "function") {
    state.webServerLogHistoryError = "De RAM-logbuffer kan niet vanuit deze browser worden geleegd.";
    render();
    return false;
  }

  const csrfToken = String(state.webServerLogCsrfToken || "");
  if (!csrfToken) {
    state.webServerLogHistoryError = "De beveiligingstoken voor de RAM-logbuffer ontbreekt. Open het logboek opnieuw.";
    render();
    return false;
  }

  state.busyAction = "clear-webserver-log-history";
  state.webServerLogHistoryError = "";
  closeWebServerLogStream();
  render();

  let cleared = false;
  try {
    let activeCsrfToken = csrfToken;
    let csrfRefreshed = false;
    while (true) {
      const body = new URLSearchParams();
      body.set("csrf_token", activeCsrfToken);
      const response = await window.fetch(getWebServerLogClearUrl(), { method: "POST", body });

      if (response.status === 403 && !csrfRefreshed) {
        csrfRefreshed = true;
        updateWebServerLogState({
          webServerLogCsrfToken: "",
          webServerLogHistoryLoaded: false,
          webServerLogHistoryRequestToken: state.webServerLogHistoryRequestToken + 1,
        });
        await refreshWebServerLogHistory();
        activeCsrfToken = String(state.webServerLogCsrfToken || "");
        if (activeCsrfToken) {
          continue;
        }
      }

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      break;
    }
    clearWebServerLogOutput();
    cleared = true;
  } catch (error) {
    updateWebServerLogState({
      webServerLogHistoryLoaded: false,
      webServerLogHistoryNeedsReconcile: true,
    });
    state.webServerLogHistoryError = `De RAM-logbuffer kon niet worden geleegd (${error instanceof Error ? error.message : "onbekende fout"}).`;
  } finally {
    state.busyAction = "";
    if (state.systemModal === "webserver-logs") {
      render();
    }
  }

  const clearError = state.webServerLogHistoryError;
  if (state.systemModal === "webserver-logs") {
    await refreshWebServerLogHistory();
    if (clearError) {
      state.webServerLogHistoryError = clearError;
      render();
    }
  }
  return cleared;
}

export function resetWebServerLogRecoveryState() {
  const scrollState = captureWebServerLogScrollState();
  closeWebServerLogStream();
  updateWebServerLogState({ webServerLogEnabled: null, webServerLogConnected: false, webServerLogCsrfToken: "" });
  clearWebServerLogOutput();
  if (state.systemModal === "webserver-logs") {
    void refreshWebServerLogHistory({ scrollState });
  }
}

export function syncWebServerLogStream() {
  if (isWebServerLogDemoMode()) {
    closeWebServerLogStream();
    return;
  }

  const shouldConnect = state.mounted && !state.nativeOpen && state.systemModal === "webserver-logs" &&
    state.busyAction !== "clear-webserver-log-history";
  if (!shouldConnect) {
    closeWebServerLogStream();
    return;
  }

  if (state.webServerLogEnabled === false) {
    closeWebServerLogStream();
    return;
  }

  if (state.webServerLogSource) {
    return;
  }

  openWebServerLogStream();
}

export function openWebServerLogStream() {
  if (isWebServerLogDemoMode()) {
    state.webServerLogEnabled = true;
    state.webServerLogConnected = false;
    state.webServerLogError = "";
    render();
    return;
  }

  if (typeof window.EventSource !== "function") {
    state.webServerLogEnabled = false;
    state.webServerLogConnected = false;
    state.webServerLogError = "Deze browser ondersteunt geen live logstream.";
    render();
    return;
  }

  try {
    const source = new window.EventSource(getWebServerLogUrl());
    state.webServerLogSource = source;
    source.addEventListener("open", handleWebServerLogOpen);
    source.addEventListener("ping", handleWebServerLogPing);
    source.addEventListener("log", handleWebServerLogMessage);
    source.onerror = handleWebServerLogError;
  } catch (error) {
    state.webServerLogEnabled = false;
    state.webServerLogConnected = false;
    state.webServerLogError = error instanceof Error ? error.message : "De live logstream kon niet worden geopend.";
    closeWebServerLogStream();
    render();
  }
}

export function closeWebServerLogStream() {
  const source = state.webServerLogSource;
  if (source) {
    try {
      source.close();
    } catch (_error) {
      // Ignore close failures when the stream already stopped.
    }
  }
  state.webServerLogSource = null;
  state.webServerLogConnected = false;
}

export function handleWebServerLogOpen() {
  if (!state.webServerLogSource || state.nativeOpen) {
    return;
  }

  const scrollState = state.systemModal === "webserver-logs"
    ? captureWebServerLogScrollState()
    : null;
  state.webServerLogEnabled = true;
  state.webServerLogConnected = true;
  state.webServerLogError = "";
  render();
  queueWebServerLogScrollRestore(scrollState);
}

setWebServerLogControls({
  clearOutput: clearWebServerLogOutput,
  closeStream: closeWebServerLogStream,
  resetRecoveryState: resetWebServerLogRecoveryState,
});

export function handleWebServerLogPing() {
  if (!state.webServerLogSource || state.nativeOpen) {
    return;
  }

  const scrollState = state.systemModal === "webserver-logs"
    ? captureWebServerLogScrollState()
    : null;
  state.webServerLogEnabled = true;
  if (!state.webServerLogConnected) {
    state.webServerLogConnected = true;
    state.webServerLogError = "";
    render();
    queueWebServerLogScrollRestore(scrollState);
  }
}

export function handleWebServerLogError() {
  if (!state.webServerLogSource) {
    return;
  }

  const scrollState = state.systemModal === "webserver-logs"
    ? captureWebServerLogScrollState()
    : null;
  state.webServerLogEnabled = false;
  state.webServerLogConnected = false;
  state.webServerLogError = "De live logstream kon niet worden geopend.";
  closeWebServerLogStream();
  render();
  queueWebServerLogScrollRestore(scrollState);
}

export function handleWebServerLogMessage(event) {
  if (!state.webServerLogSource || !event || typeof event.data !== "string") {
    return;
  }

  const scrollState = captureWebServerLogScrollState();
  const payload = normalizeWebServerLogPayload(event.data);
  if (!payload) {
    return;
  }

  const lines = payload.split(/\r?\n/).filter((line) => line.trim() !== "");
  if (lines.length === 0) {
    return;
  }

  const entries = lines.map((line) => createWebServerLogEntry(line));
  const filteredEntries = entries.filter((entry) => !shouldIgnoreLiveWebServerLogEntry(entry) && !hasWebServerLogEntry(entry));
  if (filteredEntries.length === 0) {
    return;
  }

  mergeWebServerLogEntries(filteredEntries);

  const output = getWebServerLogOutputElement();
  const scroller = getWebServerLogScrollerElement();

  trimWebServerLogEntries(output);
  appendWebServerLogEntriesToDom(filteredEntries, output);

  state.webServerLogEnabled = true;
  if (scroller && scrollState) {
    queueWebServerLogScrollRestore(scrollState, false);
  }
}

export function normalizeWebServerLogPayload(raw) {
  const text = String(raw ?? "").replace(/\r\n/g, "\n").replace(/\r/g, "\n").trimEnd();
  if (!text) {
    return "";
  }

  const trimmed = text.trim();
  if ((trimmed.startsWith("{") && trimmed.endsWith("}")) || (trimmed.startsWith("[") && trimmed.endsWith("]"))) {
    try {
      const parsed = JSON.parse(trimmed);
      const candidate = typeof parsed === "string"
        ? parsed
        : parsed?.message
        ?? parsed?.msg
        ?? parsed?.text
        ?? parsed?.data
        ?? parsed?.payload
        ?? "";
      if (typeof candidate === "string" && candidate.trim()) {
        return candidate.replace(/\r\n/g, "\n").replace(/\r/g, "\n").trimEnd();
      }
    } catch (_error) {
      // Ignore JSON parse failures and fall back to the raw text payload.
    }
  }

  return text;
}

export function stripAnsiSequences(value) {
  return String(value ?? "").replace(/\x1b\[[0-9;]*m/g, "");
}

export function getWebServerLogTone(value) {
  const raw = String(value ?? "");
  const ansiMatches = Array.from(raw.matchAll(/\x1b\[([0-9;]*)m/g));
  for (let index = ansiMatches.length - 1; index >= 0; index -= 1) {
    const codes = ansiMatches[index][1]
      .split(";")
      .map((item) => Number(item))
      .filter((item) => Number.isFinite(item));
    for (let codeIndex = codes.length - 1; codeIndex >= 0; codeIndex -= 1) {
      const code = codes[codeIndex];
      if (code === 31 || code === 91) {
        return "error";
      }
      if (code === 33 || code === 93) {
        return "warning";
      }
      if (code === 32 || code === 92) {
        return "info";
      }
      if (code === 36 || code === 96 || code === 34 || code === 35) {
        return "debug";
      }
      if (code === 37 || code === 90 || code === 38 || code === 97) {
        return "verbose";
      }
    }
  }

  const severityMatch = raw.match(/\[(E|W|I|D|V|VV)\]/i);
  if (!severityMatch) {
    return "plain";
  }

  const severity = severityMatch[1].toUpperCase();
  if (severity === "E") {
    return "error";
  }
  if (severity === "W") {
    return "warning";
  }
  if (severity === "I") {
    return "info";
  }
  if (severity === "D") {
    return "debug";
  }
  return "verbose";
}

export function trimWebServerLogEntries(output) {
  while (state.webServerLogEntries.length > WEB_SERVER_LOG_MAX_ENTRIES) {
    state.webServerLogEntries.shift();
    if (output && output.firstElementChild) {
      output.removeChild(output.firstElementChild);
    }
  }
}

export function getWebServerLogOutputElement() {
  if (!state.root) {
    return null;
  }
  return state.root.querySelector("[data-oq-webserver-log-output]");
}

export function getWebServerLogScrollerElement() {
  if (!state.root) {
    return null;
  }
  return state.root.querySelector("[data-oq-webserver-log-scroller]");
}

export function appendWebServerLogEntriesToDom(entries, output) {
  if (!output || entries.length === 0) {
    return;
  }

  if (output.dataset.webServerLogEmpty === "true") {
    output.dataset.webServerLogEmpty = "false";
    output.innerHTML = "";
  }

  for (const entry of entries) {
    output.insertAdjacentHTML("beforeend", renderWebServerLogEntry(entry));
  }
}

export function renderWebServerLogEntry(entry) {
  const timestamp = formatWebServerLogDateTime(entry.receivedAt);
  const fullTimestamp = getWebServerLogTimeTooltip(entry.receivedAt);
  return `
    <div class="oq-webserver-log-entry oq-webserver-log-entry--${escapeHtml(entry.tone)}">
      <time class="oq-webserver-log-entry-time" datetime="${escapeHtml(new Date(Number(entry.receivedAt) || Date.now()).toISOString())}" title="${escapeHtml(fullTimestamp)}">${escapeHtml(timestamp)}</time>
      <span class="oq-webserver-log-entry-text">${escapeHtml(entry.text || entry.raw || " ")}</span>
    </div>
  `;
}

export function renderWebServerLogEntries(entries = state.webServerLogEntries) {
  if (!entries.length) {
    return `
      <p class="oq-webserver-log-empty">Nog geen logregels ontvangen. Open de log en wacht op een nieuwe melding.</p>
    `;
  }

  return entries.map((entry) => renderWebServerLogEntry(entry)).join("");
}

export function renderWebServerLogStatusBanner() {
  const rows = [];
  if (state.webServerLogHistoryLoading) {
    rows.push(`<p class="oq-helper-modal-note">Recente firmwarelogs worden opgehaald...</p>`);
  }
  if (state.webServerLogCopyMessage) {
    rows.push(`
      <div class="oq-helper-modal-success oq-helper-modal-success--compact" aria-live="polite">
        <strong>Kopiëren</strong>
        <span>${escapeHtml(state.webServerLogCopyMessage)}</span>
      </div>
    `);
  }
  if (state.webServerLogCopyError) {
    rows.push(`<p class="oq-helper-modal-note oq-helper-modal-note--error">${escapeHtml(state.webServerLogCopyError)}</p>`);
  }
  if (state.webServerLogHistoryError) {
    rows.push(`<p class="oq-helper-modal-note oq-helper-modal-note--error">${escapeHtml(state.webServerLogHistoryError)}</p>`);
  }
  if (state.webServerLogError) {
    rows.push(`<p class="oq-helper-modal-note oq-helper-modal-note--error">${escapeHtml(state.webServerLogError)}</p>`);
  }

  if (!rows.length) {
    return "";
  }

  return rows.join("");
}

export function renderWebServerLogHistoryControls() {
  return `
    <div class="oq-webserver-log-history-shell">
      ${renderWebServerLoggerLevelControl()}
    </div>
  `;
}

export function renderWebServerLoggerLevelControl() {
  const entity = getWebServerLoggerLevelEntity();
  if (!entity) {
    return "";
  }

  const options = getWebServerLoggerLevelOptions(entity);
  const value = getWebServerLoggerLevelValue(entity);
  const busy = state.loadingEntities || Boolean(state.busyAction);
  const warning = value === "DEBUG"
    ? "DEBUG kan de web-app en Home Assistant vertragen."
    : "";

  return `
    ${renderWebServerLogControlCard({
      dataValue: "debugLevel",
      label: "Logger level",
      value: value || "Onbekend",
      infoId: "webserverLoggerLevel",
      infoCopy: "DEBUG is tijdelijk en wordt na een herstart teruggezet naar INFO. Bij veel Modbusverkeer kan DEBUG zoveel logging produceren dat de web-app en Home Assistant traag of onbereikbaar worden.",
      note: warning,
      action: `<label class="oq-webserver-log-level-control" aria-label="Logger level">
        <select class="oq-helper-select" data-oq-field="debugLevel" ${busy ? "disabled" : ""}>
          ${options.map((option) => `<option value="${escapeHtml(option)}" ${option === value ? "selected" : ""}>${escapeHtml(option)}</option>`).join("")}
        </select>
        <span class="oq-settings-select-caret" aria-hidden="true"></span>
      </label>`,
    })}
  `;
}

export function buildWebServerLogCopyText() {
  return state.webServerLogEntries
    .map((entry) => {
      const line = String(entry.text ?? stripAnsiSequences(entry.raw ?? "")).trimEnd();
      if (!line.trim()) {
        return "";
      }
      return `${formatWebServerLogDateTime(entry.receivedAt)} ${line}`;
    })
    .filter((entry) => entry.trim() !== "")
    .join("\n");
}

export async function copyWebServerLogOutput() {
  const text = buildWebServerLogCopyText();
  state.webServerLogCopyMessage = "";
  state.webServerLogCopyError = "";

  if (!text) {
    state.webServerLogCopyError = "Er zijn nog geen logregels om te kopiëren.";
    render();
    return;
  }

  try {
    const copied = await copyTextToClipboard(text);
    if (!copied) {
      throw new Error("Kopiëren naar het klembord is niet gelukt.");
    }
    state.webServerLogCopyMessage = `${state.webServerLogEntries.length} logregel${state.webServerLogEntries.length === 1 ? "" : "s"} gekopieerd.`;
  } catch (error) {
    state.webServerLogCopyError = error instanceof Error ? error.message : "Kopiëren naar het klembord is niet gelukt.";
  }

  if (state.systemModal === "webserver-logs") {
    render();
  }
}

const webServerLogActionHandlers = {
  "open-webserver-log-modal": () => openWebServerLogsModal(),
  "clear-webserver-log-output": () => clearWebServerLogHistory(),
  "copy-webserver-log-output": () => copyWebServerLogOutput(),
};

export function handleWebServerLogAction(action) {
  return invokeActionMap(webServerLogActionHandlers, action);
}

export function renderWebServerLogsModal() {
  const demoMode = isWebServerLogDemoMode();
  const clearBusy = state.busyAction === "clear-webserver-log-history";
  const clearDisabled = Boolean(state.busyAction) || state.webServerLogHistoryLoading ||
    (!demoMode && !state.nativeOpen && !state.webServerLogCsrfToken);
  return renderModalShell({
    id: "system",
    titleId: "oq-webserver-log-modal-title",
    kicker: "Diagnostiek",
    title: "OpenQuatt log",
    copy: demoMode
      ? "Hier zie je voorbeeldmeldingen uit de lokale preview."
      : "Hier zie je recente meldingen van OpenQuatt. Handig als je wilt terugzoeken wat er net gebeurde.",
    className: "oq-helper-modal--wide oq-helper-modal--scrollable oq-webserver-log-modal",
    closeAction: "close-system-modal",
    closeLabel: "Sluit logboek",
    body: `
        ${renderWebServerLogHistoryControls()}
        ${renderWebServerLogStatusBanner()}
        <div class="oq-webserver-log-panel" data-oq-webserver-log-scroller>
          <div class="oq-webserver-log-output" data-oq-webserver-log-output data-web-server-log-empty="${state.webServerLogEntries.length === 0 ? "true" : "false"}">
            ${renderWebServerLogEntries()}
          </div>
        </div>`,
    actions: `
      <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="copy-webserver-log-output" ${state.webServerLogEntries.length === 0 ? "disabled" : ""}>Kopieer log</button>
      <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="clear-webserver-log-output" ${clearDisabled ? "disabled" : ""}>${clearBusy ? "Legen..." : "Legen"}</button>
      <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">Gereed</button>
    `,
  });
}

function renderWebServerLogControlCard({
  dataValue,
  label,
  value,
  infoId,
  infoCopy,
  action,
  note = "",
}) {
  return `
    <div
      class="oq-settings-system-row oq-settings-system-row--with-action oq-webserver-log-control-card${note ? " oq-webserver-log-control-card--warning" : ""}"
      data-oq-diagnostics-row="${escapeHtml(dataValue)}"
    >
      <div class="oq-settings-system-row-copy">
        <p class="oq-settings-system-row-label">${escapeHtml(label)}</p>
        <div class="oq-webserver-log-control-card-value">
          <strong class="oq-settings-system-row-value">${escapeHtml(value)}</strong>
          ${renderSettingsInfoToggle(infoId, label, infoCopy)}
        </div>
        ${note ? `<p class="oq-settings-system-row-note">${escapeHtml(note)}</p>` : ""}
      </div>
      ${action}
    </div>
  `;
}
