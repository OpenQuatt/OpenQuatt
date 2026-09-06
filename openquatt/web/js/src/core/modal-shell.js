import { escapeHtml } from "./html.js";
import { state } from "./state.js";

let activeModalId = "";
let modalFocusOrigin = null;

const focusIdentityAttributes = [
  "id",
  "data-oq-action",
  "data-oq-field",
  "data-group-id",
  "data-view-id",
  "aria-label",
];

function captureFocusIdentity(element) {
  if (!element || element === document.body || typeof element.getAttribute !== "function") {
    return null;
  }
  const attributes = focusIdentityAttributes
    .map((name) => [name, element.getAttribute(name)])
    .filter(([, value]) => value);
  return attributes.length ? { tagName: element.tagName, attributes } : null;
}

function findFocusOrigin(root, identity) {
  if (!root || !identity) {
    return null;
  }
  return Array.from(root.querySelectorAll(identity.tagName.toLowerCase())).find((element) =>
    identity.attributes.every(([name, value]) => element.getAttribute(name) === value)
  ) || null;
}

export function renderModalShell({
  id = "",
  modalId,
  titleId,
  kicker,
  title,
  titleBadge = "",
  copy = "",
  body = "",
  bodyMarkup = "",
  actions = "",
  backdropClass = "",
  className = "",
  modalClass = "",
  role = "dialog",
  ariaModal = role === "dialog",
  ariaLive = "",
  sectionAttributes = "",
  closeAction = "",
  closeLabel = "",
  headerMarkup = "",
  copyInHeader = false,
}) {
  const resolvedModalId = modalId || id;
  const resolvedModalClass = modalClass || className;
  const resolvedBody = bodyMarkup || body;
  if (!activeModalId && typeof document !== "undefined" && !document.querySelector('[role="dialog"][aria-modal="true"]')) {
    modalFocusOrigin = captureFocusIdentity(document.activeElement);
  }
  const backdropClasses = `oq-helper-modal-backdrop${state.overviewTheme === "dark" ? " oq-helper-modal-backdrop--dark" : ""}${backdropClass ? ` ${backdropClass}` : ""}`;
  const modalClasses = `oq-helper-modal${resolvedModalClass ? ` ${resolvedModalClass}` : ""}`;
  const closeMarkup = closeAction
    ? `<button class="oq-helper-modal-close" type="button" data-oq-action="${escapeHtml(closeAction)}" aria-label="${escapeHtml(closeLabel)}">×</button>`
    : "";
  const ariaAttributes = [
    `role="${escapeHtml(role)}"`,
    ariaModal ? 'aria-modal="true"' : "",
    ariaLive ? `aria-live="${escapeHtml(ariaLive)}"` : "",
    `aria-labelledby="${escapeHtml(titleId)}"`,
    sectionAttributes,
    'tabindex="-1"',
  ].filter(Boolean).join(" ");

  return `
    <div class="${backdropClasses}" data-oq-modal="${escapeHtml(resolvedModalId)}" data-oq-modal-scroll="backdrop">
      <section class="${modalClasses}" ${ariaAttributes} data-oq-modal-scroll="dialog">
        ${headerMarkup || `<div class="oq-helper-modal-head">
          <div>
            <p class="oq-helper-modal-kicker">${escapeHtml(kicker)}</p>
            <h2 class="oq-helper-modal-title" id="${escapeHtml(titleId)}">${escapeHtml(title)}${titleBadge ? ` <span class="oq-helper-modal-badge">${escapeHtml(titleBadge)}</span>` : ""}</h2>
            ${copy && copyInHeader ? `<p class="oq-helper-modal-copy">${escapeHtml(copy)}</p>` : ""}
          </div>
          ${closeMarkup}
        </div>`}
        ${copy && !copyInHeader ? `<p class="oq-helper-modal-copy">${escapeHtml(copy)}</p>` : ""}
        ${resolvedBody}
        ${actions ? `<div class="oq-helper-modal-actions">${actions}</div>` : ""}
      </section>
    </div>
  `;
}

export function syncModalFocus(root) {
  if (!root || typeof document === "undefined") {
    return;
  }

  const dialog = root.querySelector('[role="dialog"][aria-modal="true"]');
  if (dialog) {
    activeModalId = dialog.closest("[data-oq-modal]")?.dataset.oqModal || "dialog";
    if (!dialog.contains(document.activeElement)) {
      const focusTarget = dialog.querySelector(".oq-helper-modal-close, button, input, select, textarea, a[href]") || dialog;
      focusTarget.focus({ preventScroll: true });
    }
    return;
  }

  if (activeModalId) {
    findFocusOrigin(root, modalFocusOrigin)?.focus({ preventScroll: true });
  }
  activeModalId = "";
  modalFocusOrigin = null;
}
