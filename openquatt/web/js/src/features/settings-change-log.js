import { fetchWithTimeout } from "../core/browser-utils.js";
import { ENTITY_DEFS } from "../core/config.js";
import { formatValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { formatDateTime, optionLabel, t } from "../i18n/index.js";
import { renderSettingsSection } from "../settings/controls.js";

export const CHANGE_LOG_GROUP_ID = "changes";
const CHANGE_LOG_PATH = "/openquatt/change-log";
const REQUEST_TIMEOUT_MS = 8000;
// The firmware stores fixed 80-byte records whose label, from and to share 65 bytes of UTF-8 text.
// Text is shortened here, values first, so the device can reject anything longer instead of cutting it.
export const CHANGE_LOG_TEXT_LIMITS = { text: 65, value: 20 };

const changeLog = { entries: [], status: "idle", requestId: 0 };
const encoder = new TextEncoder();

function utf8Length(text) {
  return encoder.encode(text).length;
}

export function clipUtf8(text, maxBytes) {
  const value = String(text ?? "").trim();
  if (utf8Length(value) <= maxBytes) {
    return value;
  }
  let clipped = "";
  for (const character of value) {
    if (utf8Length(`${clipped}${character}…`) > maxBytes) {
      break;
    }
    clipped += character;
  }
  return `${clipped.trimEnd()}…`;
}

export function formatChangeLogValue(key, value) {
  if (value === undefined || value === null || value === "") {
    return "—";
  }
  const domain = ENTITY_DEFS[key]?.domain;
  if (domain === "number") {
    return formatValue(key, value);
  }
  if (domain === "select") {
    return optionLabel(value) || String(value);
  }
  if (domain === "switch") {
    return t(value === true || value === "true" || value === "ON" ? "common.on" : "common.off");
  }
  return String(value);
}

function normalizeEntries(rawEntries) {
  return (Array.isArray(rawEntries) ? rawEntries : [])
    .map((entry) => ({
      seq: Number(entry?.seq),
      at: Number(entry?.at),
      label: String(entry?.label || ""),
      from: String(entry?.from || ""),
      to: String(entry?.to || ""),
    }))
    .filter((entry) => Number.isFinite(entry.seq) && entry.label)
    .sort((left, right) => right.seq - left.seq);
}

export function getSettingsChangeLogEntries() {
  return changeLog.entries;
}

export async function refreshSettingsChangeLog() {
  const requestId = changeLog.requestId + 1;
  changeLog.requestId = requestId;
  if (!changeLog.entries.length) {
    const firstLoad = changeLog.status !== "loading";
    changeLog.status = "loading";
    if (firstLoad) render();
  }
  let nextStatus = "error";
  try {
    const response = await fetchWithTimeout(`${getBasePath()}${CHANGE_LOG_PATH}`, { cache: "no-store" }, REQUEST_TIMEOUT_MS);
    if (response.status === 404) {
      nextStatus = "unsupported";
    } else if (response.ok) {
      const payload = await response.json();
      if (requestId !== changeLog.requestId) return;
      changeLog.entries = normalizeEntries(payload?.entries);
      nextStatus = "ready";
    }
  } catch (_error) {
    nextStatus = "error";
  }
  if (requestId !== changeLog.requestId) return;
  changeLog.status = nextStatus;
  render();
}

// A setting that was written successfully stays successful when its log entry cannot be stored.
export function prepareChangeLogEntry(label, from, to) {
  const fromText = clipUtf8(from, CHANGE_LOG_TEXT_LIMITS.value);
  const toText = clipUtf8(to, CHANGE_LOG_TEXT_LIMITS.value);
  const labelBudget = CHANGE_LOG_TEXT_LIMITS.text - utf8Length(fromText) - utf8Length(toText);
  return { label: clipUtf8(label, labelBudget), from: fromText, to: toText };
}

export async function recordSettingsChange(label, from, to) {
  const entry = prepareChangeLogEntry(label, from, to);
  // Compared before shortening: two different long values may clip to the same text.
  if (!entry.label || String(from ?? "").trim() === String(to ?? "").trim()) {
    return false;
  }
  try {
    const statusResponse = await fetchWithTimeout(`${getBasePath()}/auth/status`, { cache: "no-store" }, REQUEST_TIMEOUT_MS);
    if (!statusResponse.ok) return false;
    const csrfToken = String((await statusResponse.json())?.csrf_token || "");
    if (!csrfToken) return false;
    const body = new URLSearchParams({ csrf_token: csrfToken, at: String(Math.floor(Date.now() / 1000)), ...entry });
    const response = await fetchWithTimeout(`${getBasePath()}${CHANGE_LOG_PATH}`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body,
    }, REQUEST_TIMEOUT_MS);
    if (!response.ok) return false;
  } catch (_error) {
    return false;
  }
  if (state.appView === "settings" && state.settingsGroup === CHANGE_LOG_GROUP_ID) {
    void refreshSettingsChangeLog();
  }
  return true;
}

function renderChangeLogBody() {
  const entries = changeLog.entries;
  if (!entries.length) {
    const messageKey = changeLog.status === "unsupported"
      ? "settingsChangeLog.unsupported"
      : changeLog.status === "error"
        ? "settingsChangeLog.error"
        : changeLog.status === "ready"
          ? "settingsChangeLog.empty"
          : "settingsChangeLog.loading";
    return `<p class="oq-settings-change-log-empty">${escapeHtml(t(messageKey))}</p>`;
  }
  return `
    <div class="oq-settings-change-log-table-wrap">
      <table class="oq-settings-change-log-table">
        <thead>
          <tr>
            <th scope="col">${escapeHtml(t("settingsChangeLog.columnTime"))}</th>
            <th scope="col">${escapeHtml(t("settingsChangeLog.columnSetting"))}</th>
            <th scope="col">${escapeHtml(t("settingsChangeLog.columnFrom"))}</th>
            <th scope="col">${escapeHtml(t("settingsChangeLog.columnTo"))}</th>
          </tr>
        </thead>
        <tbody>
          ${entries.map((entry) => `
            <tr>
              <td>${escapeHtml(entry.at > 0 ? formatDateTime(entry.at * 1000, { dateStyle: "medium", timeStyle: "short" }) : "—")}</td>
              <td>${escapeHtml(entry.label)}</td>
              <td>${escapeHtml(entry.from)}</td>
              <td>${escapeHtml(entry.to)}</td>
            </tr>
          `).join("")}
        </tbody>
      </table>
    </div>
    ${changeLog.status === "error" ? `<p class="oq-settings-change-log-empty">${escapeHtml(t("settingsChangeLog.error"))}</p>` : ""}
  `;
}

export function renderSettingsChangeLogSection() {
  // Opening the group through the URL or the remembered group loads the log on first render.
  if (changeLog.status === "idle") {
    changeLog.status = "loading";
    queueMicrotask(() => void refreshSettingsChangeLog());
  }
  return renderSettingsSection(
    t("settingsChangeLog.kicker"),
    t("settingsChangeLog.title"),
    t("settingsChangeLog.copy"),
    renderChangeLogBody(),
    "",
    "oq-settings-change-log",
    `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="refresh-settings-change-log"${changeLog.status === "loading" ? " disabled" : ""}>${escapeHtml(t("settingsChangeLog.refresh"))}</button>`,
  );
}
