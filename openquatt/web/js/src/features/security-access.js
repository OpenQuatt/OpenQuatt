import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { t } from "../i18n/index.js";

  export function getWebAuthStatusLabel() {
    const authStatus = state.authStatus;
    if (!authStatus) {
      return t("securityAccess.webLoading");
    }
    if (authStatus.enabled) {
      return authStatus.setup_window_active ? t("securityAccess.webSetupWindow") : t("securityAccess.webSecured");
    }
    return t("securityAccess.webUnsecured");
  }

  export function getWebAuthModalTitle() {
    return t("securityAccess.webTitle");
  }

  export function getWebAuthModalCopy() {
    const authStatus = state.authStatus;
    if (!authStatus) {
      return t("securityAccess.webCopyLoading");
    }
    if (authStatus.enabled) {
      return t("securityAccess.webCopyEnabled");
    }
    return t("securityAccess.webCopyOpen");
  }

  export function getWebAuthStatusDetail() {
    const authStatus = state.authStatus;
    if (!authStatus) {
      return t("securityAccess.webDetailLoading");
    }
    if (authStatus.enabled) {
      return authStatus.setup_window_active
        ? t("securityAccess.webDetailSetup")
        : t("securityAccess.webDetailActive", { source: authStatus.source ? t("securityAccess.webDetailSource", { source: authStatus.source }) : "" });
    }
    return authStatus.setup_window_active
      ? t("securityAccess.webDetailOffSetup")
      : t("securityAccess.webDetailOffOpen");
  }

  export function getApiSecurityStatusLabel() {
    if (state.apiSecurityError) {
      return t("securityAccess.apiUnavailable");
    }
    const status = state.apiSecurityStatus;
    if (!status) {
      return t("securityAccess.webLoading");
    }
    if (status.transport_active === true) {
      return t("securityAccess.apiActive");
    }
    if (status.provisioning_closed === true) {
      return t("securityAccess.apiUnavailable");
    }
    if (status.provisioning_pending === true) {
      return t("securityAccess.apiWaitLink");
    }
    return t("securityAccess.apiUnavailable");
  }

  export function getApiSecurityStatusDetail() {
    if (state.apiSecurityError) {
      return t("securityAccess.apiErrorCopy");
    }
    const status = state.apiSecurityStatus;
    if (!status) {
      return t("securityAccess.apiCheckingCopy");
    }
    if (status.transport_active === true) {
      return t("securityAccess.apiSetCopy");
    }
    if (status.provisioning_pending === true) {
      return t("securityAccess.apiPendingCopy");
    }
    if (status.provisioning_closed === true) {
      return t("securityAccess.apiClosedCopy");
    }
    return t("securityAccess.apiTempCopy");
  }

  export function getApiSecurityModalTitle() {
    return t("securityAccess.apiTitle");
  }

  export function getApiSecurityModalCopy() {
    return t("securityAccess.apiCopy");
  }

  export function renderLoginStatusRow(label, value, copy = "", loading = false) {
    return `
      <div class="oq-helper-modal-row${loading ? " oq-helper-modal-row--loading" : ""}">
        <span class="oq-helper-modal-label">${escapeHtml(label)}</span>
        <strong class="oq-helper-modal-value">${loading ? `
          <span class="oq-helper-modal-loading">
            <span class="oq-helper-reconnect-spinner" aria-hidden="true"></span>
            <span>${escapeHtml(value)}</span>
          </span>
        ` : escapeHtml(value)}</strong>
      ${copy ? `<span class="oq-helper-modal-subvalue">${escapeHtml(copy)}</span>` : ""}
    </div>
    `;
  }

  export function renderApiSecurityModal() {
    return renderModalShell({
      id: "system",
      titleId: "oq-api-security-modal-title",
      kicker: t("securityAccess.apiKicker"),
      title: getApiSecurityModalTitle(),
      copy: getApiSecurityModalCopy(),
      className: "oq-helper-modal--wide",
      closeAction: "close-system-modal",
      closeLabel: t("securityAccess.apiClose"),
      body: `
        <div class="oq-settings-api-security-shell oq-settings-api-security-shell--modal">
          ${state.apiSecurityNotice ? `<p role="status">${escapeHtml(state.apiSecurityNotice)}</p>` : ""}
          ${state.apiSecurityError ? `<p role="alert">${escapeHtml(state.apiSecurityError)}</p>` : ""}
          ${state.apiSecurityActionError ? `<p role="alert">${escapeHtml(state.apiSecurityActionError)}</p>` : ""}
          <div class="oq-helper-modal-grid">
            ${renderLoginStatusRow(t("securityAccess.apiStatusRow"), getApiSecurityStatusLabel(), getApiSecurityStatusDetail())}
            ${renderLoginStatusRow(t("securityAccess.apiManageRow"), t("securityAccess.apiManageValue"), t("securityAccess.apiManageCopy"))}
          </div>
          <p>${t("recoveryUi.apiResetCopy")}</p>
          ${!state.authStatus?.enabled ? `<p>${t("recoveryUi.apiResetLogin")}</p>` : ""}
        </div>`,
      actions: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="reset-api-security" ${!state.authStatus?.enabled || state.apiSecurityBusy || state.wifiResetBusy ? "disabled" : ""}>${t("recoveryUi.apiResetButton")}</button><button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal">${escapeHtml(t("header.done"))}</button>`,
    });
  }

  export function renderLoginModal() {
    const authStatus = state.authStatus || {};
    const authEnabled = authStatus.enabled === true;
    const canEdit = authEnabled;
    const usernameValue = authEnabled ? String(authStatus.username || "").trim() : "";
    const noticeMarkup = state.authNotice
      ? `<div class="oq-helper-modal-success oq-helper-modal-success--compact" aria-live="polite"><strong>${escapeHtml(t("securityAccess.loginSaved"))}</strong><span>${escapeHtml(state.authNotice)}</span></div>`
      : "";
    const errorMarkup = state.authError
      ? `<div class="oq-helper-modal-note oq-helper-modal-note--error" aria-live="assertive">${escapeHtml(state.authError)}</div>`
      : "";
    const authFormIntro = canEdit
      ? `<p class="oq-helper-modal-intro">${escapeHtml(authEnabled ? t("securityAccess.loginEditIntro") : t("securityAccess.loginNewIntro"))}</p>`
      : "";
    const authFormMarkup = canEdit
      ? `
        ${authFormIntro}
        <div class="oq-helper-modal-auth-stack">
          ${authEnabled
            ? `
              <label class="oq-helper-modal-auth-field">
                <span>${escapeHtml(t("securityAccess.loginCurrentPass"))}</span>
                <input
                  class="oq-helper-input"
                  type="password"
                  autocomplete="current-password"
                  data-oq-auth-field="currentPassword"
                  value="${escapeHtml(state.authDraftCurrentPassword)}"
                  ${state.authBusy ? "disabled" : ""}
                >
              </label>
            `
            : ""}
          <label class="oq-helper-modal-auth-field">
            <span>${escapeHtml(t("securityAccess.loginNewUser"))}</span>
            <input
              class="oq-helper-input"
              type="text"
              autocomplete="username"
              maxlength="32"
              data-oq-auth-field="username"
              value="${escapeHtml(state.authDraftUsername)}"
              ${state.authBusy ? "disabled" : ""}
            >
          </label>
          <label class="oq-helper-modal-auth-field">
            <span>${escapeHtml(t("securityAccess.loginNewPass"))}</span>
            <input
              class="oq-helper-input"
              type="password"
              autocomplete="new-password"
              maxlength="64"
              data-oq-auth-field="newPassword"
              value="${escapeHtml(state.authDraftNewPassword)}"
              ${state.authBusy ? "disabled" : ""}
            >
          </label>
          <label class="oq-helper-modal-auth-field">
            <span>${escapeHtml(t("securityAccess.loginRepeatPass"))}</span>
            <input
              class="oq-helper-input"
              type="password"
              autocomplete="new-password"
              maxlength="64"
              data-oq-auth-field="confirmPassword"
              value="${escapeHtml(state.authDraftConfirmPassword)}"
              ${state.authBusy ? "disabled" : ""}
            >
          </label>
        </div>
      `
      : `
        <div class="oq-helper-modal-callout oq-helper-modal-callout--subtle">
          <strong>${escapeHtml(t("securityAccess.loginAddTitle"))}</strong>
          <span>${t("securityAccess.loginAddCopy")}</span>
        </div>
      `;

    return renderModalShell({
      id: "system",
      titleId: "oq-login-modal-title",
      kicker: t("securityAccess.loginKicker"),
      title: getWebAuthModalTitle(),
      copy: getWebAuthModalCopy(),
      closeAction: "close-system-modal",
      closeLabel: t("securityAccess.loginClose"),
      body: `
          ${noticeMarkup}
          ${errorMarkup}
          <div class="oq-helper-modal-grid">
            ${renderLoginStatusRow(t("securityAccess.loginStatusRow"), getWebAuthStatusLabel(), getWebAuthStatusDetail())}
            ${renderLoginStatusRow(t("securityAccess.loginUserRow"), authEnabled ? (usernameValue || t("securityAccess.loginNoName")) : t("securityAccess.loginNoLogin"), authEnabled ? t("securityAccess.loginNameCopy") : t("securityAccess.loginNoLoginCopy"))}
          </div>
          ${authFormMarkup}`,
      actions: `
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${state.authBusy ? "disabled" : ""}>${escapeHtml(t("header.done"))}</button>
        ${authEnabled
              ? `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="disable-web-auth" ${state.authBusy ? "disabled" : ""}>${escapeHtml(t("securityAccess.loginDisable"))}</button>`
              : ""}
        ${canEdit
              ? `<button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="save-web-auth" ${state.authBusy ? "disabled" : ""}>${escapeHtml(authEnabled ? t("securityAccess.loginSave") : t("securityAccess.loginSaveNew"))}</button>`
              : ""}`,
    });
  }
