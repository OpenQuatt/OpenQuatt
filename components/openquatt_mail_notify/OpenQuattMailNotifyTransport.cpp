#include "OpenQuattMailNotify.h"

#include <cstdio>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcpip.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"

namespace esphome::openquatt_mail_notify {

static uint64_t transport_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL; }

void OpenQuattMailNotify::worker_entry_(void* argument) {
  auto* self = static_cast<OpenQuattMailNotify*>(argument);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (self->phase_.load(std::memory_order_acquire) != Phase::REQUESTED) continue;
    auto& state = self->storage_[0];
    const uint64_t started = transport_ms();
    state.internal_largest_low = 0;
    if (state.job == Job::SAVE) {
      state.result_ok = self->save_job_();
    } else if (state.job == Job::SAVE_BUDGET) {
      state.result_ok = self->save_budget_job_();
    } else {
      state.result_ok = self->send_job_();
      // send_job_ destroyed TLS and the socket on every path. Release here,
      // not in the main loop: HTTP OTA may block the main loop while waiting.
      release_https_slot_();
    }
    state.duration_ms = static_cast<uint32_t>(transport_ms() - started);
    state.stack_free = uxTaskGetStackHighWaterMark(nullptr);
    self->phase_.store(Phase::READY, std::memory_order_release);
  }
}

bool OpenQuattMailNotify::fail_(const char* error, int code, bool permanent) {
  auto& state = storage_[0];
  state.error = error;
  state.code = code;
  state.permanent = permanent;
  return false;
}

bool OpenQuattMailNotify::cancelled_() const {
  const auto& state = storage_[0];
  const uint64_t now = transport_ms();
  return cancel_.load(std::memory_order_acquire) || now < state.request_ms ||
         now - state.request_ms >= oq_mail::kRequestDeadlineMs;
}

void OpenQuattMailNotify::sample_transport_memory_() {
  const uint32_t block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  auto& low = storage_[0].internal_largest_low;
  if (low == 0 || block < low) low = block;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool OpenQuattMailNotify::save_job_() {
  auto& state = storage_[0];
  nvs_handle_t handle;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return fail_("persistence_failed");
  const bool written = nvs_set_blob(handle, kNvsKey, &state.job_config, sizeof(state.job_config)) == ESP_OK &&
                       nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  if (!written) return fail_("persistence_failed");

  // Verify the durable bytes. The request buffer is idle during a save job.
  static_assert(sizeof(state.request) >= sizeof(oq_mail::Config), "verification scratch too small");
  size_t size = sizeof(oq_mail::Config);
  bool verified = false;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    verified = nvs_get_blob(handle, kNvsKey, state.request, &size) == ESP_OK && size == sizeof(oq_mail::Config) &&
               std::memcmp(state.request, &state.job_config, sizeof(oq_mail::Config)) == 0;
    nvs_close(handle);
  }
  mbedtls_platform_zeroize(state.request, sizeof(oq_mail::Config));
  return verified ? true : fail_("persistence_failed");
}

bool OpenQuattMailNotify::save_budget_job_() {
  auto& state = storage_[0];
  nvs_handle_t handle;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return fail_("persistence_failed");
  const bool written =
      nvs_set_blob(handle, kNvsBudgetKey, &state.budget_record, sizeof(state.budget_record)) == ESP_OK &&
      nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return written ? true : fail_("persistence_failed");
}

// ---------------------------------------------------------------------------
// DNS (asynchronous lwIP lookup; esp-tls getaddrinfo cannot be cancelled)
// ---------------------------------------------------------------------------

void OpenQuattMailNotify::dns_complete_(const char*, const ip_addr_t* address, void* argument) {
  auto* self = static_cast<OpenQuattMailNotify*>(argument);
  self->dns_address_.store(address && IP_IS_V4(address) ? ip_2_ip4(address)->addr : 0, std::memory_order_relaxed);
  self->dns_done_.store(true, std::memory_order_release);
}

void OpenQuattMailNotify::dns_start_(void* argument) {
  auto* self = static_cast<OpenQuattMailNotify*>(argument);
  ip_addr_t address{};
  const err_t result =
      dns_gethostbyname_addrtype(self->dns_host_, &address, dns_complete_, self, LWIP_DNS_ADDRTYPE_IPV4);
  if (result == ERR_OK)
    dns_complete_(self->dns_host_, &address, self);
  else if (result != ERR_INPROGRESS)
    dns_complete_(self->dns_host_, nullptr, self);
}

bool OpenQuattMailNotify::resolve_dns_(const char* hostname, uint32_t& address) {
  ip4_addr_t literal{};
  if (ip4addr_aton(hostname, &literal)) {
    address = literal.addr;
    return true;
  }
  // A cancelled lookup may still be queued in lwIP. Never rewrite its hostname
  // storage until its callback has completed.
  if (dns_outstanding_ && !dns_done_.load(std::memory_order_acquire)) return fail_("dns_failed");
  std::strncpy(dns_host_, hostname, sizeof(dns_host_) - 1);
  dns_host_[sizeof(dns_host_) - 1] = '\0';
  dns_outstanding_ = true;
  dns_done_.store(false, std::memory_order_relaxed);
  dns_address_.store(0, std::memory_order_relaxed);
  if (tcpip_try_callback(dns_start_, this) != ERR_OK) {
    dns_outstanding_ = false;
    return fail_("dns_failed");
  }
  while (!dns_done_.load(std::memory_order_acquire)) {
    if (cancelled_()) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  dns_outstanding_ = false;
  address = dns_address_.load(std::memory_order_relaxed);
  if (address == 0) return fail_("dns_failed");
  return !cancelled_();
}

// ---------------------------------------------------------------------------
// TLS helpers
// ---------------------------------------------------------------------------

bool OpenQuattMailNotify::tls_write_(::esp_tls* tls, const char* data, size_t length) {
  size_t sent = 0;
  while (sent < length) {
    if (cancelled_()) return false;
    const ssize_t count = esp_tls_conn_write(tls, data + sent, length - sent);
    sample_transport_memory_();
    if (count == ESP_TLS_ERR_SSL_WANT_READ || count == ESP_TLS_ERR_SSL_WANT_WRITE) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (count <= 0) return false;
    sent += static_cast<size_t>(count);
  }
  return true;
}

// Returns >0 bytes read, 0 when the peer closed and <0 on error or cancel.
int OpenQuattMailNotify::tls_read_(::esp_tls* tls, char* data, size_t size) {
  for (;;) {
    if (cancelled_()) return -1;
    const ssize_t count = esp_tls_conn_read(tls, data, size);
    sample_transport_memory_();
    if (count == ESP_TLS_ERR_SSL_WANT_READ || count == ESP_TLS_ERR_SSL_WANT_WRITE) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    return static_cast<int>(count);
  }
}

// ---------------------------------------------------------------------------
// HTTP API providers
// ---------------------------------------------------------------------------

bool OpenQuattMailNotify::http_exchange_(::esp_tls* tls, size_t request_length) {
  auto& state = storage_[0];
  if (!tls_write_(tls, state.request, request_length)) return fail_("tls_write_failed");
  oq_mail::HttpStatusParser parser;
  while (parser.status() == 0) {
    const int count = tls_read_(tls, state.tls_chunk, sizeof(state.tls_chunk));
    if (count < 0) return fail_("tls_read_failed");
    if (count == 0) return fail_("response_invalid");
    parser.feed(state.tls_chunk, static_cast<size_t>(count));
  }
  const int status = parser.status();
  if (status < 0) return fail_("response_invalid");
  if (status >= 200 && status < 300) return true;
  return fail_("http_rejected", status, oq_mail::permanent_http_status(status));
}

// ---------------------------------------------------------------------------
// SMTP over implicit TLS (port 465)
// ---------------------------------------------------------------------------

bool OpenQuattMailNotify::smtp_reply_(::esp_tls* tls, oq_mail::SmtpReplyParser& reply) {
  auto& state = storage_[0];
  reply.reset();
  while (reply.state() == oq_mail::SmtpReplyParser::State::INCOMPLETE) {
    const int count = tls_read_(tls, state.tls_chunk, sizeof(state.tls_chunk));
    if (count <= 0) return false;
    reply.feed(state.tls_chunk, static_cast<size_t>(count));
  }
  return reply.state() == oq_mail::SmtpReplyParser::State::COMPLETE;
}

bool OpenQuattMailNotify::smtp_command_(::esp_tls* tls, oq_mail::SmtpReplyParser& reply, const char* command,
                                        size_t length) {
  return tls_write_(tls, command, length) && smtp_reply_(tls, reply);
}

bool OpenQuattMailNotify::smtp_exchange_(::esp_tls* tls, size_t message_length) {
  auto& state = storage_[0];
  const auto& config = state.job_config;
  oq_mail::SmtpReplyParser reply;
  char command[320];

  if (!smtp_reply_(tls, reply) || reply.code() != 220) {
    return fail_("smtp_greeting", reply.code(), oq_mail::permanent_smtp_code(reply.code()));
  }
  int ehlo_length = std::snprintf(command, sizeof(command), "EHLO %s\r\n", state.ehlo_host);
  if (ehlo_length <= 0 || static_cast<size_t>(ehlo_length) >= sizeof(command) ||
      !smtp_command_(tls, reply, command, static_cast<size_t>(ehlo_length)) || reply.code() != 250) {
    return fail_("smtp_ehlo", reply.code(), oq_mail::permanent_smtp_code(reply.code()));
  }

  if (config.username[0] != '\0') {
    const bool plain = reply.auth_plain() || !reply.auth_login();
    // Credentials only live on this stack frame and are wiped on every path.
    char raw[oq_mail::kUsernameMax + oq_mail::kSecretMax + 3];
    // "AUTH PLAIN " + base64(NUL user NUL secret) + CRLF.
    char encoded[11 + ((sizeof(raw) + 2) / 3) * 4 + 3];
    bool ok = false;
    if (plain) {
      const size_t user_length = std::strlen(config.username);
      const size_t secret_length = std::strlen(config.secret);
      raw[0] = '\0';
      std::memcpy(raw + 1, config.username, user_length);
      raw[1 + user_length] = '\0';
      std::memcpy(raw + 2 + user_length, config.secret, secret_length);
      oq_mail::Buffer line(encoded, sizeof(encoded));
      line.append("AUTH PLAIN ");
      oq_mail::append_base64(line, reinterpret_cast<const uint8_t*>(raw), 2 + user_length + secret_length);
      line.append("\r\n");
      ok = line.ok() && smtp_command_(tls, reply, encoded, line.length()) && reply.code() == 235;
    } else {
      static const char kAuthLogin[] = "AUTH LOGIN\r\n";
      ok = smtp_command_(tls, reply, kAuthLogin, sizeof(kAuthLogin) - 1) && reply.code() == 334;
      for (const char* part : {static_cast<const char*>(config.username), static_cast<const char*>(config.secret)}) {
        if (!ok) break;
        oq_mail::Buffer line(encoded, sizeof(encoded));
        oq_mail::append_base64(line, part);
        line.append("\r\n");
        const int expected = part == config.username ? 334 : 235;
        ok = line.ok() && smtp_command_(tls, reply, encoded, line.length()) && reply.code() == expected;
      }
    }
    mbedtls_platform_zeroize(raw, sizeof(raw));
    mbedtls_platform_zeroize(encoded, sizeof(encoded));
    if (!ok) return fail_("smtp_auth", reply.code(), oq_mail::permanent_smtp_code(reply.code()));
  }

  int length = std::snprintf(command, sizeof(command), "MAIL FROM:<%s>\r\n", config.from);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(command) ||
      !smtp_command_(tls, reply, command, static_cast<size_t>(length)) || reply.code() != 250) {
    return fail_("smtp_mail_from", reply.code(), oq_mail::permanent_smtp_code(reply.code()));
  }

  bool recipients_ok = oq_mail::for_each_recipient(config.recipients, [&](const char* address, size_t address_length) {
    const int written =
        std::snprintf(command, sizeof(command), "RCPT TO:<%.*s>\r\n", static_cast<int>(address_length), address);
    return written > 0 && static_cast<size_t>(written) < sizeof(command) &&
           smtp_command_(tls, reply, command, static_cast<size_t>(written)) &&
           (reply.code() == 250 || reply.code() == 251);
  });
  if (!recipients_ok) return fail_("smtp_rcpt_to", reply.code(), oq_mail::permanent_smtp_code(reply.code()));

  static const char kData[] = "DATA\r\n";
  if (!smtp_command_(tls, reply, kData, sizeof(kData) - 1) || reply.code() != 354) {
    return fail_("smtp_data", reply.code(), oq_mail::permanent_smtp_code(reply.code()));
  }
  if (!smtp_command_(tls, reply, state.request, message_length) || reply.code() != 250) {
    return fail_("smtp_message", reply.code(), oq_mail::permanent_smtp_code(reply.code()));
  }

  // The message is accepted; QUIT is a courtesy and cannot fail the delivery.
  static const char kQuit[] = "QUIT\r\n";
  if (tls_write_(tls, kQuit, sizeof(kQuit) - 1)) (void)smtp_reply_(tls, reply);
  return true;
}

// ---------------------------------------------------------------------------
// One delivery attempt
// ---------------------------------------------------------------------------

bool OpenQuattMailNotify::send_job_() {
  auto& state = storage_[0];
  const auto& config = state.job_config;
  const auto provider = static_cast<oq_mail::Provider>(config.provider);
  const bool test = state.job == Job::SEND_TEST;
  oq_mail::Message message;
  message.from = config.from;
  message.recipients = config.recipients;
  message.subject = test ? state.test_subject : state.subject;
  message.body = test ? state.test_body : state.body;

  oq_mail::Buffer request(state.request, sizeof(state.request));
  const char* host = nullptr;
  uint16_t port = 443;
  if (provider == oq_mail::Provider::SMTP) {
    host = config.smtp_host;
    port = config.smtp_port;
    if (!oq_mail::build_smtp_message(request, message, state.date_header, state.message_id)) {
      return fail_("request_too_large", 0, true);
    }
  } else if (oq_mail::is_api_provider(provider)) {
    host = oq_mail::api_host(provider);
    if (!oq_mail::build_api_request(request, provider, config.secret, config.domain, message)) {
      mbedtls_platform_zeroize(state.request, sizeof(state.request));
      return fail_("request_too_large", 0, true);
    }
  } else {
    return fail_("not_configured", 0, true);
  }
  const size_t request_length = request.length();

  bool ok = false;
  esp_tls_t* tls = nullptr;
  do {
    uint32_t address = 0;
    if (!resolve_dns_(host, address)) break;
    ip4_addr_t ipv4{};
    ipv4.addr = address;
    char numeric_address[16];
    if (!ip4addr_ntoa_r(&ipv4, numeric_address, sizeof(numeric_address))) {
      fail_("dns_failed");
      break;
    }
    // Numeric connect avoids the synchronous getaddrinfo inside esp-tls. The
    // configured hostname remains both SNI and certificate identity.
    esp_tls_cfg_t tls_config{};
    tls_config.non_block = true;
    tls_config.timeout_ms = 50;
    tls_config.common_name = host;
    tls_config.crt_bundle_attach = esp_crt_bundle_attach;
    tls = esp_tls_init();
    sample_transport_memory_();
    if (tls == nullptr) {
      fail_("tls_allocation_failed");
      break;
    }
    int connected = 0;
    while (!cancelled_() && connected == 0) {
      connected = esp_tls_conn_new_async(numeric_address, std::strlen(numeric_address), port, &tls_config, tls);
      sample_transport_memory_();
      if (connected == 0) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (connected != 1) {
      fail_("tls_connect_failed");
      break;
    }
    ok =
        provider == oq_mail::Provider::SMTP ? smtp_exchange_(tls, request_length) : http_exchange_(tls, request_length);
  } while (false);
  // Only this worker owns and destroys TLS and the socket, including after cancel.
  if (tls != nullptr) esp_tls_conn_destroy(tls);
  // API requests carry the credential in a header.
  mbedtls_platform_zeroize(state.request, sizeof(state.request));
  if (!ok && cancelled_()) {
    return fail_(cancel_.load(std::memory_order_acquire) ? "cancelled" : "deadline");
  }
  return ok;
}

}  // namespace esphome::openquatt_mail_notify
