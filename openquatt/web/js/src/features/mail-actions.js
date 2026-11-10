import { invokeActionMap } from "../core/action-router.js";
import { updateMailState } from "../core/feature-state.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { shouldRefreshSupplementaryStatus } from "../core/supplementary-refresh.js";
import { isIntegrationsSettingsGroupActive } from "../core/surface-state.js";
import { t } from "../i18n/index.js";
import {
  buildMailSaveParams,
  getMailDraft,
  getMailDraftStateFromStatus,
  getMailSaveErrorMessage,
  getMailSaveOutcome,
  getMailSendErrorMessage,
  getMailStatusSignature,
  getMailTestBlockReason,
  getMailTestOutcome,
  getMailTestRequestErrorMessage,
  MAIL_KIND_KEYS,
  normalizeMailStatus,
  validateMailDraft,
} from "./mail.js";

export const MAIL_POLL_INTERVAL_MS = 1000;
// A save or test can queue behind a running delivery attempt (20 s deadline) and,
// for a test, behind a weather request holding the shared HTTPS slot.
export const MAIL_SAVE_WAIT_MS = 30000;
export const MAIL_TEST_WAIT_MS = 65000;

const mailSleep = (ms) => new Promise((resolve) => {
  window.setTimeout(resolve, ms);
});

export function shouldRefreshMailStatusForCurrentSurface() {
  return state.systemModal === "mail" || isIntegrationsSettingsGroupActive();
}

function shouldKeepMailDrafts() {
  return Boolean(state.mailBusy || state.mailPendingSave || (state.systemModal === "mail" && state.mailDraftDirty));
}

export function syncMailDraftsFromStatus() {
  updateMailState(getMailDraftStateFromStatus(state.mailStatus));
}

export async function refreshMailStatus(options = {}) {
  if (!shouldRefreshSupplementaryStatus(state.mailLastStatusRefreshAt, options)) {
    return false;
  }
  updateMailState({ mailLastStatusRefreshAt: Date.now() });
  const previousSignature = `${state.mailSupported}:${state.mailUnavailable}:${getMailStatusSignature()}`;
  try {
    const response = await fetch("/openquatt/mail/status", { cache: "no-store" });
    if (response.status === 404) {
      // Firmware without e-mail notifications: hide the section entirely.
      updateMailState({ mailSupported: false, mailUnavailable: false, mailStatus: null, mailError: "" });
    } else if (response.status === 503) {
      updateMailState({ mailSupported: true, mailUnavailable: true, mailStatus: null });
    } else if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    } else {
      const status = normalizeMailStatus(await response.json());
      const changed = getMailStatusSignature(status) !== getMailStatusSignature();
      updateMailState({ mailSupported: true, mailUnavailable: false, mailStatus: status });
      if (changed && !shouldKeepMailDrafts()) {
        syncMailDraftsFromStatus();
      }
    }
    if (state.systemModal !== "mail" && state.mailError && !state.mailStatus) {
      updateMailState({ mailError: "" });
    }
  } catch (error) {
    if (!state.mailStatus) {
      updateMailState({ mailError: t("mail.statusLoadFailed", { error: error.message }) });
    }
    return false;
  }
  return previousSignature !== `${state.mailSupported}:${state.mailUnavailable}:${getMailStatusSignature()}`;
}

// Polls a firmware-confirmed outcome. Returns the terminal outcome, "timeout"
// after the deadline or "detached" once the caller no longer follows it.
export async function waitForMailOutcome({ outcome, deadlineAt, shouldContinue, refresh, sleep = mailSleep, now = Date.now }) {
  for (;;) {
    const current = outcome();
    if (current !== "pending") return current;
    if (now() >= deadlineAt) return "timeout";
    if (!shouldContinue()) return "detached";
    await sleep(MAIL_POLL_INTERVAL_MS);
    if (!shouldContinue()) return "detached";
    await refresh();
  }
}

const followsMailModal = (pending, key) => () => state.systemModal === "mail" && state[key] === pending;

async function followMailSave() {
  const pending = state.mailPendingSave;
  if (!pending) return;
  updateMailState({ mailBusy: "save" });
  render();
  const result = await waitForMailOutcome({
    outcome: () => getMailSaveOutcome(state.mailStatus, pending.revision),
    deadlineAt: pending.deadlineAt,
    shouldContinue: followsMailModal(pending, "mailPendingSave"),
    refresh: () => refreshMailStatus({ force: true }),
  });
  if (state.mailPendingSave !== pending) return;
  if (result === "detached") {
    // Keep the pending revision; reopening the modal reconciles it.
    updateMailState({ mailBusy: "" });
    return;
  }
  updateMailState({ mailPendingSave: null, mailBusy: "" });
  if (result === "stored") {
    syncMailDraftsFromStatus();
    updateMailState({
      mailNotice: state.mailStatus?.enabled ? t("mail.savedOn") : t("mail.savedOff"),
      mailError: "",
    });
  } else if (result === "failed") {
    updateMailState({ mailError: t("mail.saveFailedPersist", { error: state.mailStatus?.save.error || "?" }) });
  } else {
    updateMailState({ mailError: t("mail.saveUncertain") });
  }
  render();
}

async function followMailTest() {
  const pending = state.mailPendingTest;
  if (!pending) return;
  updateMailState({ mailBusy: "test" });
  render();
  const result = await waitForMailOutcome({
    outcome: () => getMailTestOutcome(state.mailStatus, pending.id),
    deadlineAt: pending.deadlineAt,
    shouldContinue: followsMailModal(pending, "mailPendingTest"),
    refresh: async () => {
      await refreshMailStatus({ force: true });
      render();
    },
  });
  if (state.mailPendingTest !== pending) return;
  if (result === "detached") {
    updateMailState({ mailBusy: "" });
    return;
  }
  updateMailState({ mailPendingTest: null, mailBusy: "" });
  if (result === "sent") {
    updateMailState({ mailNotice: t("mail.testSent", { recipients: state.mailStatus?.recipients || "" }), mailError: "" });
  } else if (result === "failed") {
    const test = state.mailStatus?.test || {};
    updateMailState({ mailError: t("mail.testFailed", { reason: getMailSendErrorMessage(test.error, test.code) }) });
  } else {
    updateMailState({ mailError: t("mail.testUnknown") });
  }
  render();
}

async function postMailForm(url, params) {
  const response = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
    body: params.toString(),
  });
  const payload = await response.json().catch(() => ({}));
  return { response, payload: payload && typeof payload === "object" ? payload : {} };
}

function getMailRejectCode(response, payload) {
  if (response.status === 403) return "forbidden";
  return String(payload.error || `HTTP ${response.status}`);
}

export async function commitMailConfig() {
  const status = state.mailStatus;
  if (state.mailBusy || state.mailPendingSave || state.mailPendingTest) return;
  if (!status?.csrf_token) {
    updateMailState({ mailError: t("mail.configLoading") });
    render();
    return;
  }
  const draft = getMailDraft();
  const validationError = validateMailDraft(draft, status);
  if (validationError) {
    updateMailState({ mailError: getMailSaveErrorMessage(validationError), mailNotice: "" });
    render();
    return;
  }

  updateMailState({ mailBusy: "save", mailNotice: "", mailError: "" });
  render();
  let revision = 0;
  try {
    const { response, payload } = await postMailForm("/openquatt/mail/save", buildMailSaveParams(draft, status.csrf_token));
    if (!response.ok || payload.ok !== true) {
      updateMailState({ mailBusy: "", mailError: getMailSaveErrorMessage(getMailRejectCode(response, payload)) });
      render();
      return;
    }
    revision = Number(payload.revision) || 0;
  } catch (_error) {
    // The request may have reached the device. Never repeat it automatically.
    updateMailState({ mailBusy: "", mailError: t("mail.saveUncertain") });
    render();
    void refreshMailStatus({ force: true }).then(render);
    return;
  }
  updateMailState({ mailPendingSave: { revision, deadlineAt: Date.now() + MAIL_SAVE_WAIT_MS } });
  await followMailSave();
}

export async function sendMailTest() {
  const status = state.mailStatus;
  if (state.mailBusy || state.mailPendingSave || state.mailPendingTest) return;
  const blockReason = getMailTestBlockReason(status);
  if (blockReason) {
    updateMailState({ mailError: blockReason, mailNotice: "" });
    render();
    return;
  }

  updateMailState({ mailBusy: "test", mailNotice: "", mailError: "" });
  render();
  let testId = 0;
  try {
    const params = new URLSearchParams();
    params.set("csrf_token", status.csrf_token);
    const { response, payload } = await postMailForm("/openquatt/mail/test", params);
    if (!response.ok || payload.ok !== true) {
      updateMailState({ mailBusy: "", mailError: getMailTestRequestErrorMessage(getMailRejectCode(response, payload)) });
      render();
      return;
    }
    testId = Number(payload.test_id) || 0;
  } catch (_error) {
    updateMailState({ mailBusy: "", mailError: t("mail.testUnknown") });
    render();
    return;
  }
  updateMailState({ mailPendingTest: { id: testId, deadlineAt: Date.now() + MAIL_TEST_WAIT_MS } });
  await followMailTest();
}

export function syncMailDraftFromInput(input) {
  const field = input?.dataset?.oqMailField;
  if (!field) {
    return false;
  }
  const value = String(input.value || "");
  const checked = Boolean(input.checked);
  const wasDirty = state.mailDraftDirty;
  let rerender = false;
  switch (field) {
    case "enabled": updateMailState({ mailDraftEnabled: checked }); break;
    case "provider": updateMailState({ mailDraftProvider: value }); rerender = true; break;
    case "smtp-host": updateMailState({ mailDraftSmtpHost: value }); break;
    case "smtp-port": updateMailState({ mailDraftSmtpPort: value }); break;
    case "username": updateMailState({ mailDraftUsername: value }); break;
    case "secret": updateMailState({ mailDraftSecret: value }); break;
    case "clear-secret":
      updateMailState({ mailDraftClearSecret: checked, ...(checked ? { mailDraftSecret: "" } : {}) });
      rerender = true;
      break;
    case "domain": updateMailState({ mailDraftDomain: value }); break;
    case "from": updateMailState({ mailDraftFrom: value }); break;
    case "recipients": updateMailState({ mailDraftRecipients: value }); break;
    case "recovery": updateMailState({ mailDraftRecovery: checked }); break;
    case "kind": {
      const kind = String(input.dataset.oqMailKind || "");
      if (!MAIL_KIND_KEYS.includes(kind)) return true;
      updateMailState({ mailDraftKinds: { ...state.mailDraftKinds, [kind]: checked } });
      break;
    }
    default:
      return true;
  }
  updateMailState({ mailDraftDirty: true, mailNotice: "", mailError: "" });
  if (rerender) {
    render();
    return true;
  }
  const modal = input.closest(".oq-helper-modal");
  modal?.querySelectorAll(".oq-helper-modal-success, .oq-helper-modal-note--error").forEach((node) => {
    node.remove();
  });
  if (!wasDirty && modal) {
    // A changed draft must be saved before a test mail can use it.
    const testButton = modal.querySelector('[data-oq-action="send-mail-test"]');
    if (testButton) testButton.disabled = true;
    const hint = modal.querySelector("[data-oq-mail-test-hint]");
    if (hint) hint.textContent = t("mail.testNeedsSave");
  }
  return true;
}

const mailActionHandlers = {
  "open-mail-modal": async () => {
    state.systemModal = "mail";
    if (!state.mailPendingSave && !state.mailPendingTest) {
      syncMailDraftsFromStatus();
      updateMailState({ mailNotice: "", mailError: "" });
    }
    render();
    await refreshMailStatus({ force: true });
    if (state.systemModal !== "mail") return;
    if (!state.mailPendingSave && !state.mailPendingTest && !state.mailDraftDirty) {
      syncMailDraftsFromStatus();
    }
    render();
    if (state.mailPendingSave) {
      await followMailSave();
    } else if (state.mailPendingTest) {
      await followMailTest();
    }
  },
  "save-mail-config": () => commitMailConfig(),
  "send-mail-test": () => sendMailTest(),
};

export function handleMailAction(action, button) {
  return invokeActionMap(mailActionHandlers, action, button);
}
