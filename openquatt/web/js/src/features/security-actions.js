import { invokeActionMap } from "../core/action-router.js";
import { LOGIN_MODAL_AUTH_STATUS_REFRESH_INTERVAL_MS } from "../core/config.js";
import { state } from "../core/state.js";
import { shouldRefreshSupplementaryStatus } from "../core/supplementary-refresh.js";
import { isSystemSettingsGroupActive } from "../core/surface-state.js";
import { render } from "../core/render-scheduler.js";

  export function getAuthStatusSignature(status = state.authStatus || {}) {
    return [
      status.enabled ? "on" : "off",
      status.setup_window_active ? "armed" : "locked",
      String(status.username || ""),
      String(status.source || ""),
      String(status.csrf_token || ""),
    ].join(":");
  }

  export function syncAuthDraftsFromStatus() {
    const status = state.authStatus || {};
    state.authDraftUsername = status.enabled ? String(status.username || "").trim() : "";
    state.authDraftCurrentPassword = "";
    state.authDraftNewPassword = "";
    state.authDraftConfirmPassword = "";
  }

  export function getApiSecurityStatusSignature(status = state.apiSecurityStatus || {}) {
    return [
      status.transport_active ? "active" : "idle",
      status.key_present ? "has-key" : "no-key",
      status.provisioning_pending ? "pending" : "settled",
      status.provisioning_closed ? "closed" : "open",
    ].join(":");
  }

  export function shouldRefreshAuthStatusForCurrentSurface() {
    return state.systemModal === "login" || state.systemModal === "api-security" || isSystemSettingsGroupActive();
  }

  export function shouldRefreshApiSecurityStatusForCurrentSurface() {
    return state.systemModal === "api-security" || isSystemSettingsGroupActive();
  }

  export async function refreshAuthStatus(options = {}) {
    if (!shouldRefreshSupplementaryStatus(state.lastAuthStatusRefreshAt, options)) {
      return false;
    }
    state.lastAuthStatusRefreshAt = Date.now();
    try {
      const response = await fetch("/auth/status", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const payload = await response.json();
      const nextStatus = {
        enabled: Boolean(payload.enabled),
        setup_window_active: Boolean(payload.setup_window_active),
        username: String(payload.username || ""),
        source: String(payload.source || ""),
        csrf_token: String(payload.csrf_token || ""),
      };
      const previousSignature = getAuthStatusSignature();
      const nextSignature = getAuthStatusSignature(nextStatus);
      state.authStatus = nextStatus;
      if (previousSignature !== nextSignature) {
        syncAuthDraftsFromStatus();
      }
      if (state.systemModal === "login") {
        state.authError = "";
      }
      return previousSignature !== nextSignature;
    } catch (error) {
      if (state.systemModal === "login") {
        state.authError = `Loginstatus kon niet worden geladen. ${error.message}`;
      }
      return false;
    }
  }

  export function shouldPollLoginAuthStatus() {
    if (state.nativeOpen || state.systemModal !== "login") {
      return false;
    }
    const status = state.authStatus || {};
    return status.setup_window_active !== true;
  }

  export function stopLoginAuthStatusPolling() {
    if (!state.loginAuthStatusPollTimer) {
      return;
    }
    window.clearTimeout(state.loginAuthStatusPollTimer);
    state.loginAuthStatusPollTimer = null;
  }

  export function scheduleLoginAuthStatusPolling(delayMs = LOGIN_MODAL_AUTH_STATUS_REFRESH_INTERVAL_MS) {
    if (state.loginAuthStatusPollTimer || !shouldPollLoginAuthStatus()) {
      return;
    }

    state.loginAuthStatusPollTimer = window.setTimeout(async () => {
      state.loginAuthStatusPollTimer = null;
      if (!shouldPollLoginAuthStatus()) {
        return;
      }
      const previousAuthError = state.authError;
      const changed = await refreshAuthStatus({ force: true });
      if ((changed || state.authError !== previousAuthError) && state.systemModal === "login") {
        render();
      }
      if (shouldPollLoginAuthStatus()) {
        scheduleLoginAuthStatusPolling();
      }
    }, Math.max(0, Number(delayMs) || 0));
  }

  export async function refreshLoginModalAuthStatus(options = {}) {
    if (state.systemModal !== "login") {
      return false;
    }
    const previousAuthError = state.authError;
    const changed = await refreshAuthStatus({ force: true });
    if ((changed || state.authError !== previousAuthError) && state.systemModal === "login") {
      render();
    }
    if (options.poll !== false && shouldPollLoginAuthStatus()) {
      scheduleLoginAuthStatusPolling();
    }
    return changed;
  }

  export async function refreshApiSecurityStatus(options = {}) {
    if (!shouldRefreshSupplementaryStatus(state.lastApiSecurityStatusRefreshAt, options)) {
      return false;
    }
    state.lastApiSecurityStatusRefreshAt = Date.now();
    try {
      const response = await fetch("/api-security/status", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const payload = await response.json();
      const nextStatus = {
        transport_active: Boolean(payload.transport_active),
        key_present: Boolean(payload.key_present),
        provisioning_pending: Boolean(payload.provisioning_pending),
        provisioning_closed: Boolean(payload.provisioning_closed),
      };
      const previousSignature = getApiSecurityStatusSignature();
      const nextSignature = getApiSecurityStatusSignature(nextStatus);
      state.apiSecurityStatus = nextStatus;
      state.apiSecurityError = "";
      if (previousSignature !== nextSignature) {
        state.apiSecurityNotice = "";
      }
      return previousSignature !== nextSignature;
    } catch (error) {
      state.apiSecurityError = `API-beveiliging kon niet worden geladen. ${error.message}`;
      if (state.systemModal === "api-security") {
        render();
      }
      return false;
    }
  }

  export async function commitWebAuthChanges() {
    const status = state.authStatus || {};
    const authEnabled = status.enabled === true;
    const setupWindowActive = status.setup_window_active === true;
    const currentPassword = String(state.authDraftCurrentPassword || "");
    const newUsername = String(state.authDraftUsername || "").trim();
    const newPassword = String(state.authDraftNewPassword || "");
    const confirmPassword = String(state.authDraftConfirmPassword || "");

    if (!newUsername || !newPassword) {
      state.authError = "Vul een gebruikersnaam en wachtwoord in.";
      render();
      return;
    }
    if (newPassword !== confirmPassword) {
      state.authError = "De twee wachtwoorden zijn niet gelijk.";
      render();
      return;
    }
    if (authEnabled && !currentPassword) {
      state.authError = "Vul je huidige wachtwoord in.";
      render();
      return;
    }
    if (!authEnabled && !setupWindowActive) {
      state.authError = "Houd de herstelknop 5 seconden vast.";
      render();
      return;
    }
    if (!status.csrf_token) {
      state.authError = "Logingegevens laden nog. Probeer het zo opnieuw.";
      render();
      return;
    }

    state.authBusy = true;
    state.authError = "";
    state.authNotice = "";
    render();

    try {
      const params = new URLSearchParams();
      params.set("csrf_token", status.csrf_token);
      params.set("current_password", currentPassword);
      params.set("new_username", newUsername);
      params.set("new_password", newPassword);

      const response = await fetch("/auth/change", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body: params.toString(),
      });
      const payload = await response.json().catch(() => ({ ok: false, error: "invalid_response" }));
      if (!response.ok || !payload.ok) {
        throw new Error(payload.error || `HTTP ${response.status}`);
      }
      await refreshAuthStatus({ force: true });
      state.authDraftCurrentPassword = "";
      state.authDraftNewPassword = "";
      state.authDraftConfirmPassword = "";
      state.authDraftUsername = String(state.authStatus?.username || newUsername).trim();
      state.authNotice = authEnabled
        ? "Login aangepast."
        : "Login staat nu aan.";
      state.authError = "";
      render();
    } catch (error) {
      state.authError = `Opslaan is mislukt. ${error.message}`;
      render();
    } finally {
      state.authBusy = false;
      render();
    }
  }

  export async function commitDisableWebAuth() {
    const status = state.authStatus || {};
    if (!status.enabled) {
      state.authNotice = "Login staat al uit.";
      state.authError = "";
      render();
      return;
    }

    const currentPassword = String(state.authDraftCurrentPassword || "");
    if (!currentPassword) {
      state.authError = "Vul je huidige wachtwoord in.";
      render();
      return;
    }
    if (!status.csrf_token) {
      state.authError = "Logingegevens laden nog. Probeer het zo opnieuw.";
      render();
      return;
    }

    state.authBusy = true;
    state.authError = "";
    state.authNotice = "";
    render();

    try {
      const params = new URLSearchParams();
      params.set("csrf_token", status.csrf_token);
      params.set("current_password", currentPassword);

      const response = await fetch("/auth/disable", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body: params.toString(),
      });
      const payload = await response.json().catch(() => ({ ok: false, error: "invalid_response" }));
      if (!response.ok || !payload.ok) {
        throw new Error(payload.error || `HTTP ${response.status}`);
      }
      await refreshAuthStatus({ force: true });
      state.authDraftCurrentPassword = "";
      state.authDraftNewPassword = "";
      state.authDraftConfirmPassword = "";
      state.authDraftUsername = "";
      state.authNotice = "Login staat nu uit.";
      state.authError = "";
      render();
    } catch (error) {
      state.authError = `Uitzetten is mislukt. ${error.message}`;
      render();
    } finally {
      state.authBusy = false;
      render();
    }
  }

  async function resetCredentials(wifi) {
    if (state.apiSecurityBusy || state.wifiResetBusy || !state.authStatus?.enabled || !state.authStatus?.csrf_token) return;
    if (wifi && !state.wifiResetAvailable) return;
    if (!window.confirm(wifi
      ? "Wi-Fi-gegevens wissen en controller herstarten? Stel daarna Wi-Fi opnieuw in via het OpenQuatt access point."
      : "API-beveiliging wissen en controller herstarten? Dit verbreekt alle API-koppelingen.")) return;
    const prefix = wifi ? "wifiReset" : "apiSecurity";
    state[`${prefix}Busy`] = true;
    state[`${prefix}Error`] = "";
    state[`${prefix}ActionError`] = "";
    state[`${prefix}Notice`] = "Reset aanvragen…";
    render();
    try {
      const body = new URLSearchParams({ csrf_token: state.authStatus.csrf_token, confirm: wifi ? "RESET_WIFI" : "RESET_API_SECURITY" });
      const response = await fetch(wifi ? "/wifi/reset" : "/api-security/reset", { method: "POST", body });
      if (response.status !== 202) {
        state[`${prefix}Busy`] = false;
        state[`${prefix}Notice`] = "";
        state[`${prefix}ActionError`] = `Reset is afgewezen. HTTP ${response.status}. Je kunt opnieuw proberen.`;
        render();
        return;
      }
      state[`${prefix}Notice`] = wifi
        ? "Reset aangevraagd. Verbind na de herstart met het OpenQuatt access point en stel Wi-Fi opnieuw in. Web-login en API-beveiliging blijven behouden."
        : "Reset aangevraagd. Bij succes herstart de controller. Open daarna de web-app opnieuw en koppel Home Assistant binnen 10 minuten.";
      render();
      await new Promise(resolve => window.setTimeout(resolve, 1000));
      // One status check, never retry a destructive request after an ambiguous response.
      const result = await fetch("/recovery/status", { cache: "no-store" }).then(response => response.json());
      if (result.error) {
        state[`${prefix}Busy`] = false;
        state[`${prefix}Notice`] = "";
        state[`${prefix}ActionError`] = "Reset mislukt; er is niet herstart. Je kunt opnieuw proberen.";
      }
    } catch (error) {
      state[`${prefix}Notice`] = "Controleer of de controller is herstart en open de web-app opnieuw. De reset wordt niet automatisch herhaald.";
    }
    render();
  }

  export function resetApiSecurity() { return resetCredentials(false); }
  export function resetWifi() { return resetCredentials(true); }

  const securityActionHandlers = {
    "open-login-modal": () => {
      state.systemModal = "login";
      syncAuthDraftsFromStatus();
      state.authNotice = "";
      state.authError = "";
      render();
      return refreshLoginModalAuthStatus({ poll: true });
    },
    "open-api-security-modal": async () => {
      state.systemModal = "api-security";
      state.apiSecurityNotice = "";
      state.apiSecurityError = "";
      render();
      await refreshApiSecurityStatus({ force: true });
      if (state.systemModal === "api-security") {
        render();
      }
    },
    "save-web-auth": () => commitWebAuthChanges(),
    "disable-web-auth": () => commitDisableWebAuth(),
    "reset-api-security": () => resetApiSecurity(),
    "reset-wifi": () => resetWifi(),
  };

  export function handleSecurityAction(action) {
    return invokeActionMap(securityActionHandlers, action);
  }
