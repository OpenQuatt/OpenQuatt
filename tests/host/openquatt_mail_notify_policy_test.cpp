#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

#include "components/openquatt_mail_notify/OpenQuattMailNotifyPolicy.h"
#include "components/openquatt_mail_notify/OpenQuattMailNotifyText.h"

using namespace oq_mail;

namespace {

bool contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

std::string canonical(const char* input, ValidationError expected = ValidationError::NONE, size_t* count = nullptr) {
  char out[kRecipientsMax + 1];
  size_t parsed = 0;
  assert(canonicalize_recipients(input, out, sizeof(out), parsed) == expected);
  if (count != nullptr) *count = parsed;
  return expected == ValidationError::NONE ? std::string(out) : std::string();
}

Config complete_api_config(Provider provider) {
  Config config = default_config();
  config.enabled = 1U;
  config.provider = static_cast<uint8_t>(provider);
  std::strcpy(config.secret, "token-123");
  std::strcpy(config.from, "openquatt@example.nl");
  std::strcpy(config.recipients, "a@example.nl, b@example.nl");
  if (is_mailgun(provider)) std::strcpy(config.domain, "mg.example.nl");
  seal(config);
  return config;
}

std::string build_request(Provider provider, const Config& config, const Message& message) {
  static char buffer[8192];
  Buffer out(buffer, sizeof(buffer));
  assert(build_api_request(out, provider, config.secret, config.domain, message));
  return std::string(buffer, out.length());
}

size_t declared_content_length(const std::string& request) {
  const size_t header = request.find("Content-Length: ");
  assert(header != std::string::npos);
  return static_cast<size_t>(std::stoul(request.substr(header + 16)));
}

size_t actual_body_length(const std::string& request) {
  const size_t split = request.find("\r\n\r\n");
  assert(split != std::string::npos);
  return request.size() - split - 4;
}

std::string decode_base64(const std::string& encoded) {
  static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  uint32_t buffer = 0;
  int bits = 0;
  for (char c : encoded) {
    if (c == '=' || c == '\r' || c == '\n') continue;
    const size_t value = alphabet.find(c);
    assert(value != std::string::npos);
    buffer = (buffer << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
    }
  }
  return out;
}

}  // namespace

int main() {
  // Stored keys are the API contract and must remain stable.
  Provider provider = Provider::NONE;
  assert(parse_provider("mailgun_eu", provider) && provider == Provider::MAILGUN_EU);
  assert(parse_provider("lettermint", provider) && provider == Provider::LETTERMINT);
  assert(!parse_provider("sendmail", provider));
  for (uint8_t value = 0; value < kProviderCount; ++value) {
    Provider parsed = Provider::NONE;
    assert(parse_provider(provider_key(static_cast<Provider>(value)), parsed));
    assert(static_cast<uint8_t>(parsed) == value);
  }

  uint16_t mask = 0xFFFF;
  assert(parse_kind_list("", mask) && mask == 0U);
  assert(parse_kind_list("hp_fault,boiler_fallback", mask));
  assert(mask == (kind_bit(Kind::HP_FAULT) | kind_bit(Kind::BOILER_FALLBACK)));
  assert(parse_kind_list(" hp_warning , controller_restart ", mask));
  assert(mask == (kind_bit(Kind::HP_WARNING) | kind_bit(Kind::CONTROLLER_RESTART)));
  assert(!parse_kind_list("hp_fault,unknown", mask));
  assert(!parse_kind_list("hp_faul", mask));

  // Addresses: plain ASCII only; display names and quoting are rejected.
  assert(valid_email("dennis@example.nl"));
  assert(valid_email("first.last+tag@sub.example.co.uk"));
  assert(!valid_email("no-at.example.nl"));
  assert(!valid_email("a@b"));
  assert(!valid_email("a@@example.nl"));
  assert(!valid_email(".a@example.nl"));
  assert(!valid_email("a..b@example.nl"));
  assert(!valid_email("Dennis <d@example.nl>"));
  assert(!valid_email("d@example.nl\r\nBcc: x@evil.nl"));
  assert(!valid_email("d@-example.nl"));

  size_t count = 0;
  assert(canonical(" a@x.nl ,b@y.nl;; A@X.NL ", ValidationError::NONE, &count) == "a@x.nl, b@y.nl");
  assert(count == 2);
  assert(canonical("", ValidationError::NONE, &count).empty() && count == 0);
  canonical("a@x.nl, nope", ValidationError::INVALID_RECIPIENTS);
  canonical("a@x.nl,b@x.nl,c@x.nl,d@x.nl,e@x.nl,f@x.nl", ValidationError::TOO_MANY_RECIPIENTS);
  assert(canonical("a@x.nl,b@x.nl,c@x.nl,d@x.nl,e@x.nl", ValidationError::NONE, &count).size() > 0 && count == 5);
  size_t visited = 0;
  for_each_recipient("a@x.nl, b@y.nl", [&](const char* address, size_t length) {
    assert(length == 6);
    assert(std::strncmp(address, visited == 0 ? "a@x.nl" : "b@y.nl", length) == 0);
    ++visited;
    return true;
  });
  assert(visited == 2);

  // Defaults: disabled, nothing configured, faults on, warnings off.
  Config config = default_config();
  assert(valid_config_record(config));
  assert(config.smtp_port == 465U);
  assert(config.recovery == 1U);
  assert((config.kind_mask & kind_bit(Kind::HP_FAULT)) != 0U);
  assert((config.kind_mask & kind_bit(Kind::HP_WARNING)) == 0U);
  assert(validate(config, false) == ValidationError::NONE);
  assert(validate(config, true) == ValidationError::INVALID_PROVIDER);
  assert(!config_complete(config));

  // Integrity: a flipped byte or missing terminator invalidates the record.
  Config corrupt = config;
  corrupt.smtp_port = 25U;
  assert(!valid_config_record(corrupt));
  corrupt = config;
  std::memset(corrupt.from, 'x', sizeof(corrupt.from));
  seal(corrupt);
  assert(!valid_config_record(corrupt));

  // Complete configurations per provider.
  for (Provider api : {Provider::POSTMARK, Provider::MAILGUN_EU, Provider::MAILGUN_US, Provider::LETTERMINT,
                       Provider::RESEND, Provider::BREVO}) {
    Config api_config = complete_api_config(api);
    assert(config_complete(api_config));
    api_config.secret[0] = '\0';
    assert(validate(api_config, true) == ValidationError::MISSING_SECRET);
    assert(validate(api_config, false) == ValidationError::NONE);
  }
  Config mailgun = complete_api_config(Provider::MAILGUN_EU);
  mailgun.domain[0] = '\0';
  assert(validate(mailgun, true) == ValidationError::MISSING_DOMAIN);
  std::strcpy(mailgun.domain, "not a domain");
  assert(validate(mailgun, false) == ValidationError::INVALID_DOMAIN);
  // The web app always posts every field: another provider's drafts are kept
  // but never block a save.
  Config stale_drafts = complete_api_config(Provider::POSTMARK);
  std::strcpy(stale_drafts.smtp_host, "half typed_host");
  std::strcpy(stale_drafts.domain, "not a domain");
  stale_drafts.smtp_port = 0U;
  assert(config_complete(stale_drafts));
  Config token_with_space = complete_api_config(Provider::POSTMARK);
  std::strcpy(token_with_space.secret, "abc def");
  assert(validate(token_with_space, false) == ValidationError::INVALID_SECRET);
  Config header_injection = complete_api_config(Provider::RESEND);
  std::strcpy(header_injection.secret, "abc\r\nX-Evil: 1");
  assert(validate(header_injection, false) == ValidationError::INVALID_SECRET);
  Config no_recipients = complete_api_config(Provider::LETTERMINT);
  no_recipients.recipients[0] = '\0';
  assert(validate(no_recipients, true) == ValidationError::MISSING_RECIPIENTS);
  Config no_from = complete_api_config(Provider::LETTERMINT);
  no_from.from[0] = '\0';
  assert(validate(no_from, true) == ValidationError::MISSING_FROM);
  assert(std::strcmp(validation_error_key(ValidationError::MISSING_FROM), "invalid_from") == 0);

  Config smtp = default_config();
  smtp.enabled = 1U;
  smtp.provider = static_cast<uint8_t>(Provider::SMTP);
  std::strcpy(smtp.from, "openquatt@example.nl");
  std::strcpy(smtp.recipients, "a@example.nl");
  assert(validate(smtp, true) == ValidationError::MISSING_HOST);
  std::strcpy(smtp.smtp_host, "smtp.example.nl");
  assert(config_complete(smtp));  // Unauthenticated relay is allowed.
  std::strcpy(smtp.username, "user@example.nl");
  assert(validate(smtp, true) == ValidationError::MISSING_SECRET);
  std::strcpy(smtp.secret, "pass word with spaces");
  assert(config_complete(smtp));
  std::strcpy(smtp.smtp_host, "smtp_example.nl");
  assert(validate(smtp, false) == ValidationError::INVALID_HOST);
  std::strcpy(smtp.smtp_host, "smtp.example.nl");
  smtp.smtp_port = 0U;
  assert(validate(smtp, false) == ValidationError::INVALID_PORT);

  char field[8];
  assert(copy_field(field, "1234567", 7) && std::strcmp(field, "1234567") == 0);
  assert(!copy_field(field, "12345678", 8));
  assert(copy_field(field, nullptr, 0) && field[0] == '\0');

  // Classification: faults, warnings, synthetic incidents and system events.
  Notice notice;
  EventInput fault{};
  fault.event_type = event::INCIDENT_START;
  fault.subject = subject::HP1;
  fault.reason_code = reason::HP_FAULT;
  fault.severity = severity::FAULT;
  fault.value_a = 5;  // R2119.b4 high_pressure_switch
  fault.flags = flag::MANUAL_RESET_REQUIRED;
  assert(classify(fault, notice));
  assert(notice.kind == static_cast<uint8_t>(Kind::HP_FAULT) && !notice.recovery);
  EventInput cleared = fault;
  cleared.event_type = event::INCIDENT_CLEAR;
  cleared.duration_s = 720;
  assert(classify(cleared, notice) && notice.recovery && notice.duration_s == 720);

  EventInput restored = fault;
  restored.flags = flag::RESTORED_AFTER_BOOT;
  assert(!classify(restored, notice));

  // The raw bit dropped but the power-cycle latch still blocks the heat pump.
  EventInput still_latched = cleared;
  still_latched.flags = flag::LATCHED | flag::MANUAL_RESET_REQUIRED;
  assert(!classify(still_latched, notice));
  // A display latch on an auto-clearing incident is a real recovery.
  EventInput display_latch = cleared;
  display_latch.flags = flag::LATCHED;
  assert(classify(display_latch, notice) && notice.recovery);

  EventInput limit = fault;
  limit.severity = severity::LIMITED;
  limit.reason_code = reason::HP_PROTECTION;
  assert(classify(limit, notice) && notice.kind == static_cast<uint8_t>(Kind::HP_WARNING));
  EventInput preheat = limit;
  preheat.reason_code = reason::HP_PREHEAT;
  assert(!classify(preheat, notice));
  EventInput status = fault;
  status.severity = severity::NORMAL;
  assert(!classify(status, notice));

  EventInput link = fault;
  link.reason_code = reason::HP_LINK_LOSS;
  assert(classify(link, notice) && notice.kind == static_cast<uint8_t>(Kind::HP_LINK_LOSS));
  EventInput start_failed = fault;
  start_failed.reason_code = reason::HP_START_FAILED;
  assert(classify(start_failed, notice) && notice.kind == static_cast<uint8_t>(Kind::HP_START_STOP));
  start_failed.reason_code = reason::HP_STOP_UNCONFIRMED;
  assert(classify(start_failed, notice) && notice.kind == static_cast<uint8_t>(Kind::HP_START_STOP));

  EventInput fallback{};
  fallback.event_type = event::BOILER_FALLBACK_START;
  fallback.subject = subject::CV;
  assert(classify(fallback, notice) && notice.kind == static_cast<uint8_t>(Kind::BOILER_FALLBACK));
  fallback.event_type = event::BOILER_FALLBACK_STOP;
  assert(classify(fallback, notice) && notice.recovery);
  EventInput frost{};
  frost.event_type = event::FROST_PROTECTION_START;
  assert(classify(frost, notice) && notice.kind == static_cast<uint8_t>(Kind::FROST_PROTECTION));
  EventInput cycling{};
  cycling.event_type = event::ATTENTION_PATTERN;
  cycling.reason_code = reason::START_STOP_RATE_HIGH;
  cycling.subject = subject::HP1;
  cycling.value_a = 9;
  cycling.threshold_a = 6;
  assert(classify(cycling, notice) && notice.kind == static_cast<uint8_t>(Kind::COMPRESSOR_CYCLING));
  EventInput acknowledged = fault;
  acknowledged.event_type = 25;  // EVENT_INCIDENT_ACKNOWLEDGED
  assert(!classify(acknowledged, notice));
  EventInput availability = fault;
  availability.event_type = 26;  // EVENT_HP_AVAILABILITY_CHANGE duplicates the incident itself
  assert(!classify(availability, notice));

  // Selection honours the kind mask and the recovery toggle.
  assert(classify(fault, notice));
  assert(selected(notice, kDefaultKindMask, false));
  assert(!selected(notice, kind_bit(Kind::HP_WARNING), true));
  assert(classify(cleared, notice));
  assert(selected(notice, kDefaultKindMask, true));
  assert(!selected(notice, kDefaultKindMask, false));

  // Delivery policy.
  assert(retry_delay_ms(0) == 0U);
  assert(retry_delay_ms(1) == 60000U);
  assert(retry_delay_ms(2) == 120000U);
  assert(retry_delay_ms(5) == 960000U);
  assert(retry_delay_ms(6) == 1800000U);
  assert(retry_delay_ms(200) == 1800000U);
  SendBudget budget;
  const uint32_t day = kDaySeconds;
  for (uint32_t index = 0; index < kDailyLimit; ++index) {
    assert(budget.allowed(1000U + index));
    budget.record(1000U + index);
  }
  assert(!budget.allowed(2000U));
  assert(budget.used(2000U) == kDailyLimit);
  assert(budget.allowed(1000U + day));  // The oldest send has aged out.
  budget.record(1000U + day);
  assert(!budget.allowed(1000U + day));
  assert(budget.allowed(1001U + day));  // The send at t=1001 has aged out as well.
  // A clock that jumped back must not turn future-dated entries into a permanent block.
  assert(budget.allowed(500U));

  // The budget survives a reboot through its sealed record.
  const BudgetRecord record = budget_record(budget);
  assert(valid_budget_record(record));
  SendBudget restored_budget;
  restored_budget.load(record.sent_s);
  assert(restored_budget.used(1000U + day) == budget.used(1000U + day));
  BudgetRecord corrupt_record = record;
  corrupt_record.sent_s[3] ^= 1U;
  assert(!valid_budget_record(corrupt_record));

  char secret_buffer[8] = "secret";
  secure_wipe(secret_buffer, sizeof(secret_buffer));
  for (char c : secret_buffer) assert(c == '\0');

  // Bounded buffer and encoders.
  char small[8];
  Buffer bounded(small, sizeof(small));
  assert(bounded.append("1234567"));
  assert(!bounded.append("8"));
  assert(!bounded.ok() && std::strcmp(small, "1234567") == 0);
  Buffer counter = Buffer::counter();
  counter.append("hello");
  assert(counter.length() == 5 && counter.ok());

  char json[128];
  Buffer json_out(json, sizeof(json));
  append_json_string(json_out, "a\"b\\c\nd\x01");
  assert(std::strcmp(json, "\"a\\\"b\\\\c\\nd\\u0001\"") == 0);

  char encoded[64];
  Buffer base64(encoded, sizeof(encoded));
  append_base64(base64, "api:key-1");
  assert(std::strcmp(encoded, "YXBpOmtleS0x") == 0);
  Buffer base64_pad(encoded, sizeof(encoded));
  append_base64(base64_pad, "ab");
  assert(std::strcmp(encoded, "YWI=") == 0);

  // API requests: correct endpoint, auth header and exact Content-Length.
  Message message;
  message.from = "openquatt@example.nl";
  message.recipients = "a@example.nl, b@example.nl";
  message.subject = "OpenQuatt woonkamer: Storing bij warmtepomp 1";
  message.body = "Regel 1 \"quoted\"\nRegel 2 é\n";

  const std::string postmark = build_request(Provider::POSTMARK, complete_api_config(Provider::POSTMARK), message);
  assert(contains(postmark, "POST /email HTTP/1.1\r\nHost: api.postmarkapp.com\r\n"));
  assert(contains(postmark, "X-Postmark-Server-Token: token-123\r\n"));
  assert(contains(postmark, "\"To\":\"a@example.nl, b@example.nl\""));
  assert(contains(postmark, "\"From\":\"OpenQuatt <openquatt@example.nl>\""));
  assert(contains(postmark, "\\\"quoted\\\"\\nRegel 2"));
  assert(declared_content_length(postmark) == actual_body_length(postmark));

  const std::string mailgun_request =
      build_request(Provider::MAILGUN_EU, complete_api_config(Provider::MAILGUN_EU), message);
  assert(contains(mailgun_request, "POST /v3/mg.example.nl/messages HTTP/1.1\r\nHost: api.eu.mailgun.net\r\n"));
  assert(contains(mailgun_request, "Authorization: Basic YXBpOnRva2VuLTEyMw==\r\n"));
  assert(contains(mailgun_request, "Content-Type: multipart/form-data; boundary=oq-mail-7c1f2b9e4d5a\r\n"));
  assert(contains(mailgun_request, "name=\"to\"\r\n\r\na@example.nl\r\n"));
  assert(contains(mailgun_request, "name=\"to\"\r\n\r\nb@example.nl\r\n"));
  assert(contains(mailgun_request, "--oq-mail-7c1f2b9e4d5a--\r\n"));
  assert(declared_content_length(mailgun_request) == actual_body_length(mailgun_request));
  const std::string mailgun_us =
      build_request(Provider::MAILGUN_US, complete_api_config(Provider::MAILGUN_US), message);
  assert(contains(mailgun_us, "Host: api.mailgun.net\r\n"));

  const std::string lettermint =
      build_request(Provider::LETTERMINT, complete_api_config(Provider::LETTERMINT), message);
  assert(contains(lettermint, "POST /v1/send HTTP/1.1\r\nHost: api.lettermint.co\r\n"));
  assert(contains(lettermint, "x-lettermint-token: token-123\r\n"));
  assert(contains(lettermint, "\"to\":[\"a@example.nl\",\"b@example.nl\"]"));
  assert(declared_content_length(lettermint) == actual_body_length(lettermint));

  const std::string resend = build_request(Provider::RESEND, complete_api_config(Provider::RESEND), message);
  assert(contains(resend, "POST /emails HTTP/1.1\r\nHost: api.resend.com\r\n"));
  assert(contains(resend, "Authorization: Bearer token-123\r\n"));
  assert(declared_content_length(resend) == actual_body_length(resend));

  const std::string brevo = build_request(Provider::BREVO, complete_api_config(Provider::BREVO), message);
  assert(contains(brevo, "POST /v3/smtp/email HTTP/1.1\r\nHost: api.brevo.com\r\n"));
  assert(contains(brevo, "api-key: token-123\r\n"));
  assert(contains(brevo, "\"sender\":{\"name\":\"OpenQuatt\",\"email\":\"openquatt@example.nl\"}"));
  assert(contains(brevo, "\"to\":[{\"email\":\"a@example.nl\"},{\"email\":\"b@example.nl\"}]"));
  assert(declared_content_length(brevo) == actual_body_length(brevo));

  // A request that does not fit fails instead of being truncated.
  char tiny[64];
  Buffer tiny_out(tiny, sizeof(tiny));
  assert(!build_api_request(tiny_out, Provider::POSTMARK, "token", "", message));
  Buffer no_secret(tiny, sizeof(tiny));
  assert(!build_api_request(no_secret, Provider::POSTMARK, "", "", message));

  // SMTP message: encoded subject, CRLF-normalized base64 body, terminator.
  static char smtp_buffer[4096];
  Buffer smtp_out(smtp_buffer, sizeof(smtp_buffer));
  assert(build_smtp_message(smtp_out, message, "Tue, 10 Nov 2026 13:32:00 +0000", "1.2@openquatt"));
  const std::string smtp_message(smtp_buffer, smtp_out.length());
  assert(contains(smtp_message, "From: OpenQuatt <openquatt@example.nl>\r\n"));
  assert(contains(smtp_message, "To: a@example.nl, b@example.nl\r\n"));
  assert(contains(smtp_message, "Subject: =?UTF-8?B?"));
  assert(contains(smtp_message, "Date: Tue, 10 Nov 2026 13:32:00 +0000\r\n"));
  assert(contains(smtp_message, "Message-ID: <1.2@openquatt>\r\n"));
  assert(smtp_message.size() > 5 && smtp_message.compare(smtp_message.size() - 5, 5, "\r\n.\r\n") == 0);
  const size_t body_start = smtp_message.find("\r\n\r\n") + 4;
  const std::string body_encoded = smtp_message.substr(body_start, smtp_message.size() - body_start - 3);
  assert(decode_base64(body_encoded) == "Regel 1 \"quoted\"\r\nRegel 2 é\r\n");
  for (size_t line_start = body_start; line_start < smtp_message.size();) {
    const size_t line_end = smtp_message.find("\r\n", line_start);
    assert(line_end != std::string::npos && line_end - line_start <= 76);
    line_start = line_end + 2;
  }
  const size_t subject_start = smtp_message.find("=?UTF-8?B?") + 10;
  const size_t subject_end = smtp_message.find("?=", subject_start);
  assert(decode_base64(smtp_message.substr(subject_start, subject_end - subject_start)) == message.subject);

  // Long body exercises the 57-byte chunking without mid-stream padding.
  std::string long_body(500, 'x');
  long_body += "\n";
  Message long_message = message;
  long_message.body = long_body.c_str();
  Buffer long_out(smtp_buffer, sizeof(smtp_buffer));
  assert(build_smtp_message(long_out, long_message, "", ""));
  const std::string long_smtp(smtp_buffer, long_out.length());
  const size_t long_body_start = long_smtp.find("\r\n\r\n") + 4;
  const std::string long_encoded = long_smtp.substr(long_body_start, long_smtp.size() - long_body_start - 3);
  assert(decode_base64(long_encoded) == std::string(500, 'x') + "\r\n");
  assert(long_encoded.find('=') == long_encoded.rfind('=') || long_encoded.find('=') > long_encoded.size() - 5);
  assert(!contains(long_smtp, "Date:"));

  // HTTP status line parsing.
  HttpStatusParser http;
  assert(http.feed("HTTP/1.1 2", 10) == 0);
  assert(http.feed("02 Accepted\r\nX: y", 17) == 202);
  HttpStatusParser bad_http;
  assert(bad_http.feed("SMTP 250 ok\r\n", 13) == -1);

  // SMTP replies, including multi-line EHLO capabilities.
  SmtpReplyParser reply;
  reply.reset();
  const char* ehlo = "250-smtp.example.nl\r\n250-SIZE 35882577\r\n250-AUTH LOGIN PLAIN XOAUTH2\r\n";
  assert(reply.feed(ehlo, std::strlen(ehlo)) == SmtpReplyParser::State::INCOMPLETE);
  assert(reply.feed("250 SMTPUTF8\r\n", 14) == SmtpReplyParser::State::COMPLETE);
  assert(reply.code() == 250 && reply.auth_plain() && reply.auth_login());
  reply.reset();
  assert(reply.feed("250-x\r\n250 AUTH=LOGIN\r\n", 23) == SmtpReplyParser::State::COMPLETE);
  assert(!reply.auth_plain() && reply.auth_login());
  reply.reset();
  assert(reply.feed("535 5.7.8 Authentication failed\r\n", 33) == SmtpReplyParser::State::COMPLETE);
  assert(reply.code() == 535 && !smtp_positive(reply.code()));
  reply.reset();
  assert(reply.feed("250-a\r\n251 b\r\n", 14) == SmtpReplyParser::State::INVALID);
  reply.reset();
  assert(reply.feed("hello\r\n", 7) == SmtpReplyParser::State::INVALID);
  reply.reset();
  assert(reply.feed("220 ready", 9) == SmtpReplyParser::State::INCOMPLETE);
  assert(reply.feed("\r\n", 2) == SmtpReplyParser::State::COMPLETE && smtp_positive(reply.code()));

  // Mail copy.
  assert(std::strcmp(incident_title(5, reason::HP_FAULT), "Hogedrukbeveiliging") == 0);
  assert(std::strcmp(incident_title(0, reason::HP_LINK_LOSS), "Verbinding met warmtepomp weg") == 0);
  assert(std::strcmp(
             incident_title(static_cast<int16_t>(oq_incidents::kRuntimeFrequencyMappingIncidentId), reason::HP_FAULT),
             "ODU-frequentietabel moet opnieuw worden gevalideerd") == 0);
  assert(std::strcmp(incident_title(16, reason::HP_FAULT), "Onbekende ODU-melding") == 0);  // R2119.b15 unknown
  for (const auto& definition : oq_incidents::kHpIncidentCatalog) {
    bool found = false;
    for (const auto& entry : kIncidentTitles) found |= std::strcmp(entry.key, definition.key) == 0;
    assert(found);  // Every catalog incident has Dutch mail copy.
  }

  Notice notices[3];
  assert(classify(fault, notices[0]));
  notices[0].epoch_s = 1789000000U;
  assert(classify(fallback, notices[1]));  // BOILER_FALLBACK_STOP from above
  notices[1].epoch_s = 0U;
  assert(classify(cycling, notices[2]));
  notices[2].epoch_s = 1789000060U;

  char subject_text[160];
  Buffer subject_out(subject_text, sizeof(subject_text));
  append_subject(subject_out, "Woonkamer\r\nBcc: x", notices, 1, 0);
  assert(std::strcmp(subject_text, "OpenQuatt Woonkamer  Bcc: x: Storing bij warmtepomp 1") == 0);
  Buffer multi_subject(subject_text, sizeof(subject_text));
  append_subject(multi_subject, "Woonkamer", notices, 3, 2);
  assert(std::strcmp(subject_text, "OpenQuatt Woonkamer: 5 meldingen") == 0);

  char body_text[2048];
  Buffer body_out(body_text, sizeof(body_text));
  assert(append_body(body_out, "Woonkamer", "http://192.168.1.20/", notices, 3, 2,
                     [](uint32_t epoch, char* out, size_t size) {
                       std::snprintf(out, size, "T%u", static_cast<unsigned>(epoch));
                       return true;
                     }));
  const std::string body(body_text);
  assert(contains(body, "T1789000000  Storing bij warmtepomp 1: Hogedrukbeveiliging. Herstel vraagt"));
  assert(contains(body, "tijd onbekend  Ketel stopt met overnemen.\n"));
  assert(contains(body, "Veel compressorstarts: warmtepomp 1 9 starts (grens 6).\n"));
  assert(contains(body, "... en nog 2 meldingen."));
  assert(contains(body, "Web-app: http://192.168.1.20/\n"));

  Notice recovered;
  assert(classify(cleared, recovered));
  Buffer recovered_out(body_text, sizeof(body_text));
  append_short_title(recovered_out, recovered);
  append_detail(recovered_out, recovered);
  assert(std::strcmp(body_text, "Storing opgelost bij warmtepomp 1: Hogedrukbeveiliging (duurde 12 min).") == 0);

  Notice restart;
  restart.kind = static_cast<uint8_t>(Kind::CONTROLLER_RESTART);
  restart.value_a = reset_cause::WATCHDOG;
  Buffer restart_out(body_text, sizeof(body_text));
  append_short_title(restart_out, restart);
  append_detail(restart_out, restart);
  assert(std::strcmp(body_text, "Controller onverwacht herstart: een watchdog greep in.") == 0);

  Buffer duration(body_text, sizeof(body_text));
  append_duration(duration, 3725);
  assert(std::strcmp(body_text, "1 u 2 min") == 0);

  Buffer test_subject(subject_text, sizeof(subject_text));
  append_test_subject(test_subject, "Woonkamer");
  assert(std::strcmp(subject_text, "OpenQuatt Woonkamer: testmail") == 0);
  return 0;
}
