import { renderOqIcon } from "../core/config.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { CHANGE_LOG_GROUP_ID } from "./settings-change-log.js";

const NOTICE_VISIBLE_MS = 4000;
// Long enough to notice an accidental change and undo it.
const UNDO_VISIBLE_MS = 8000;
const ENTER_MS = 260;
const LEAVE_MS = 200;
// While the pointer or keyboard focus rests on the stack, or a modal is open, an expiring toast waits and checks again.
const HOLD_RETRY_MS = 1000;
const MAX_TOASTS = 3;
const STACK_GAP_PX = 8;

// Toasts, newest first. A full render rebuilds the page HTML, so the stack element itself is kept and put back after
// every render: the toasts keep their place and their transitions instead of being drawn again from scratch.
let toasts = [];
let nextToastId = 0;
let lastKey = "";
let lastUndo = null;
let undoToast = null;
let liveStack = null;
let lastRoot = null;
let resizeBound = false;
// The stack opens while the mouse or keyboard focus is on it, and on touch screens with a tap. A full render briefly
// takes the stack out of the page, which drops CSS :hover; these flags live on through that.
let pointerInside = false;
let keyboardInside = false;
let touchOpen = false;
const toastMarkup = new WeakMap();

function getFeedback() {
  const error = Boolean(state.controlError);
  const text = state.controlError || state.controlNotice;
  return text ? { key: `${error}:${text}`, error, text } : null;
}

function getUndo(feedback) {
  const undo = state.settingsUndo;
  return feedback && !feedback.error && undo?.notice === feedback.text ? undo : null;
}

// Every new message becomes one toast. The same text with a new undo is a new change (the same setting changed again).
// An error toast follows the error in state: it leaves once that error is cleared or replaced.
function collectFeedback() {
  toasts.filter((entry) => entry.error && entry.text !== state.controlError).forEach(dismissToast);
  const feedback = getFeedback();
  if (!feedback) {
    lastKey = "";
    lastUndo = null;
    return;
  }
  const undo = getUndo(feedback);
  const isNew = feedback.key !== lastKey || (undo && undo !== lastUndo);
  lastKey = feedback.key;
  lastUndo = undo;
  // While an undo runs, its toast takes the outcome instead of a new toast.
  if (!isNew || undoToast) return;
  toasts.unshift({
    id: ++nextToastId, error: feedback.error, text: feedback.text, detail: undo ? `${undo.from} → ${undo.to}` : "",
    undo, undoKey: undo?.key || "", timer: 0, entered: false,
  });
  toasts.slice(MAX_TOASTS).forEach(dismissToast);
}

// An older toast cannot undo a setting that a newer toast changed again.
function canUndo(entry) {
  return Boolean(entry.undo) && !toasts.some((other) => other.id > entry.id && other.undoKey === entry.undoKey);
}

function renderToastContent(entry) {
  const undo = canUndo(entry)
    ? `<button class="oq-settings-feedback-undo" type="button" data-oq-action="undo-settings-change" data-toast-id="${entry.id}"${state.busyAction || undoToast ? " disabled" : ""}>${escapeHtml(t("actions.undo"))}</button>`
    : "";
  const log = entry.undoKey && state.settingsGroup !== CHANGE_LOG_GROUP_ID
    ? `<button class="oq-settings-feedback-log" type="button" data-oq-action="select-settings-group" data-group-id="${CHANGE_LOG_GROUP_ID}">${escapeHtml(t("actions.viewChangeLog"))}</button>`
    : "";
  return `<div class="oq-settings-feedback-inner">${renderOqIcon(entry.error ? "alert-circle" : "check", "oq-settings-feedback-icon")}<p>${escapeHtml(entry.text)}${entry.detail ? `<span class="oq-settings-feedback-detail">${escapeHtml(entry.detail)}</span>` : ""}</p><button class="oq-settings-feedback-close" type="button" data-oq-action="dismiss-settings-feedback" data-toast-id="${entry.id}" aria-label="${escapeHtml(t("common.close"))}">×</button>${log || undo ? `<div class="oq-settings-feedback-footer">${log}${undo}</div>` : ""}</div>`;
}

function renderToast(entry) {
  return `<div class="oq-settings-feedback${entry.error ? " oq-settings-feedback--error" : ""}" data-toast-id="${entry.id}"${entry.error ? ' role="alert"' : ""}>${renderToastContent(entry)}</div>`;
}

// Feedback floats over the page (top or bottom, chosen in the interface menu) without shifting it.
export function renderSettingsFeedback() {
  collectFeedback();
  return `<div class="oq-settings-feedback-stack${state.feedbackPosition === "bottom" ? " oq-settings-feedback-stack--bottom" : ""}" data-oq-feedback-stack aria-live="polite">${toasts.map(renderToast).join("")}</div>`;
}

function findToastElement(id) {
  return liveStack?.querySelector?.(`:scope > [data-toast-id="${id}"]:not(.is-leaving)`) || null;
}

// Collapsed, older toasts peek out behind the newest one; expanded (hover or focus), each sits at its own offset.
function layoutStack() {
  if (!liveStack?.style) return;
  let offset = 0;
  let frontHeight = 0;
  toasts.forEach((entry, index) => {
    const element = findToastElement(entry.id);
    if (!element) return;
    const height = Math.ceil((element.firstElementChild?.offsetHeight || 0) + element.offsetHeight - element.clientHeight);
    if (index === 0) frontHeight = height;
    element.style.setProperty("--oq-toast-index", String(index));
    element.style.setProperty("--oq-toast-offset", `${offset}px`);
    element.style.setProperty("--oq-toast-height", `${height}px`);
    element.classList.toggle("is-behind", index > 0);
    offset += height + STACK_GAP_PX;
  });
  liveStack.style.setProperty("--oq-front-height", `${frontHeight}px`);
  liveStack.style.setProperty("--oq-stack-height", `${Math.max(0, offset - STACK_GAP_PX)}px`);
}

function reconcileStack() {
  if (!liveStack) return;
  for (const entry of toasts) {
    const markup = renderToast(entry);
    let element = findToastElement(entry.id);
    if (!element) {
      liveStack.insertAdjacentHTML("beforeend", markup);
      element = findToastElement(entry.id);
    } else if (toastMarkup.get(element) !== markup) {
      element.className = `oq-settings-feedback${entry.error ? " oq-settings-feedback--error" : ""}`;
      if (entry.error) element.setAttribute("role", "alert");
      else element.removeAttribute("role");
      element.innerHTML = renderToastContent(entry);
    }
    if (!element) continue;
    toastMarkup.set(element, markup);
    if (!entry.entered) {
      entry.entered = true;
      const shift = state.feedbackPosition === "bottom" ? 16 : -16;
      if (!state.reducedMotion) {
        element.animate?.([{ opacity: 0, translate: `0 ${shift}px`, scale: "0.96" }, { opacity: 1, translate: "0 0", scale: "1" }],
          { duration: ENTER_MS, easing: "cubic-bezier(0.2, 0.8, 0.2, 1)" });
      }
    }
  }
  layoutStack();
}

function updateExpanded() {
  if (!toasts.length) touchOpen = false;
  liveStack?.classList?.toggle("is-open", pointerInside || keyboardInside || touchOpen);
}

// After a render the pointer may rest on the same spot; its next movement tells whether it is still on the stack.
function recheckPointer(event) {
  pointerInside = Boolean(liveStack?.contains(event.target));
  updateExpanded();
}

function bindStack(stack) {
  if (typeof stack.addEventListener !== "function") return;
  stack.addEventListener("pointerenter", (event) => {
    if (event.pointerType === "touch") return;
    pointerInside = true;
    updateExpanded();
  });
  stack.addEventListener("pointerleave", (event) => {
    if (event.pointerType === "touch") return;
    pointerInside = false;
    updateExpanded();
  });
  stack.addEventListener("pointerup", (event) => {
    if (event.pointerType !== "touch" || event.target?.closest?.("button")) return;
    touchOpen = !touchOpen;
    updateExpanded();
  });
  stack.addEventListener("focusin", (event) => {
    keyboardInside = Boolean(event.target?.matches?.(":focus-visible"));
    updateExpanded();
  });
  stack.addEventListener("focusout", (event) => {
    if (stack.contains(event.relatedTarget)) return;
    keyboardInside = false;
    updateExpanded();
  });
}

function mountStack(root) {
  const placeholder = root?.querySelector?.("[data-oq-feedback-stack]");
  if (!placeholder) return;
  if (liveStack && placeholder !== liveStack) {
    liveStack.className = placeholder.className;
    placeholder.replaceWith(liveStack);
    if (pointerInside) document.addEventListener?.("pointermove", recheckPointer, { once: true, passive: true });
  } else {
    liveStack = placeholder;
    bindStack(liveStack);
  }
  updateExpanded();
  if (!resizeBound && typeof window.addEventListener === "function") {
    window.addEventListener("resize", layoutStack, { passive: true });
    resizeBound = true;
  }
}

// A tap-opened stack on a touch screen does not hold the toasts, or they would stay once the phone is put away.
function isStackHeld(entry) {
  return entry === undoToast || pointerInside || keyboardInside || Boolean(lastRoot?.querySelector?.("[data-oq-modal]"));
}

function expireToast(entry) {
  entry.timer = 0;
  if (!toasts.includes(entry)) return;
  if (isStackHeld(entry)) {
    entry.timer = window.setTimeout(() => expireToast(entry), HOLD_RETRY_MS);
    return;
  }
  dismissToast(entry);
}

// Confirmations leave on their own, later when they offer undo; errors stay until dismissed or replaced.
function scheduleToast(entry) {
  if (entry.timer || entry.error) return;
  entry.timer = window.setTimeout(() => expireToast(entry), entry.undo ? UNDO_VISIBLE_MS : NOTICE_VISIBLE_MS);
}

function dismissToast(entry) {
  const index = toasts.indexOf(entry);
  if (index < 0) return;
  const element = findToastElement(entry.id);
  toasts.splice(index, 1);
  window.clearTimeout(entry.timer);
  entry.timer = 0;
  if (entry === undoToast) undoToast = null;
  if (element) {
    element.classList.add("is-leaving");
    window.setTimeout(() => element.remove(), state.reducedMotion ? 0 : LEAVE_MS);
  }
  layoutStack();
  updateExpanded();
}

// Leaving the settings page finishes every toast; a message still in state does not come back on return.
export function syncSettingsFeedback(root) {
  lastRoot = root;
  if (state.appView !== "settings") {
    toasts.forEach((entry) => window.clearTimeout(entry.timer));
    toasts = [];
    undoToast = null;
    const feedback = getFeedback();
    lastKey = feedback?.key || "";
    lastUndo = getUndo(feedback);
    liveStack?.replaceChildren?.();
    return;
  }
  collectFeedback();
  mountStack(root);
  reconcileStack();
  toasts.forEach(scheduleToast);
}

// Hands out the undo of one toast; that toast shows the outcome once finishFeedbackUndo runs.
export function takeFeedbackUndo(id) {
  const entry = toasts.find((candidate) => String(candidate.id) === String(id));
  if (!entry || !canUndo(entry) || undoToast || state.busyAction) return null;
  const { undo } = entry;
  entry.undo = null;
  undoToast = entry;
  reconcileStack();
  return undo;
}

export function finishFeedbackUndo() {
  const entry = undoToast;
  undoToast = null;
  if (!entry || !toasts.includes(entry)) return;
  const feedback = getFeedback();
  if (feedback) {
    Object.assign(entry, { text: feedback.text, error: feedback.error, detail: "" });
    lastKey = feedback.key;
    lastUndo = getUndo(feedback);
  }
  window.clearTimeout(entry.timer);
  entry.timer = 0;
  if (state.appView !== "settings") return;
  reconcileStack();
  scheduleToast(entry);
}

// Without an id every toast goes. Dismissing the message that is still in state also clears it there.
// Returns true when that cleared an error, which other parts of the page may show as well.
export function dismissSettingsFeedback(id) {
  const entry = id == null ? null : toasts.find((candidate) => String(candidate.id) === String(id));
  if (id != null && !entry) return false;
  const current = getFeedback();
  const clearsState = !entry || Boolean(current && entry.text === current.text && entry.error === current.error);
  const clearedError = clearsState && Boolean(state.controlError);
  if (entry) dismissToast(entry);
  else [...toasts].forEach(dismissToast);
  if (clearsState) {
    state.controlError = "";
    state.controlNotice = "";
    state.settingsUndo = null;
    lastKey = "";
    lastUndo = null;
  }
  return clearedError;
}
