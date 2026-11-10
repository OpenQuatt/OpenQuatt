import assert from "node:assert/strict";
import test from "node:test";
import { setLocale } from "../js/src/i18n/index.js";

globalThis.__OQ_PREVIEW__ = false;
const { state } = await import("../js/src/core/state.js");
const {
  buildMailSaveParams,
  getMailDraft,
  getMailDraftStateFromStatus,
  getMailSaveErrorMessage,
  getMailSaveOutcome,
  getMailSendErrorMessage,
  getMailTestOutcome,
  getMailTestRequestErrorMessage,
  MAIL_KIND_KEYS,
  normalizeMailStatus,
  validateMailDraft,
} = await import("../js/src/features/mail.js");
const { commitMailConfig, refreshMailStatus, sendMailTest, waitForMailOutcome } = await import("../js/src/features/mail-actions.js");

function draft(overrides = {}) {
  return {
    enabled: true,
    provider: "postmark",
    smtpHost: "",
    smtpPort: "465",
    username: "",
    secret: "",
    clearSecret: false,
    domain: "",
    from: "openquatt@example.com",
    recipients: "a@example.com, b@example.com",
    kinds: { hp_fault: true, hp_link_loss: true },
    recovery: true,
    ...overrides,
  };
}

function resetMailState() {
  Object.assign(state, getMailDraftStateFromStatus(null), {
    systemModal: "mail",
    mailStatus: null,
    mailSupported: null,
    mailUnavailable: false,
    mailBusy: "",
    mailNotice: "",
    mailError: "",
    mailPendingSave: null,
    mailPendingTest: null,
    mailLastStatusRefreshAt: 0,
  });
}

function setupFetch(t, handler) {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  t.after(() => {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
    state.systemModal = "";
  });
  globalThis.window = { setTimeout: (callback) => { callback(); return 0; }, clearTimeout() {} };
  globalThis.fetch = handler;
}

const response = (status, payload) => ({ ok: status >= 200 && status < 300, status, json: async () => payload });

test("status normalization keeps wire values and falls back to firmware defaults", () => {
  const empty = normalizeMailStatus({});
  assert.equal(empty.provider, "none");
  assert.equal(empty.smtp_port, 465);
  assert.equal(empty.recovery, true);
  assert.deepEqual(MAIL_KIND_KEYS.filter((key) => empty.kinds[key]), ["hp_fault", "hp_link_loss", "hp_start_stop", "boiler_fallback", "controller_restart"]);

  const status = normalizeMailStatus({
    enabled: true,
    configured: true,
    provider: "mailgun_eu",
    smtp_port: 99999,
    secret_set: true,
    recipients: "a@example.com, b@example.com",
    kinds: { hp_warning: true, hp_fault: "yes" },
    recovery: false,
    save: { revision: 4, state: "stored" },
    test: { id: "2", state: "failed", error: "smtp_auth", code: 535 },
    queued: -3,
    csrf_token: "tok",
  });
  assert.equal(status.provider, "mailgun_eu");
  assert.equal(status.smtp_port, 465);
  assert.equal(status.kinds.hp_warning, true);
  assert.equal(status.kinds.hp_fault, false);
  assert.equal(status.recovery, false);
  assert.deepEqual(status.test, { id: 2, state: "failed", error: "smtp_auth", code: 535 });
  assert.equal(status.queued, 0);
  assert.equal(normalizeMailStatus({ provider: "sendgrid" }).provider, "none");
});

test("save form lists enabled kinds and distinguishes keep versus clear secret", () => {
  const keep = buildMailSaveParams(draft({ recipients: " a@example.com,b@example.com " }), "csrf");
  assert.equal(keep.get("csrf_token"), "csrf");
  assert.equal(keep.get("enabled"), "true");
  assert.equal(keep.get("provider"), "postmark");
  assert.equal(keep.get("secret"), "");
  assert.equal(keep.get("clear_secret"), "false");
  assert.equal(keep.get("recipients"), "a@example.com,b@example.com");
  assert.equal(keep.get("kinds"), "hp_fault,hp_link_loss");
  assert.equal(keep.get("recovery"), "true");

  const apiKey = buildMailSaveParams(draft({ secret: "  key-123  " }), "csrf");
  assert.equal(apiKey.get("secret"), "key-123");
  const smtpPassword = buildMailSaveParams(draft({ provider: "smtp", secret: " pass " }), "csrf");
  assert.equal(smtpPassword.get("secret"), " pass ");

  const clear = buildMailSaveParams(draft({ secret: "ignored", clearSecret: true, kinds: {}, recovery: false }), "csrf");
  assert.equal(clear.get("secret"), "");
  assert.equal(clear.get("clear_secret"), "true");
  assert.equal(clear.get("kinds"), "");
  assert.equal(clear.get("recovery"), "false");
});

test("client validation mirrors the firmware error keys", () => {
  const stored = { secret_set: true };
  assert.equal(validateMailDraft(draft(), stored), "");
  assert.equal(validateMailDraft(draft(), {}), "missing_secret");
  assert.equal(validateMailDraft(draft({ clearSecret: true }), stored), "missing_secret");
  assert.equal(validateMailDraft(draft({ provider: "none" }), stored), "invalid_provider");
  assert.equal(validateMailDraft(draft({ provider: "mailgun_us" }), stored), "missing_domain");
  assert.equal(validateMailDraft(draft({ provider: "smtp" }), {}), "missing_host");
  assert.equal(validateMailDraft(draft({ provider: "smtp", smtpHost: "smtp.example.com", smtpPort: "0" }), {}), "invalid_port");
  assert.equal(validateMailDraft(draft({ provider: "smtp", smtpHost: "smtp.example.com" }), {}), "");
  assert.equal(validateMailDraft(draft({ provider: "smtp", smtpHost: "smtp.example.com", username: "me" }), {}), "missing_secret");
  assert.equal(validateMailDraft(draft({ recipients: "" }), stored), "missing_recipients");
  assert.equal(validateMailDraft(draft({ recipients: "a@example.com, nope" }), stored), "invalid_recipients");
  assert.equal(validateMailDraft(draft({ recipients: "a@x.nl,b@x.nl,c@x.nl,d@x.nl,e@x.nl,f@x.nl" }), stored), "too_many_recipients");
  assert.equal(validateMailDraft(draft({ from: "invalid" }), stored), "invalid_from");
  // Disabled drafts may be incomplete; only the format of filled-in fields is checked.
  assert.equal(validateMailDraft(draft({ enabled: false, provider: "none", from: "", recipients: "" }), {}), "");
  assert.equal(validateMailDraft(draft({ enabled: false, recipients: "nope" }), {}), "invalid_recipients");
});

test("error keys map to specific copy with a generic fallback", () => {
  setLocale("nl", { persist: false });
  assert.match(getMailSendErrorMessage("http_rejected", 401), /API-sleutel of token geweigerd \(401\)/);
  assert.match(getMailSendErrorMessage("http_rejected", 422), /afzenderdomein geverifieerd/);
  assert.match(getMailSendErrorMessage("smtp_auth", 535), /Gebruikersnaam of wachtwoord geweigerd \(535\)/);
  assert.match(getMailSendErrorMessage("tls_connect_failed", 0), /poort 465/);
  assert.equal(getMailSendErrorMessage("future_error", 0), "Verzending mislukt: future_error.");
  assert.equal(getMailSendErrorMessage("future_error", 7), "Verzending mislukt: future_error (7).");
  assert.match(getMailSaveErrorMessage("too_many_recipients"), /maximaal 5/);
  assert.match(getMailSaveErrorMessage("weird"), /weird/);
  assert.match(getMailTestRequestErrorMessage("save_pending"), /testmail of opslag/);
  setLocale("en", { persist: false });
  try {
    assert.equal(getMailSendErrorMessage("smtp_rcpt_to", 550), "Recipient rejected (550).");
  } finally {
    setLocale("nl", { persist: false });
  }
});

test("save and test outcomes complete only on a terminal state for the request", () => {
  const status = (save, testState) => normalizeMailStatus({ save, test: testState });
  assert.equal(getMailSaveOutcome(status({ revision: 2, state: "stored" }), 3), "pending");
  assert.equal(getMailSaveOutcome(status({ revision: 3, state: "pending" }), 3), "pending");
  assert.equal(getMailSaveOutcome(status({ revision: 3, state: "stored" }), 3), "stored");
  assert.equal(getMailSaveOutcome(status({ revision: 4, state: "failed" }), 3), "failed");
  assert.equal(getMailTestOutcome(status({}, { id: 1, state: "sent" }), 2), "pending");
  assert.equal(getMailTestOutcome(status({}, { id: 2, state: "sending" }), 2), "pending");
  assert.equal(getMailTestOutcome(status({}, { id: 2, state: "failed" }), 2), "failed");
});

test("outcome polling stops at the deadline or when the caller detaches", async () => {
  let clock = 0;
  let refreshes = 0;
  const pending = await waitForMailOutcome({
    outcome: () => "pending",
    deadlineAt: 3000,
    shouldContinue: () => true,
    refresh: async () => { refreshes += 1; },
    sleep: async (ms) => { clock += ms; },
    now: () => clock,
  });
  assert.equal(pending, "timeout");
  assert.equal(refreshes, 3);

  let follow = true;
  const detached = await waitForMailOutcome({
    outcome: () => "pending",
    deadlineAt: 10000,
    shouldContinue: () => follow,
    refresh: async () => { follow = false; },
    sleep: async () => {},
    now: () => 0,
  });
  assert.equal(detached, "detached");

  let calls = 0;
  const done = await waitForMailOutcome({
    outcome: () => (calls >= 2 ? "stored" : "pending"),
    deadlineAt: 10000,
    shouldContinue: () => true,
    refresh: async () => { calls += 1; },
    sleep: async () => {},
    now: () => 0,
  });
  assert.equal(done, "stored");
});

test("a status refresh does not overwrite dirty drafts in the open modal", async (t) => {
  resetMailState();
  setupFetch(t, async () => response(200, { enabled: true, configured: true, provider: "resend", recipients: "x@example.com", csrf_token: "tok" }));
  state.mailDraftDirty = true;
  state.mailDraftRecipients = "typed@example.com";
  assert.equal(await refreshMailStatus({ force: true }), true);
  assert.equal(state.mailDraftRecipients, "typed@example.com");
  state.mailDraftDirty = false;
  state.mailStatus = null;
  await refreshMailStatus({ force: true });
  assert.equal(state.mailDraftRecipients, "x@example.com");
  assert.equal(state.mailDraftProvider, "resend");
});

test("firmware without the endpoint hides the section", async (t) => {
  resetMailState();
  setupFetch(t, async () => response(404, {}));
  assert.equal(await refreshMailStatus({ force: true }), true);
  assert.equal(state.mailSupported, false);
});

test("save waits for the firmware to confirm the accepted revision", async (t) => {
  resetMailState();
  const requests = [];
  let polls = 0;
  setupFetch(t, async (url, options = {}) => {
    requests.push(`${options.method || "GET"} ${url}`);
    if (options.method === "POST") {
      const body = new URLSearchParams(options.body);
      assert.equal(body.get("kinds"), "hp_fault");
      assert.equal(body.get("csrf_token"), "tok");
      return response(202, { ok: true, revision: 5 });
    }
    polls += 1;
    return response(200, {
      enabled: true,
      configured: true,
      provider: "postmark",
      secret_set: true,
      from: "openquatt@example.com",
      recipients: "a@example.com",
      kinds: { hp_fault: true },
      save: { revision: polls >= 2 ? 5 : 4, state: "stored" },
      csrf_token: "tok",
    });
  });
  state.mailStatus = normalizeMailStatus({ provider: "postmark", secret_set: true, csrf_token: "tok", save: { revision: 4, state: "stored" } });
  Object.assign(state, {
    mailDraftEnabled: true,
    mailDraftProvider: "postmark",
    mailDraftFrom: "openquatt@example.com",
    mailDraftRecipients: "a@example.com",
    mailDraftKinds: { hp_fault: true },
    mailDraftDirty: true,
  });
  await commitMailConfig();
  assert.deepEqual(requests, ["POST /openquatt/mail/save", "GET /openquatt/mail/status", "GET /openquatt/mail/status"]);
  assert.equal(state.mailPendingSave, null);
  assert.equal(state.mailBusy, "");
  assert.equal(state.mailDraftDirty, false);
  assert.match(state.mailNotice, /staan aan/);
  assert.equal(getMailDraft().recipients, "a@example.com");
});

test("a rejected save shows the firmware reason and never polls", async (t) => {
  resetMailState();
  const requests = [];
  setupFetch(t, async (url, options = {}) => {
    requests.push(url);
    return response(409, { ok: false, error: "invalid_recipients" });
  });
  state.mailStatus = normalizeMailStatus({ secret_set: true, csrf_token: "tok" });
  Object.assign(state, { mailDraftEnabled: false, mailDraftProvider: "postmark", mailDraftRecipients: "a@example.com" });
  await commitMailConfig();
  assert.deepEqual(requests, ["/openquatt/mail/save"]);
  assert.match(state.mailError, /geen geldig e-mailadres/);
  assert.equal(state.mailPendingSave, null);
});

test("test mail is blocked for unsaved drafts and reports the send failure", async (t) => {
  resetMailState();
  let polls = 0;
  setupFetch(t, async (url, options = {}) => {
    if (options.method === "POST") return response(202, { ok: true, test_id: 3 });
    polls += 1;
    return response(200, {
      configured: true,
      csrf_token: "tok",
      test: polls >= 2 ? { id: 3, state: "failed", error: "smtp_auth", code: 535 } : { id: 3, state: "sending" },
    });
  });
  state.mailStatus = normalizeMailStatus({ configured: true, csrf_token: "tok" });
  state.mailDraftDirty = true;
  await sendMailTest();
  assert.match(state.mailError, /Sla je wijzigingen eerst op/);
  assert.equal(polls, 0);

  state.mailDraftDirty = false;
  await sendMailTest();
  assert.equal(state.mailPendingTest, null);
  assert.match(state.mailError, /Gebruikersnaam of wachtwoord geweigerd \(535\)/);
});

test("modal renders provider-specific fields and explains a blocked test mail", async () => {
  const { renderMailModal, renderSettingsMailSection } = await import("../js/src/features/mail.js");
  resetMailState();
  state.mailSupported = true;
  state.mailStatus = normalizeMailStatus({ enabled: true, configured: true, provider: "smtp", secret_set: true, recipients: "a@example.com", csrf_token: "tok" });
  Object.assign(state, getMailDraftStateFromStatus(state.mailStatus));
  let html = renderMailModal();
  assert.match(html, /data-oq-mail-field="smtp-host"/);
  assert.match(html, /data-oq-mail-field="clear-secret"/);
  assert.doesNotMatch(html, /data-oq-mail-field="domain"/);
  assert.match(html, /data-oq-mail-kind="hp_fault" checked/);
  assert.match(html, /data-oq-action="send-mail-test" >/);

  state.mailDraftProvider = "mailgun_eu";
  state.mailDraftDirty = true;
  html = renderMailModal();
  assert.match(html, /data-oq-mail-field="domain"/);
  assert.doesNotMatch(html, /data-oq-mail-field="smtp-host"/);
  assert.match(html, /data-oq-action="send-mail-test" disabled/);
  assert.match(html, /Sla je wijzigingen eerst op/);

  assert.match(renderSettingsMailSection(), /Via SMTP \(SSL\/TLS, poort 465\) naar a@example\.com/);
  state.mailSupported = false;
  assert.equal(renderSettingsMailSection(), "");
});
