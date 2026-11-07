import { renderOqIcon } from "../core/config.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";

const NOTICE_VISIBLE_MS = 4000;
// Long enough to notice an accidental change and undo it.
const UNDO_VISIBLE_MS = 8000;
const NOTICE_LEAVE_MS = 200;
let noticeTimer = 0;
// Every full render rebuilds the toast; these remember which message already appeared or is finished,
// so a rebuild neither replays the entrance nor brings back an old message.
let presentedFeedback = "";
let expiredFeedback = "";

function getFeedback() {
  const error = Boolean(state.controlError);
  const text = state.controlError || state.controlNotice;
  return text ? { key: `${error}:${text}`, error, text } : null;
}

function getUndo(feedback) {
  const undo = state.settingsUndo;
  return feedback && !feedback.error && undo?.notice === feedback.text ? undo : null;
}

// Feedback floats over the page (top or bottom, chosen in the interface menu) without shifting it.
export function renderSettingsFeedback() {
  const feedback = getFeedback();
  if (!feedback || feedback.key === expiredFeedback) {
    return "";
  }
  const { error, text } = feedback;
  const undo = getUndo(feedback);
  return `
    <div class="oq-settings-feedback${state.feedbackPosition === "bottom" ? " oq-settings-feedback--bottom" : ""}${error ? " oq-settings-feedback--error" : ""}${undo ? " oq-settings-feedback--action" : ""}${feedback.key === presentedFeedback ? " is-settled" : ""}" role="${error ? "alert" : "status"}">
      ${renderOqIcon(error ? "alert-circle" : "check", "oq-settings-feedback-icon")}
      <p>${escapeHtml(text)}${undo ? `<span class="oq-settings-feedback-detail">${escapeHtml(`${undo.from} → ${undo.to}`)}</span>` : ""}</p>
      ${undo ? `<button class="oq-settings-feedback-undo" type="button" data-oq-action="undo-settings-change"${state.busyAction ? " disabled" : ""}>${escapeHtml(t("actions.undo"))}</button>` : ""}
      ${error ? `<button class="oq-settings-feedback-close" type="button" data-oq-action="dismiss-settings-feedback" aria-label="${escapeHtml(t("common.close"))}">×</button>` : ""}
    </div>
  `;
}

function stopNoticeTimer() {
  window.clearTimeout(noticeTimer);
  noticeTimer = 0;
}

// Confirmations leave on their own, later when they offer undo; errors stay until dismissed or replaced. Leaving the settings page
// finishes the message. While a modal is visible the timer waits, because some modals show the notice themselves.
export function syncSettingsFeedback(root) {
  const feedback = getFeedback();
  if (!feedback) {
    stopNoticeTimer();
    presentedFeedback = "";
    expiredFeedback = "";
    return;
  }
  if (feedback.key === expiredFeedback) {
    return;
  }
  if (state.appView !== "settings") {
    stopNoticeTimer();
    presentedFeedback = "";
    expiredFeedback = feedback.key;
    return;
  }
  if (feedback.key !== presentedFeedback) {
    stopNoticeTimer();
    presentedFeedback = feedback.key;
  }
  if (feedback.error || root?.querySelector("[data-oq-modal]")) {
    stopNoticeTimer();
    return;
  }
  if (noticeTimer) {
    return;
  }
  noticeTimer = window.setTimeout(() => {
    noticeTimer = 0;
    if (presentedFeedback !== feedback.key) {
      return;
    }
    presentedFeedback = "";
    expiredFeedback = feedback.key;
    const toast = root?.querySelector(".oq-settings-feedback");
    if (!toast) {
      return;
    }
    toast.classList.add("is-leaving");
    window.setTimeout(() => toast.remove(), NOTICE_LEAVE_MS);
  }, getUndo(feedback) ? UNDO_VISIBLE_MS : NOTICE_VISIBLE_MS);
}

export function dismissSettingsFeedback() {
  state.controlError = "";
  state.controlNotice = "";
  state.settingsUndo = null;
}
