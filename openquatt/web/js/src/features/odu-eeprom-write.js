import { invokeActionMap } from "../core/action-router.js";
import { downloadJsonFile, fetchWithTimeout } from "../core/browser-utils.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { formatNumber, t } from "../i18n/index.js";
import { getOduEepromDumpHpIndexes } from "./odu-eeprom-dump.js";

let statuses = {};
const drafts = {};
let timer = null;
let fetchGeneration = 0;
let feedback = "";
let pendingHp = 0;
const RESULT_KEYS = {
  READY: "oduEepromWrite.ready", READING: "oduEepromWrite.reading", WRITING: "oduEepromWrite.writing",
  POWER_CYCLE_REQUIRED: "oduEepromWrite.powerCycle", BACKUP_READY: "oduEepromWrite.backupReady",
  PREPARED: "oduEepromWrite.prepared", VERIFIED: "oduEepromWrite.verified", NOT_PERSISTED: "oduEepromWrite.notPersisted",
  STORAGE_FAILED: "oduEepromWrite.storageFailed", STORAGE_UNAVAILABLE: "oduEepromWrite.unavailable",
  UNSUPPORTED: "oduEepromWrite.unsupported", IDENTITY_MISMATCH: "oduEepromWrite.identityMismatch",
  SAFETY_BLOCKED: "oduEepromWrite.safetyBlocked", INVALID_STATE: "oduEepromWrite.invalidState",
  INVALID_IMAGE: "oduEepromWrite.invalidImage", STALE: "oduEepromWrite.stale", CANCELLED: "oduEepromWrite.cancelled",
  READ_FAILED: "oduEepromWrite.readFailed", READBACK_FAILED: "oduEepromWrite.readbackFailed",
  WRITE_UNCERTAIN: "oduEepromWrite.uncertain", COMMIT_FAILED: "oduEepromWrite.commitFailed",
  COMMIT_NOT_IDLE: "oduEepromWrite.commitNotIdle", WRITER_ACTIVE: "oduEepromWrite.writerActive",
  RECOVERY_REQUIRED: "oduEepromWrite.recoveryRequired", OFFLINE: "oduEepromWrite.offline", TIMEOUT: "oduEepromWrite.uncertain",
};
const open = () => state.systemModal === "odu-eeprom-write";
const endpoint = (hp, action) => `${getBasePath()}/openquatt/odu-eeprom-write/hp${hp}/${action}`;
const draft = (hp) => drafts[hp] ||= { groups: 0, consent: false, cold: false };

export function normalizeEepromWriteStatus(payload = {}) {
  return {
    hp: payload.hp === 2 ? 2 : 1, available: payload.available === true, busy: payload.busy === true,
    baseline: payload.baseline === true, legacy: payload.legacy === true, powerCycleSafe: payload.power_cycle_safe === true, coldSeen: payload.cold_seen === true, holding: payload.holding === true,
    state: Number(payload.state || 0), generation: Number(payload.generation || 0),
    result: String(payload.result || "STORAGE_UNAVAILABLE"), csrf: String(payload.csrf_token || ""),
    detailsReady: payload.details_ready === true && Array.isArray(payload.diff) && payload.diff.length > 0 && payload.diff.every((row) => [row.address, row.stored, row.runtime, row.target].every(Number.isInteger)),
    diff: Array.isArray(payload.diff) ? payload.diff.filter((row) => [row.address, row.stored, row.runtime, row.target].every(Number.isInteger)) : [],
  };
}

export function eepromDiffLabel(row) {
  const { address } = row;
  if (address === 3236) return t("oduSettings.controlLabel");
  if (address === 3237) return t("oduSettings.limitDefrost");
  if (address === 3238) return t("oduSettings.hysteresis");
  if (address === 3275) return t("oduDefrost.modeLabel");
  let level, mode;
  if (address >= 3000 && address <= 3010) { level = address - 3000; mode = t("oduEepromWrite.cooling"); }
  else if (address >= 3011 && address <= 3021) { level = address - 3011; mode = t("oduEepromWrite.heating"); }
  else if (address >= 3050 && address <= 3059) { level = address - 3050 + 11; mode = t("oduEepromWrite.heating"); }
  else if (address >= 3060 && address <= 3069) { level = address - 3060 + 11; mode = t("oduEepromWrite.cooling"); }
  else return t("oduEepromWrite.register", { address });
  return t("oduEepromWrite.frequencyLevel", { mode, level });
}
export function eepromDiffValue(address, value) {
  if (address === 3237) return `${formatNumber(value - 30)} °C`;
  if (address === 3238) return `${formatNumber(value)} K`;
  return `${formatNumber(value)}${address < 3070 ? " Hz" : ""}`;
}
function schedule() {
  if (timer) window.clearTimeout(timer);
  timer = null;
  if (open()) timer = window.setTimeout(() => { timer = null; if (open()) void refresh(); }, 2000);
}
async function refresh() {
  const generation = ++fetchGeneration;
  try {
    const entries = await Promise.all(getOduEepromDumpHpIndexes().map(async (hp) => {
      const response = await fetchWithTimeout(endpoint(hp, "status"), { cache: "no-store" }, 8000, t("oduEepromWrite.requestFailed"));
      if (!response.ok) throw new Error("status");
      return [hp, normalizeEepromWriteStatus(await response.json())];
    }));
    if (generation !== fetchGeneration) return;
    const next = Object.fromEntries(entries);
    for (const [hp, status] of Object.entries(next)) {
      if (statuses[hp]?.generation !== status.generation || (status.state === 3 && !status.detailsReady)) { draft(hp).consent = false; draft(hp).cold = false; }
    }
    const changed = JSON.stringify(next) !== JSON.stringify(statuses);
    statuses = next;
    if (changed && open()) render();
  } catch {
    if (generation === fetchGeneration) { feedback = "oduEepromWrite.requestFailed"; if (open()) render(); }
  } finally { if (generation === fetchGeneration) schedule(); }
}
async function run(button) {
  const hp = Number(button.dataset.hp) === 2 ? 2 : 1;
  const action = button.dataset.eepromAction;
  const status = statuses[hp];
  const choice = draft(hp);
  if (!status || pendingHp || status.busy || (action === "commit" && (!choice.consent || status.state !== 3 || !status.detailsReady)) ||
      (["capture", "verify"].includes(action) && (!choice.cold || !status.coldSeen))) return;
  choice.consent = false;
  pendingHp = hp;
  feedback = "";
  ++fetchGeneration;
  if (timer) window.clearTimeout(timer);
  render();
  try {
    const response = await fetchWithTimeout(endpoint(hp, action), {
      method: "POST", cache: "no-store", headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ csrf_token: status.csrf, groups: String(choice.groups), generation: String(status.generation), cold_confirmed: String(choice.cold) }),
    }, 8000, t("oduEepromWrite.requestFailed"));
    if (!response.ok) throw new Error("action");
    statuses[hp] = normalizeEepromWriteStatus(await response.json());
    choice.consent = false;
    choice.cold = false;
  } catch { feedback = "oduEepromWrite.requestFailed"; }
  finally { pendingHp = 0; if (open()) render(); void refresh(); }
}
async function download(button) {
  const hp = Number(button.dataset.hp) === 2 ? 2 : 1;
  try {
    const response = await fetchWithTimeout(endpoint(hp, "backup"), { cache: "no-store" }, 10000, t("oduEepromWrite.requestFailed"));
    if (!response.ok) throw new Error("backup");
    const backup = await response.json();
    if (backup.format !== "openquatt-odu-eeprom-backup-v1" || backup.original?.length !== 512) throw new Error("format");
    downloadJsonFile(`openquatt-hp${hp}-eeprom-original.json`, backup);
  } catch { feedback = "oduEepromWrite.requestFailed"; if (open()) render(); }
}
export function updateEepromWriteDraft(input) {
  const hp = Number(input.dataset.oqEepromWriteHp) === 2 ? 2 : 1;
  const choice = draft(hp);
  const group = Number(input.dataset.eepromGroup || 0);
  if ([1, 2, 4].includes(group)) choice.groups = input.checked ? choice.groups | group : choice.groups & ~group;
  else if (input.dataset.eepromField === "cold") choice.cold = input.checked;
  else if (input.dataset.eepromField === "consent") choice.consent = input.checked;
  else return false;
  const panel = input.closest("[data-eeprom-write-panel]");
  const status = statuses[hp];
  const commit = panel?.querySelector('[data-eeprom-action="commit"]');
  if (commit) commit.disabled = !choice.consent || !status?.detailsReady || !!pendingHp || status?.busy;
  const prepare = panel?.querySelector('[data-eeprom-action="prepare"]');
  if (prepare) prepare.disabled = !choice.groups || !!pendingHp || status?.busy;
  const cold = panel?.querySelector('[data-eeprom-action="capture"], [data-eeprom-action="verify"]');
  if (cold) cold.disabled = !choice.cold || !status?.coldSeen || !!pendingHp || status?.busy;
  return true;
}
function actionButton(hp, action, key, disabled, primary = false) {
  return `<button type="button" class="oq-helper-button ${primary ? "oq-helper-button--primary" : "oq-helper-button--ghost"}" data-oq-action="odu-eeprom-write-run" data-eeprom-action="${action}" data-hp="${hp}" ${disabled ? "disabled" : ""}>${escapeHtml(t(key))}</button>`;
}
function renderHp(hp) {
  const status = statuses[hp];
  const choice = draft(hp);
  const busy = Boolean(pendingHp || status?.busy);
  const disabled = busy || !status?.available || (status.state === 3 && !status.detailsReady);
  const checked = (field, key) => `<label class="oq-settings-odu-auto"><input id="oq-eeprom-${hp}-${field}" type="checkbox" data-oq-eeprom-write-hp="${hp}" data-eeprom-field="${field}" ${choice[field] ? "checked" : ""} ${disabled ? "disabled" : ""}><span>${escapeHtml(t(key))}</span></label>`;
  let body = "";
  if (!status || !status.available) body = `<p>${escapeHtml(t("oduEepromWrite.unavailable"))}</p>`;
  else if (status.busy) body = `<p role="status">${escapeHtml(t("common.busy"))}</p>`;
  else if ([4, 5].includes(status.state) && !status.powerCycleSafe) {
    body = `<p role="status">${escapeHtml(t("oduEepromWrite.keepPowered"))}</p>
      <div class="oq-helper-modal-actions">${actionButton(hp, "check-writer", "oduEepromWrite.checkWriter", disabled, true)}</div>
      ${status.coldSeen ? `${checked("cold", "oduEepromWrite.coldConfirm")}<div class="oq-helper-modal-actions">${actionButton(hp, "verify", "oduEepromWrite.readAfterRestart", disabled || !choice.cold)}</div>` : ""}`;
  } else if (status.state === 1 || [4, 5, 6].includes(status.state)) {
    body = `<p>${escapeHtml(t("oduEepromWrite.coldInstructions"))}</p>${checked("cold", "oduEepromWrite.coldConfirm")}
      <div class="oq-helper-modal-actions">${actionButton(hp, status.state === 1 ? "capture" : "verify", "oduEepromWrite.readAfterRestart", disabled || !choice.cold || !status.coldSeen, true)}
      ${status.state === 1 ? actionButton(hp, "cancel", "common.cancel", disabled) : ""}</div>`;
  } else if (status.state === 3) {
    body = `<p>${escapeHtml(t("oduEepromWrite.reviewCopy"))}</p><div class="oq-odu-eeprom-diff"><table><thead><tr><th>${escapeHtml(t("oduEepromWrite.setting"))}</th><th>${escapeHtml(t("oduEepromWrite.stored"))}</th><th>${escapeHtml(t("oduEepromWrite.runtime"))}</th><th>${escapeHtml(t("oduEepromWrite.target"))}</th></tr></thead><tbody>
      ${status.diff.map((row) => `<tr><th scope="row">${escapeHtml(eepromDiffLabel(row))}</th>${[row.stored, row.runtime, row.target].map((value) => `<td>${escapeHtml(eepromDiffValue(row.address, value))}</td>`).join("")}</tr>`).join("")}</tbody></table></div>
      ${checked("consent", "oduEepromWrite.consent")}<div class="oq-helper-modal-actions">${actionButton(hp, "commit", "oduEepromWrite.commit", disabled || !choice.consent, true)}${actionButton(hp, "cancel", "common.cancel", disabled)}</div>`;
  } else if (status.baseline) {
    body = `<div class="oq-settings-odu-fields">${[[1, "oduEepromWrite.bottom"], [2, "oduEepromWrite.defrost"], [4, "oduEepromWrite.frequency"]].map(([group, key]) => `<label class="oq-settings-odu-auto"><input id="oq-eeprom-${hp}-group-${group}" type="checkbox" data-oq-eeprom-write-hp="${hp}" data-eeprom-group="${group}" ${choice.groups & group ? "checked" : ""} ${disabled ? "disabled" : ""}><span>${escapeHtml(t(key))}</span></label>`).join("")}</div>
      <div class="oq-helper-modal-actions">${actionButton(hp, "prepare", "oduEepromWrite.prepare", disabled || !choice.groups, true)}${actionButton(hp, "restore", "oduEepromWrite.restore", disabled)}</div>`;
  } else body = `<p>${escapeHtml(t("oduEepromWrite.firstBackup"))}</p>${actionButton(hp, "begin-backup", "oduEepromWrite.beginBackup", disabled || status.result === "UNSUPPORTED", true)}`;
  return `<article class="oq-odu-eeprom-device" data-eeprom-write-panel="${hp}"><div class="oq-odu-eeprom-device-head"><h4>HP${hp}</h4><span class="oq-odu-eeprom-state">${escapeHtml(t(RESULT_KEYS[status?.result] || "oduEepromWrite.unavailable"))}</span></div>${status?.legacy ? `<p>${escapeHtml(t("oduEepromWrite.legacyBinding"))}</p>` : ""}${status?.holding ? `<p role="status">${escapeHtml(t("oduEepromWrite.controlHold"))}</p>` : ""}${body}${status?.baseline ? `<div class="oq-helper-modal-actions"><button type="button" class="oq-helper-button oq-helper-button--ghost" data-oq-action="odu-eeprom-write-download" data-hp="${hp}" ${busy ? "disabled" : ""}>${escapeHtml(t("oduEepromWrite.download"))}</button></div>` : ""}</article>`;
}
export function renderEepromWriteModal() {
  return renderModalShell({
    modalId: "system", titleId: "oq-eeprom-write-title", kicker: t("oduEepromWrite.kicker"), title: t("oduEepromWrite.title"),
    copy: t("oduEepromWrite.copy"), className: "oq-helper-modal--wide oq-helper-modal--scrollable oq-helper-modal--odu-eeprom",
    closeAction: "close-system-modal", closeLabel: t("common.close"),
    body: `${feedback ? `<p role="alert">${escapeHtml(t(feedback))}</p>` : ""}<div class="oq-odu-eeprom-grid">${getOduEepromDumpHpIndexes().map(renderHp).join("")}</div>`,
    actions: `<button type="button" class="oq-helper-button oq-helper-button--ghost" data-oq-action="close-system-modal">${escapeHtml(t("common.close"))}</button>`,
  });
}
const handlers = {
  "open-odu-eeprom-write": () => { state.systemModal = "odu-eeprom-write"; feedback = ""; render(); void refresh(); },
  "odu-eeprom-write-run": run,
  "odu-eeprom-write-download": download,
};
export function handleEepromWriteAction(action, button) { return invokeActionMap(handlers, action, button); }
