import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";

// Presentation only: each service keeps its own write, identity and busy gates.
export function renderOduEditorAction(hp, action, label, disabled, variant = "ghost") {
  const buttonClass = variant === "primary" ? "oq-helper-button--primary"
    : variant === "warning" ? "oq-helper-button--warning" : "oq-helper-button--ghost";
  return `<button class="oq-helper-button ${buttonClass}" type="button" data-oq-action="${escapeHtml(action)}" data-hp="${hp}" ${disabled ? "disabled" : ""}>${escapeHtml(label)}</button>`;
}

export function renderOduEditorPanel({ hp, title, copy, actions, statusLabel, tone, body }) {
  return `<article class="oq-settings-odu-runtime-panel">
    <div class="oq-settings-odu-runtime-panel-head">
      <div><p class="oq-helper-label">HP${hp}</p><h4>${escapeHtml(title)}</h4><p>${escapeHtml(copy)}</p></div>
      <div class="oq-settings-odu-runtime-actions">${actions}</div>
    </div>
    <div class="oq-settings-odu-runtime-status${tone ? ` is-${tone}` : ""}" role="status"><strong>${escapeHtml(statusLabel)}</strong></div>
    ${body}
  </article>`;
}

export function renderOduEditorModal({ modalId, titleId, title, closeLabel, warning, error, notice = "", panels, footer = "" }) {
  return renderModalShell({
    modalId,
    titleId,
    kicker: "Instellingen buitenunit",
    title,
    titleBadge: "Experimenteel",
    closeAction: "close-system-modal",
    closeLabel,
    modalClass: "oq-helper-modal--wide oq-settings-odu-modal",
    bodyMarkup: `<div class="oq-settings-odu-modal-body" data-oq-modal-scroll="body">
      <div class="oq-settings-odu-runtime-warning" role="note">${warning}</div>
      ${error ? `<p class="oq-helper-error" role="alert">${escapeHtml(error)}</p>` : ""}
      ${notice ? `<p class="oq-helper-notice" role="status">${escapeHtml(notice)}</p>` : ""}
      <div class="oq-settings-odu-runtime-panels">${panels}</div>
      ${footer}
    </div>`,
  });
}
