import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { state } from "../core/state.js";
import { renderSettingsSection } from "../settings/controls.js";
import { formatDateTime, t } from "../i18n/index.js";

// Firmware wire values. Keep these stable: they are stored on the device.
export const MAIL_KIND_KEYS = [
  "hp_fault",
  "hp_warning",
  "hp_link_loss",
  "hp_start_stop",
  "boiler_fallback",
  "frost_protection",
  "compressor_cycling",
  "controller_restart",
];
export const MAIL_PROVIDERS = ["none", "smtp", "postmark", "mailgun_eu", "mailgun_us", "lettermint", "resend", "brevo"];
const MAIL_DEFAULT_KINDS = ["hp_fault", "hp_link_loss", "hp_start_stop", "boiler_fallback", "controller_restart"];
export const MAIL_MAX_RECIPIENTS = 5;
const MAIL_RECIPIENTS_MAX_LENGTH = 255;
const MAIL_ADDRESS_PATTERN = /^[^\s@,;<>"]+@[^\s@,;<>"]+\.[^\s@,;<>"]+$/;

const text = (value) => (value === undefined || value === null ? "" : String(value));
const count = (value) => {
  const numeric = Number(value);
  return Number.isFinite(numeric) && numeric >= 0 ? Math.floor(numeric) : 0;
};
const section = (value) => (value && typeof value === "object" ? value : {});

export function normalizeMailStatus(payload = {}) {
  const rawKinds = section(payload.kinds);
  const kinds = {};
  MAIL_KIND_KEYS.forEach((key) => {
    kinds[key] = payload.kinds ? rawKinds[key] === true : MAIL_DEFAULT_KINDS.includes(key);
  });
  const port = count(payload.smtp_port);
  const save = section(payload.save);
  const test = section(payload.test);
  const last = section(payload.last);
  return {
    enabled: payload.enabled === true,
    configured: payload.configured === true,
    provider: MAIL_PROVIDERS.includes(payload.provider) ? payload.provider : "none",
    smtp_host: text(payload.smtp_host),
    smtp_port: port >= 1 && port <= 65535 ? port : 465,
    username: text(payload.username),
    secret_set: payload.secret_set === true,
    domain: text(payload.domain),
    from: text(payload.from),
    recipients: text(payload.recipients),
    kinds,
    recovery: payload.recovery !== false,
    save: { revision: count(save.revision), state: text(save.state), error: text(save.error) },
    test: { id: count(test.id), state: text(test.state), error: text(test.error), code: count(test.code) },
    last: { state: text(last.state), epoch: count(last.epoch), error: text(last.error), code: count(last.code) },
    queued: count(payload.queued),
    sent_24h: count(payload.sent_24h),
    daily_limit: count(payload.daily_limit),
    csrf_token: text(payload.csrf_token),
  };
}

export function getMailStatusSignature(status = state.mailStatus) {
  return status ? JSON.stringify(status) : "";
}

export function getMailDraftStateFromStatus(status) {
  const source = status || normalizeMailStatus({});
  return {
    mailDraftEnabled: source.enabled,
    mailDraftProvider: source.provider,
    mailDraftSmtpHost: source.smtp_host,
    mailDraftSmtpPort: String(source.smtp_port),
    mailDraftUsername: source.username,
    mailDraftSecret: "",
    mailDraftClearSecret: false,
    mailDraftDomain: source.domain,
    mailDraftFrom: source.from,
    mailDraftRecipients: source.recipients,
    mailDraftKinds: { ...source.kinds },
    mailDraftRecovery: source.recovery,
    mailDraftDirty: false,
  };
}

export function getMailDraft(source = state) {
  return {
    enabled: Boolean(source.mailDraftEnabled),
    provider: MAIL_PROVIDERS.includes(source.mailDraftProvider) ? source.mailDraftProvider : "none",
    smtpHost: text(source.mailDraftSmtpHost),
    smtpPort: text(source.mailDraftSmtpPort),
    username: text(source.mailDraftUsername),
    secret: text(source.mailDraftSecret),
    clearSecret: Boolean(source.mailDraftClearSecret),
    domain: text(source.mailDraftDomain),
    from: text(source.mailDraftFrom),
    recipients: text(source.mailDraftRecipients),
    kinds: { ...source.mailDraftKinds },
    recovery: Boolean(source.mailDraftRecovery),
  };
}

// Mirrors the firmware rules so obvious mistakes fail before a request. The
// firmware remains authoritative and returns the same error keys.
export function validateMailDraft(draft, status = null) {
  const smtp = draft.provider === "smtp";
  const port = draft.smtpPort.trim();
  const from = draft.from.trim();
  const recipients = draft.recipients.split(",").map((entry) => entry.trim()).filter(Boolean);
  const secretAvailable = Boolean(draft.secret) || (!draft.clearSecret && status?.secret_set === true);
  if (draft.recipients.length > MAIL_RECIPIENTS_MAX_LENGTH) return "field_too_long";
  if (smtp && (draft.enabled || port) && !(/^\d+$/.test(port) && port >= 1 && port <= 65535)) return "invalid_port";
  if (from && !MAIL_ADDRESS_PATTERN.test(from)) return "invalid_from";
  if (recipients.length > MAIL_MAX_RECIPIENTS) return "too_many_recipients";
  if (recipients.some((address) => !MAIL_ADDRESS_PATTERN.test(address))) return "invalid_recipients";
  if (!draft.enabled) return "";
  if (draft.provider === "none") return "invalid_provider";
  if (smtp && !draft.smtpHost.trim()) return "missing_host";
  if (draft.provider.startsWith("mailgun") && !draft.domain.trim()) return "missing_domain";
  if ((!smtp || draft.username.trim()) && !secretAvailable) return "missing_secret";
  if (!from) return "invalid_from";
  return recipients.length ? "" : "missing_recipients";
}

export function buildMailSaveParams(draft, csrfToken) {
  // API keys are often pasted with surrounding whitespace; SMTP passwords are sent verbatim.
  const secret = draft.clearSecret ? "" : draft.provider === "smtp" ? draft.secret : draft.secret.trim();
  return new URLSearchParams({
    csrf_token: text(csrfToken),
    enabled: String(draft.enabled),
    provider: draft.provider,
    smtp_host: draft.smtpHost.trim(),
    smtp_port: draft.smtpPort.trim(),
    username: draft.username.trim(),
    secret,
    clear_secret: String(draft.clearSecret),
    domain: draft.domain.trim(),
    from: draft.from.trim(),
    recipients: draft.recipients.trim(),
    kinds: MAIL_KIND_KEYS.filter((key) => draft.kinds[key] === true).join(","),
    recovery: String(draft.recovery),
  });
}

// A request counts as done only once the firmware reports a terminal state
// for this revision (or a newer one); anything else is still pending.
export function getMailSaveOutcome(status, revision) {
  const save = status?.save;
  if (!save || !revision || save.revision < revision) return "pending";
  return save.state === "stored" || save.state === "failed" ? save.state : "pending";
}

export function getMailTestOutcome(status, testId) {
  const test = status?.test;
  if (!test || !testId || test.id < testId) return "pending";
  return test.state === "sent" || test.state === "failed" ? test.state : "pending";
}

export function getMailProviderLabel(provider) {
  switch (provider) {
    case "smtp": return t("mail.providerSmtp");
    case "postmark": return "Postmark";
    case "mailgun_eu": return t("mail.providerMailgunEu");
    case "mailgun_us": return t("mail.providerMailgunUs");
    case "lettermint": return "Lettermint";
    case "resend": return "Resend";
    case "brevo": return "Brevo";
    default: return t("common.none");
  }
}

function getMailSecretLabel(provider) {
  switch (provider) {
    case "smtp": return t("mqtt.passLabel");
    case "postmark": return t("mail.secretServerToken");
    case "lettermint": return t("mail.secretProjectToken");
    default: return t("mail.secretApiKey");
  }
}

function getMailKindText(kind) {
  switch (kind) {
    case "hp_fault": return [t("mail.kindHpFault"), t("mail.kindHpFaultCopy")];
    case "hp_warning": return [t("mail.kindHpWarning"), t("mail.kindHpWarningCopy")];
    case "hp_link_loss": return [t("mail.kindHpLinkLoss"), t("mail.kindHpLinkLossCopy")];
    case "hp_start_stop": return [t("mail.kindHpStartStop"), t("mail.kindHpStartStopCopy")];
    case "boiler_fallback": return [t("mail.kindBoilerFallback"), t("mail.kindBoilerFallbackCopy")];
    case "frost_protection": return [t("mail.kindFrostProtection"), t("mail.kindFrostProtectionCopy")];
    case "compressor_cycling": return [t("mail.kindCompressorCycling"), t("mail.kindCompressorCyclingCopy")];
    default: return [t("mail.kindControllerRestart"), t("mail.kindControllerRestartCopy")];
  }
}

export function getMailSaveErrorMessage(code) {
  switch (code) {
    case "invalid_provider": return t("mail.errInvalidProvider");
    case "missing_host": return t("mail.errMissingHost");
    case "invalid_port": return t("mail.errInvalidPort");
    case "missing_secret": return t("mail.errMissingSecret");
    case "missing_domain": return t("mail.errMissingDomain");
    case "invalid_from": return t("mail.errInvalidFrom");
    case "missing_recipients": return t("mail.errMissingRecipients");
    case "invalid_recipients": return t("mail.errInvalidRecipients");
    case "too_many_recipients": return t("mail.errTooManyRecipients", { max: MAIL_MAX_RECIPIENTS });
    case "field_too_long": return t("mail.errFieldTooLong");
    case "forbidden": return t("mail.errForbidden");
    case "busy": return t("mail.errBusy");
    case "unavailable": return t("mail.detailUnavailable");
    default: return t("mail.errSaveGeneric", { error: code || "?" });
  }
}

export function getMailTestRequestErrorMessage(code) {
  switch (code) {
    case "not_configured": return t("mail.testNeedsConfig");
    case "save_pending":
    case "test_busy":
    case "rate_limited": return t("mail.testBusy");
    case "forbidden":
    case "busy":
    case "unavailable": return getMailSaveErrorMessage(code);
    default: return t("mail.testRequestFailed", { error: code || "?" });
  }
}

export function getMailSendErrorMessage(error, code = 0) {
  const numericCode = count(code);
  const vars = { code: numericCode ? ` (${numericCode})` : "" };
  switch (error) {
    case "cancelled":
    case "deadline": return t("mail.sendAborted");
    case "network_unavailable": return t("mail.sendNetwork");
    case "dns_failed": return t("mail.sendDns");
    case "tls_allocation_failed": return t("mail.sendTlsMemory");
    case "tls_connect_failed": return t("mail.sendTlsConnect");
    case "tls_write_failed":
    case "tls_read_failed": return t("mail.sendInterrupted");
    case "response_invalid": return t("mail.sendResponseInvalid");
    case "http_rejected":
      if (numericCode === 401 || numericCode === 403) return t("mail.sendHttpAuth", vars);
      if (numericCode === 422) return t("mail.sendHttpUnprocessable", vars);
      return numericCode === 429 ? t("mail.sendHttpRateLimited", vars) : t("mail.sendHttpRejected", vars);
    case "smtp_greeting":
    case "smtp_ehlo": return t("mail.sendSmtpConnect", vars);
    case "smtp_auth": return t("mail.sendSmtpAuth", vars);
    case "smtp_mail_from": return t("mail.sendSmtpFrom", vars);
    case "smtp_rcpt_to": return t("mail.sendSmtpRcpt", vars);
    case "smtp_data":
    case "smtp_message": return t("mail.sendSmtpMessage", vars);
    default: return t("mail.sendGeneric", { ...vars, error: error || "?" });
  }
}

export function getMailStatusLabel(status = state.mailStatus) {
  if (state.mailUnavailable) return t("common.unavailable");
  if (!status) return t("common.loading");
  if (status.enabled) return status.configured ? t("common.on") : t("mail.statusIncomplete");
  return status.configured ? t("common.off") : t("mqtt.statusUnset");
}

export function getMailStatusDetail(status = state.mailStatus) {
  if (state.mailUnavailable) return t("mail.detailUnavailable");
  if (!status) return state.mailError || t("mail.detailLoading");
  if (!status.enabled) return status.configured ? t("mail.detailOff") : t("mail.detailUnset");
  return status.configured
    ? t("mail.detailOn", { provider: getMailProviderLabel(status.provider), recipients: status.recipients })
    : t("mail.detailIncomplete");
}

export function getMailActivityDetail(status = state.mailStatus) {
  if (!status || state.mailUnavailable) return "";
  const { last } = status;
  const time = last.epoch
    ? formatDateTime(last.epoch * 1000, { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" })
    : t("common.unknown");
  return [
    last.state === "sent" ? t("mail.lastSent", { time }) : "",
    last.state === "failed" ? t("mail.lastFailed", { time, reason: getMailSendErrorMessage(last.error, last.code) }) : "",
    status.queued ? t("mail.queued", { count: status.queued }) : "",
    status.daily_limit && status.sent_24h >= status.daily_limit ? t("mail.limitReached", { limit: status.daily_limit }) : "",
  ].filter(Boolean).join(" ");
}

export function renderSettingsMailSection() {
  if (state.mailSupported === false) {
    return "";
  }
  const activity = getMailActivityDetail();
  return renderSettingsSection(
    t("settingsIntegrations.groupIntegrations"),
    t("mail.sectionTitle"),
    t("mail.sectionCopy"),
    `<div class="oq-settings-mqtt-shell"><section class="oq-settings-mqtt-panel oq-settings-mqtt-panel--broker"><div class="oq-settings-quickstart-status-row oq-settings-mqtt-status-row"><div><p class="oq-settings-quickstart-status-label">${escapeHtml(t("settingsIntegrations.mqttCurrentStatus"))}</p><strong class="oq-settings-quickstart-status-value">${escapeHtml(getMailStatusLabel())}</strong><p class="oq-settings-quickstart-status-copy">${escapeHtml(getMailStatusDetail())}</p>${activity ? `<p class="oq-settings-quickstart-status-copy">${escapeHtml(activity)}</p>` : ""}</div><button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="open-mail-modal" ${state.mailUnavailable ? "disabled" : ""}>${escapeHtml(t("mail.configure"))}</button></div></section></div>`,
  );
}

export function getMailTestBlockReason(status = state.mailStatus) {
  if (!status?.csrf_token) return t("mail.configLoading");
  if (state.mailDraftDirty) return t("mail.testNeedsSave");
  return status.configured ? "" : t("mail.testNeedsConfig");
}

function renderMailInput(field, label, value, { type = "text", disabled = false, extraClass = "", hint = "", attributes = "" } = {}) {
  return `<label class="oq-helper-modal-auth-field oq-settings-mqtt-field${extraClass}"><span>${escapeHtml(label)}</span><input class="oq-helper-input" type="${type}" data-oq-mail-field="${field}" value="${escapeHtml(value)}" autocomplete="off" ${attributes} ${disabled ? "disabled" : ""}>${hint ? `<small class="oq-helper-modal-subvalue">${escapeHtml(hint)}</small>` : ""}</label>`;
}

function renderMailCheckbox(field, label, checked, disabled, { copy = "", attributes = "", extraClass = "" } = {}) {
  return `<label class="oq-settings-mqtt-toggle${extraClass}"><input type="checkbox" data-oq-mail-field="${field}" ${attributes} ${checked ? "checked" : ""} ${disabled ? "disabled" : ""}><span>${escapeHtml(label)}${copy ? `<small class="oq-helper-modal-subvalue">${escapeHtml(copy)}</small>` : ""}</span></label>`;
}

export function renderMailModal() {
  const status = state.mailStatus || normalizeMailStatus({});
  const draft = getMailDraft();
  const busy = Boolean(state.mailBusy || state.mailPendingSave || state.mailPendingTest);
  const { provider } = draft;
  const smtp = provider === "smtp";
  const testBlockReason = getMailTestBlockReason(status);
  const progress = state.mailBusy === "save" || state.mailPendingSave
    ? t("mqtt.saving")
    : state.mailBusy === "test" || state.mailPendingTest ? t("mail.testSending") : "";
  const secretPlaceholder = status.secret_set && !draft.clearSecret ? t("mail.secretKeep") : smtp ? t("mqtt.passOptional") : "";
  const connectionFields = provider === "none"
    ? `<p class="oq-helper-modal-subvalue oq-settings-mail-wide">${escapeHtml(t("mail.providerChoose"))}</p>`
    : [
      smtp ? renderMailInput("smtp-host", t("mail.smtpHostLabel"), draft.smtpHost, { disabled: busy }) : "",
      smtp ? renderMailInput("smtp-port", t("mqtt.portLabel"), draft.smtpPort, { type: "number", disabled: busy, extraClass: " oq-settings-mqtt-field--port", attributes: 'min="1" max="65535" inputmode="numeric"' }) : "",
      smtp ? renderMailInput("username", t("mqtt.userLabel"), draft.username, { disabled: busy }) : "",
      provider.startsWith("mailgun") ? renderMailInput("domain", t("mail.domainLabel"), draft.domain, { disabled: busy }) : "",
      renderMailInput("secret", getMailSecretLabel(provider), draft.secret, { type: "password", disabled: busy || draft.clearSecret, attributes: secretPlaceholder ? `placeholder="${escapeHtml(secretPlaceholder)}"` : "" }),
      status.secret_set ? renderMailCheckbox("clear-secret", t("mail.clearSecret"), draft.clearSecret, busy) : "",
      renderMailInput("from", t("mail.fromLabel"), draft.from, { type: "email", disabled: busy, hint: t("mail.fromHint") }),
      renderMailInput("recipients", t("mail.recipientsLabel"), draft.recipients, { disabled: busy, hint: t("mail.recipientsHint", { max: MAIL_MAX_RECIPIENTS }), attributes: `maxlength="${MAIL_RECIPIENTS_MAX_LENGTH}"` }),
      `<fieldset class="oq-settings-mail-kinds"><legend>${escapeHtml(t("mail.kindsTitle"))}</legend>${MAIL_KIND_KEYS.map((kind) => {
        const [label, copy] = getMailKindText(kind);
        return renderMailCheckbox("kind", label, draft.kinds[kind] === true, busy, { copy, attributes: `data-oq-mail-kind="${kind}"`, extraClass: " oq-settings-mail-kind" });
      }).join("")}${renderMailCheckbox("recovery", t("mail.recoveryLabel"), draft.recovery, busy)}</fieldset>`,
    ].join("");

  return renderModalShell({
    id: "system",
    titleId: "oq-mail-modal-title",
    kicker: t("settingsIntegrations.groupIntegrations"),
    title: t("mail.sectionTitle"),
    copy: t("mail.modalCopy"),
    closeAction: "close-system-modal",
    closeLabel: t("mail.modalClose"),
    body: `${state.mailNotice ? `<div class="oq-helper-modal-success oq-helper-modal-success--compact" aria-live="polite"><strong>${escapeHtml(t("mqtt.modalStatus"))}</strong><span>${escapeHtml(state.mailNotice)}</span></div>` : ""}${state.mailError ? `<div class="oq-helper-modal-note oq-helper-modal-note--error" aria-live="assertive">${escapeHtml(state.mailError)}</div>` : ""}${progress ? `<div class="oq-helper-modal-note" aria-live="polite">${escapeHtml(progress)}</div>` : ""}<div class="oq-settings-mqtt-form-grid">${renderMailCheckbox("enabled", t("mail.enableLabel"), draft.enabled, busy)}<label class="oq-helper-modal-auth-field oq-settings-mqtt-field"><span>${escapeHtml(t("mail.providerLabel"))}</span><select class="oq-helper-select" data-oq-mail-field="provider" ${busy ? "disabled" : ""}>${MAIL_PROVIDERS.map((value) => `<option value="${value}" ${value === provider ? "selected" : ""}>${escapeHtml(getMailProviderLabel(value))}</option>`).join("")}</select></label>${connectionFields}<div class="oq-settings-mail-test-row"><button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="send-mail-test" ${busy || testBlockReason ? "disabled" : ""}>${escapeHtml(t("mail.testButton"))}</button><small class="oq-helper-modal-subvalue" data-oq-mail-test-hint>${escapeHtml(testBlockReason || t("mail.testHint"))}</small></div></div>`,
    actions: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal">${escapeHtml(t("header.done"))}</button><button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="save-mail-config" ${busy || !status.csrf_token ? "disabled" : ""}>${escapeHtml(t("common.save"))}</button>`,
  });
}
