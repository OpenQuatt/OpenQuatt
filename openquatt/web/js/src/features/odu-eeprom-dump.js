import { hasEntity } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { downloadJsonFile, fetchWithTimeout } from "../core/browser-utils.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { getInstallationTopology } from "./device-context.js";
import { formatNumber, t } from "../i18n/index.js";

const STATUS_REFRESH_INTERVAL_MS = 5000;
const ACTIVE_POLL_INTERVAL_MS = 1000;
const REQUEST_TIMEOUT_MS = 8000;

export function getOduEepromDumpEndpoint(hp, action) {
  const hpIndex = Number(hp) === 2 ? 2 : 1;
  return `${getBasePath()}/openquatt/odu-eeprom/hp${hpIndex}/${action}`;
}

export function normalizeOduEepromDumpStatus(payload = {}, hp = 1) {
  const progress = Math.max(0, Math.min(100, Number(payload.progress_percent || 0)));
  return {
    ok: payload.ok !== false,
    available: payload.available !== false,
    unsupported: payload.unsupported === true,
    hp: Number(payload.hp || hp) === 2 ? 2 : 1,
    active: payload.active === true,
    dumpReady: payload.dump_ready === true,
    jobId: Math.max(0, Number(payload.job_id || 0)),
    phase: String(payload.phase || "idle"),
    progress,
    registersRead: Math.max(0, Number(payload.registers_read || 0)),
    registerCount: Math.max(0, Number(payload.register_count || 512)),
    warningFlags: Math.max(0, Number(payload.warning_flags || 0)),
    error: String(payload.error || ""),
    crc: {
      calculated: String(payload.crc?.calculated || "0x0000"),
      stored: String(payload.crc?.stored || "0x0000"),
      matchesStoredEeprom: payload.crc?.matches_stored_eeprom === true,
      retryCount: Math.max(0, Number(payload.crc?.retry_count || 0)),
    },
    identity: {
      extendedSupported: payload.identity?.extended_supported === true,
      model: String(payload.identity?.model || ""),
      coreAvailable: payload.identity?.core_available === true,
      pcbProgramRaw: Math.max(0, Number(payload.identity?.pcb_program_raw || 0)),
      pcbProgram: String(payload.identity?.pcb_program || ""),
      eepromProgramRaw: Math.max(0, Number(payload.identity?.eeprom_program_raw || 0)),
    },
  };
}

export function getOduEepromDumpHpIndexes() {
  return getInstallationTopology() === "duo" || hasEntity("hp2ExcludeMinHz") ? [1, 2] : [1];
}

export function getOduEepromCrcLabel(status) {
  if (!status?.dumpReady) return "";
  return status.crc.matchesStoredEeprom
    ? t("oduEeprom.crcMatch", { crc: status.crc.calculated })
    : t("oduEeprom.crcMismatch", { runtime: status.crc.calculated, stored: status.crc.stored });
}

function clearPollTimer() {
  if (state.oduEepromDumpPollTimer) {
    window.clearTimeout(state.oduEepromDumpPollTimer);
    state.oduEepromDumpPollTimer = null;
  }
}

function schedulePoll() {
  clearPollTimer();
  const statuses = Object.values(state.oduEepromDumpStatuses || {});
  if (!shouldRefreshOduEepromDumpSurface() || !statuses.some((status) => status?.active)) {
    return;
  }
  state.oduEepromDumpPollTimer = window.setTimeout(() => {
    if (!shouldRefreshOduEepromDumpSurface()) {
      clearPollTimer();
      return;
    }
    void refreshOduEepromDumpStatuses({ force: true, silent: true });
  }, ACTIVE_POLL_INTERVAL_MS);
}

async function fetchStatus(hp) {
  const response = await fetchWithTimeout(
    getOduEepromDumpEndpoint(hp, "status"),
    { cache: "no-store", headers: { "Cache-Control": "no-store" } },
    REQUEST_TIMEOUT_MS,
    t("oduEeprom.statusTimeout", { hp }),
  );
  if (response.status === 404) {
    return normalizeOduEepromDumpStatus({ available: false, unsupported: true, hp }, hp);
  }
  if (!response.ok) {
    throw new Error(`HP${hp} status HTTP ${response.status}`);
  }
  return normalizeOduEepromDumpStatus(await response.json(), hp);
}

export function shouldRefreshOduEepromDumpSurface() {
  return state.appView === "settings" && state.settingsGroup === "service" && state.systemModal === "odu-eeprom-dump";
}

export async function refreshOduEepromDumpStatuses(options = {}) {
  if (!shouldRefreshOduEepromDumpSurface() && options.force !== true) {
    return false;
  }
  if (state.oduEepromDumpFetchPromise) {
    return state.oduEepromDumpFetchPromise;
  }
  const now = Date.now();
  if (!options.force && now - Number(state.oduEepromDumpLastFetchAt || 0) < STATUS_REFRESH_INTERVAL_MS) {
    return false;
  }

  const previousSignature = JSON.stringify(state.oduEepromDumpStatuses || {});
  state.oduEepromDumpFetchPromise = (async () => {
    try {
      const entries = await Promise.all(getOduEepromDumpHpIndexes().map(async (hp) => [hp, await fetchStatus(hp)]));
      state.oduEepromDumpStatuses = Object.fromEntries(entries);
      state.oduEepromDumpLastFetchAt = Date.now();
      state.oduEepromDumpError = "";
      schedulePoll();
      const changed = JSON.stringify(state.oduEepromDumpStatuses) !== previousSignature;
      if (changed && shouldRefreshOduEepromDumpSurface()) syncOduEepromDumpModal();
      return changed;
    } catch (error) {
      state.oduEepromDumpError = t("oduEeprom.statusFetchFail", { error: error.message || String(error) });
      if (!options.silent) syncOduEepromDumpModal();
      return false;
    } finally {
      state.oduEepromDumpFetchPromise = null;
    }
  })();
  return state.oduEepromDumpFetchPromise;
}

async function startDump(button) {
  const hp = Number(button.dataset.hp) === 2 ? 2 : 1;
  state.oduEepromDumpBusyHp = hp;
  state.oduEepromDumpError = "";
  state.oduEepromDumpNotice = "";
  syncOduEepromDumpModal();
  try {
    const response = await fetchWithTimeout(
      getOduEepromDumpEndpoint(hp, "start?extended=1"),
      { method: "POST", cache: "no-store", headers: { "Cache-Control": "no-store" } },
      REQUEST_TIMEOUT_MS,
      t("oduEeprom.dumpStartTimeout", { hp }),
    );
    if (!response.ok) {
      const payload = await response.json().catch(() => ({}));
      throw new Error(payload.error === "dump_busy" ? t("oduEeprom.dumpBusy") : `HTTP ${response.status}`);
    }
    const status = normalizeOduEepromDumpStatus(await response.json(), hp);
    state.oduEepromDumpStatuses = { ...(state.oduEepromDumpStatuses || {}), [hp]: status };
    state.oduEepromDumpNotice = t("oduEeprom.dumpStarted", { hp });
    schedulePoll();
  } catch (error) {
    state.oduEepromDumpError = t("oduEeprom.dumpFailed", { hp, error: error.message || String(error) });
  } finally {
    state.oduEepromDumpBusyHp = 0;
    syncOduEepromDumpModal();
  }
}

function getDownloadFilename(payload, hp) {
  const model = String(payload?.identity?.model || "odu")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "") || "odu";
  const stamp = new Date().toISOString().replace(/[:.]/g, "-");
  return `openquatt-hp${hp}-${model}-eeprom-${stamp}.json`;
}

async function downloadDump(button) {
  const hp = Number(button.dataset.hp) === 2 ? 2 : 1;
  state.oduEepromDumpBusyHp = hp;
  state.oduEepromDumpError = "";
  state.oduEepromDumpNotice = "";
  syncOduEepromDumpModal();
  try {
    const response = await fetchWithTimeout(
      getOduEepromDumpEndpoint(hp, "download"),
      { cache: "no-store", headers: { "Cache-Control": "no-store" } },
      20000,
      t("oduEeprom.dumpDownloadTimeout", { hp }),
    );
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const payload = await response.json();
    downloadJsonFile(getDownloadFilename(payload, hp), payload);
    state.oduEepromDumpNotice = t("oduEeprom.dumpDownloaded", { hp });
  } catch (error) {
    state.oduEepromDumpError = t("oduEeprom.dumpDownloadFailed", { hp, error: error.message || String(error) });
  } finally {
    state.oduEepromDumpBusyHp = 0;
    syncOduEepromDumpModal();
  }
}

function translatePhase(phase) {
  const normalized = String(phase || "").toLowerCase();
  if (normalized === "complete") return t("oduEeprom.phaseDone");
  if (normalized === "complete with warnings") return t("oduEeprom.phaseDoneWarnings");
  if (normalized === "failed") return t("oduEeprom.phaseFailed");
  if (normalized.includes("waiting")) return t("oduEeprom.phaseWaiting");
  if (normalized.includes("extended")) return t("oduEeprom.phaseExtended");
  if (normalized.includes("core")) return t("oduEeprom.phaseCore");
  if (normalized.includes("reread") || normalized.includes("runtime crc differs")) return t("oduEeprom.phaseReread");
  if (normalized.includes("eeprom")) return t("oduEeprom.phaseEeprom");
  if (normalized.includes("verifying")) return t("oduEeprom.phaseVerifying");
  return normalized === "idle" ? t("oduEeprom.phaseIdle") : phase;
}

function renderHpPanel(hp) {
  const status = state.oduEepromDumpStatuses?.[hp] || null;
  const busy = Number(state.oduEepromDumpBusyHp || 0) === hp;
  const active = status?.active === true;
  const ready = status?.dumpReady === true;
  const unavailable = status?.available === false;
  const unsupported = status?.unsupported === true;
  const progress = active ? status.progress : ready ? 100 : 0;
  const statusLabel = unsupported
    ? t("oduEeprom.statusUnsupported")
    : unavailable
      ? t("oduEeprom.statusNoPsram")
      : status?.error
        ? t("oduEeprom.statusFailed", { error: status.error })
        : status
          ? ready && status.warningFlags === 4
            ? t("oduEeprom.statusChanged")
            : translatePhase(status.phase)
          : t("oduEeprom.statusNotFetched");
  const crcLabel = getOduEepromCrcLabel(status);
  const identityParts = [];
  if (status?.identity?.model) identityParts.push(status.identity.model);
  if (status?.identity?.pcbProgram) identityParts.push(`PCB ${status.identity.pcbProgram}`);
  if (status?.identity?.eepromProgramRaw) identityParts.push(`EEPROM ${status.identity.eepromProgramRaw}`);

  return `
    <article class="oq-odu-eeprom-device${active ? " is-active" : ""}${ready ? " is-ready" : ""}">
      <div class="oq-odu-eeprom-device-head">
        <div>
          <span class="oq-helper-label">HP${hp}</span>
          <h4>${escapeHtml(t("oduEeprom.panelTitle"))}</h4>
          <p>${escapeHtml(identityParts.join(" · ") || t("oduEeprom.panelCopyFallback"))}</p>
        </div>
        <span class="oq-odu-eeprom-state${ready && !status?.crc?.matchesStoredEeprom ? " is-warning" : ""}">${escapeHtml(statusLabel)}</span>
      </div>
      <div class="oq-odu-eeprom-progress" role="progressbar" aria-label="${escapeHtml(t("oduEeprom.progressAria", { hp }))}" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${progress}">
        <span style="width:${progress}%"></span>
      </div>
      <div class="oq-odu-eeprom-meta">
        <span>${active ? `${formatNumber(status.registersRead, { maximumFractionDigits: 0 })}/${formatNumber(status.registerCount, { maximumFractionDigits: 0 })} EEPROM-registers` : ready ? t("oduEeprom.progressFull") : t("oduEeprom.progressRange")}</span>
        ${crcLabel ? `<strong class="${status.crc.matchesStoredEeprom ? "" : "is-warning"}">${escapeHtml(crcLabel)}</strong>` : ""}
      </div>
      <div class="oq-odu-eeprom-actions">
        <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="start-odu-eeprom-dump" data-hp="${hp}" ${busy || active || unavailable ? "disabled" : ""}>${active ? escapeHtml(t("oduEeprom.startBusy")) : ready ? escapeHtml(t("oduEeprom.startAgain")) : escapeHtml(t("oduEeprom.startAction"))}</button>
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="download-odu-eeprom-dump" data-hp="${hp}" ${busy || !ready ? "disabled" : ""}>${escapeHtml(t("oduEeprom.downloadAction"))}</button>
      </div>
    </article>
  `;
}

export function renderOduEepromDumpPanel() {
  const feedback = state.oduEepromDumpError
    ? `<p class="oq-odu-eeprom-feedback is-error" role="alert">${escapeHtml(state.oduEepromDumpError)}</p>`
    : state.oduEepromDumpNotice
      ? `<p class="oq-odu-eeprom-feedback" role="status">${escapeHtml(state.oduEepromDumpNotice)}</p>`
      : "";
  return `
    <div class="oq-odu-eeprom-shell" data-oq-odu-eeprom-panel>
      <div class="oq-odu-eeprom-callout">
        <strong>${escapeHtml(t("oduEeprom.calloutTitle"))}</strong>
        <p>${escapeHtml(t("oduEeprom.calloutCopy"))}</p>
      </div>
      ${feedback}
      <div class="oq-odu-eeprom-grid">
        ${getOduEepromDumpHpIndexes().map((hp) => renderHpPanel(hp)).join("")}
      </div>
    </div>
  `;
}

export function syncOduEepromDumpModal() {
  const current = state.root?.querySelector?.("[data-oq-odu-eeprom-panel]");
  if (!current || state.systemModal !== "odu-eeprom-dump") {
    return false;
  }

  const template = document.createElement("template");
  template.innerHTML = renderOduEepromDumpPanel().trim();
  const next = template.content.firstElementChild;
  if (!next) {
    return false;
  }
  const focusedButton = current.contains(document.activeElement) ? document.activeElement : null;
  const currentButtons = current.querySelectorAll("button");
  next.querySelectorAll("button").forEach((replacement, index) => {
    const button = currentButtons[index];
    if (!button) return;
    button.disabled = replacement.disabled;
    button.textContent = replacement.textContent;
    replacement.replaceWith(button);
  });
  current.replaceWith(next);
  if (focusedButton && !focusedButton.disabled) focusedButton.focus({ preventScroll: true });
  return true;
}

export function renderOduEepromDumpModal() {
  return renderModalShell({
    modalId: "system",
    titleId: "oq-odu-eeprom-dump-modal-title",
    kicker: t("oduEeprom.modalKicker"),
    title: t("oduEeprom.modalTitle"),
    copy: t("oduEeprom.modalCopy"),
    className: "oq-helper-modal--wide oq-helper-modal--scrollable oq-helper-modal--odu-eeprom",
    closeAction: "close-system-modal",
    closeLabel: t("oduEeprom.modalClose"),
    body: renderOduEepromDumpPanel(),
    actions: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal">${escapeHtml(t("common.close"))}</button>`,
  });
}

function openDumpModal() {
  state.systemModal = "odu-eeprom-dump";
  state.oduEepromDumpError = "";
  state.oduEepromDumpNotice = "";
  render();
  void refreshOduEepromDumpStatuses({ force: true });
}

const actionHandlers = {
  "open-odu-eeprom-dump-modal": () => openDumpModal(),
  "start-odu-eeprom-dump": (button) => startDump(button),
  "download-odu-eeprom-dump": (button) => downloadDump(button),
};

export function handleOduEepromDumpAction(action, button) {
  return invokeActionMap(actionHandlers, action, button);
}
