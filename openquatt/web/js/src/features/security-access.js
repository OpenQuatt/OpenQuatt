import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";

  export function getWebAuthStatusLabel() {
    const authStatus = state.authStatus;
    if (!authStatus) {
      return "Laden...";
    }
    if (authStatus.enabled) {
      return authStatus.setup_window_active ? "Instelvenster" : "Beveiligd";
    }
    return "Onbeveiligd";
  }

  export function getWebAuthModalTitle() {
    return "Login";
  }

  export function getWebAuthModalCopy() {
    const authStatus = state.authStatus;
    if (!authStatus) {
      return "We halen de huidige loginstatus op.";
    }
    if (authStatus.enabled) {
      return "De web-app vraagt nu een login voordat beheer beschikbaar is. Je kunt die hier aanpassen of uitzetten.";
    }
    return "De web-app staat open op je netwerk. Houd de herstelknop 5 seconden vast en open de herstelpagina om een login toe te voegen.";
  }

  export function getWebAuthStatusDetail() {
    const authStatus = state.authStatus;
    if (!authStatus) {
      return "Logingegevens laden...";
    }
    if (authStatus.enabled) {
      return authStatus.setup_window_active
        ? "Login actief. Tijdelijk instelvenster is open."
        : `Login actief${authStatus.source ? ` via ${authStatus.source}` : ""}.`;
    }
    return authStatus.setup_window_active
      ? "Login uit. Tijdelijk instelvenster is open."
      : "Login uit. Webtoegang is open / onbeveiligd op het netwerk.";
  }

  export function getApiSecurityStatusLabel() {
    if (state.apiSecurityError) {
      return "Niet beschikbaar";
    }
    const status = state.apiSecurityStatus;
    if (!status) {
      return "Laden...";
    }
    if (status.transport_active === true) {
      return "Actief";
    }
    if (status.provisioning_closed === true) {
      return "Niet beschikbaar";
    }
    if (status.provisioning_pending === true) {
      return "Wacht op koppeling";
    }
    return "Niet beschikbaar";
  }

  export function getApiSecurityStatusDetail() {
    if (state.apiSecurityError) {
      return "De beveiligingsstatus kon niet worden opgehaald. Controleer de verbinding met het apparaat en probeer het opnieuw.";
    }
    const status = state.apiSecurityStatus;
    if (!status) {
      return "Beveiligde verbinding wordt gecontroleerd.";
    }
    if (status.transport_active === true) {
      return "De beveiliging voor Home Assistant is ingesteld.";
    }
    if (status.provisioning_pending === true) {
      return "Dit apparaat is nog niet gekoppeld. Na een opstart kan Home Assistant 10 minuten lang de beveiligde verbinding instellen. Daarna worden nieuwe koppelpogingen geweigerd.";
    }
    if (status.provisioning_closed === true) {
      return "De eerste koppeling is niet binnen 10 minuten gelukt. Zet het apparaat kort uit en weer aan om opnieuw te proberen.";
    }
    return "De beveiligde verbinding is tijdelijk niet beschikbaar.";
  }

  export function getApiSecurityModalTitle() {
    return "Beveiligde verbinding met Home Assistant";
  }

  export function getApiSecurityModalCopy() {
    return "Home Assistant regelt deze beveiliging automatisch. Je hoeft hier niets in te stellen.";
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
      kicker: "Toegang",
      title: getApiSecurityModalTitle(),
      copy: getApiSecurityModalCopy(),
      className: "oq-helper-modal--wide",
      closeAction: "close-system-modal",
      closeLabel: "Sluit API-beveiliging popup",
      body: `
        <div class="oq-settings-api-security-shell oq-settings-api-security-shell--modal">
          <div class="oq-helper-modal-grid">
            ${renderLoginStatusRow("Status", getApiSecurityStatusLabel(), getApiSecurityStatusDetail())}
            ${renderLoginStatusRow("Beheer", "Automatisch door Home Assistant", "De beveiligingssleutel wordt automatisch ingesteld en bewaard.")}
          </div>
        </div>`,
      actions: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal">Gereed</button>`,
    });
  }

  export function renderLoginModal() {
    const authStatus = state.authStatus || {};
    const authEnabled = authStatus.enabled === true;
    const canEdit = authEnabled;
    const usernameValue = authEnabled ? String(authStatus.username || "").trim() : "";
    const noticeMarkup = state.authNotice
      ? `<div class="oq-helper-modal-success oq-helper-modal-success--compact" aria-live="polite"><strong>Opgeslagen</strong><span>${escapeHtml(state.authNotice)}</span></div>`
      : "";
    const errorMarkup = state.authError
      ? `<div class="oq-helper-modal-note oq-helper-modal-note--error" aria-live="assertive">${escapeHtml(state.authError)}</div>`
      : "";
    const authFormIntro = canEdit
      ? `<p class="oq-helper-modal-intro">${authEnabled ? "Pas hier je login aan." : "Vul hier je nieuwe login in."}</p>`
      : "";
    const authFormMarkup = canEdit
      ? `
        ${authFormIntro}
        <div class="oq-helper-modal-auth-stack">
          ${authEnabled
            ? `
              <label class="oq-helper-modal-auth-field">
                <span>Huidig wachtwoord</span>
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
            <span>Nieuwe gebruikersnaam</span>
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
            <span>Nieuw wachtwoord</span>
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
            <span>Herhaal nieuw wachtwoord</span>
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
          <strong>Login toevoegen</strong>
          <span>Houd de herstelknop 5 seconden vast en laat hem los. Stel daarna je login in op de <a href="/recovery">herstelpagina</a>.</span>
        </div>
      `;

    return renderModalShell({
      id: "system",
      titleId: "oq-login-modal-title",
      kicker: "Systeem",
      title: getWebAuthModalTitle(),
      copy: getWebAuthModalCopy(),
      closeAction: "close-system-modal",
      closeLabel: "Sluit login-popup",
      body: `
          ${noticeMarkup}
          ${errorMarkup}
          <div class="oq-helper-modal-grid">
            ${renderLoginStatusRow("Beveiligingsstatus", getWebAuthStatusLabel(), getWebAuthStatusDetail())}
            ${renderLoginStatusRow("Gebruiker", authEnabled ? (usernameValue || "Geen naam") : "Geen login", authEnabled ? "Deze naam gebruik je om in te loggen." : "Er staat nog geen login op het device.")}
          </div>
          ${authFormMarkup}`,
      actions: `
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${state.authBusy ? "disabled" : ""}>Gereed</button>
        ${authEnabled
              ? `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="disable-web-auth" ${state.authBusy ? "disabled" : ""}>Uitzetten</button>`
              : ""}
        ${canEdit
              ? `<button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="save-web-auth" ${state.authBusy ? "disabled" : ""}>${authEnabled ? "Opslaan" : "Login opslaan"}</button>`
              : ""}`,
    });
  }
