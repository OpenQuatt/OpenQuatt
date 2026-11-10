#include "OpenQuattMailNotify.h"

#include <cstdio>
#include <cstring>

#include "OpenQuattHttpStatus.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"
#include "lwip/sockets.h"
#include "mbedtls/platform_util.h"

namespace esphome::openquatt_mail_notify {

namespace {

using openquatt_common::HttpStatus;
using openquatt_common::send_http_status;

bool same_origin(AsyncWebServerRequest* request) {
  const auto host = request->get_header("Host");
  if (!host || host->empty()) return false;
  for (const char* name : {"Origin", "Referer"}) {
    const auto header = request->get_header(name);
    if (!header) continue;
    const auto start = header->find("://");
    if (start == std::string::npos) return false;
    const auto end = header->find_first_of("/?#", start + 3);
    if (header->substr(start + 3, end == std::string::npos ? end : end - start - 3) != *host) return false;
  }
  return true;
}

bool truthy(const std::string& value) { return value == "true" || value == "1" || value == "on"; }

class MailHandler : public AsyncWebHandler {
 public:
  explicit MailHandler(OpenQuattMailNotify* owner) : owner_(owner) {}

  bool canHandle(AsyncWebServerRequest* request) const override { return route_(request) != Route::NONE; }

  void handleRequest(AsyncWebServerRequest* request) override {
    const Route route = route_(request);
    if (!owner_->authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    if (!owner_->available()) {
      send_http_status(*request, HttpStatus::SERVICE_UNAVAILABLE, R"({"ok":false,"error":"unavailable"})");
      return;
    }
    if (route != Route::STATUS) {
      const std::string token = owner_->csrf_token();
      if (!same_origin(request) || token.empty() || request->arg("csrf_token") != token) {
        send_http_status(*request, HttpStatus::FORBIDDEN, R"({"ok":false,"error":"forbidden"})");
        return;
      }
    }
    if (!owner_->begin_http()) {
      send_http_status(*request, HttpStatus::TOO_MANY_REQUESTS, R"({"ok":false,"error":"busy"})");
      return;
    }
    struct Lease {
      OpenQuattMailNotify* owner;
      ~Lease() { owner->end_http(); }
    } lease{owner_};
    switch (route) {
      case Route::STATUS:
        if (!owner_->write_status(*request)) ESP_LOGW("openquatt.mail", "Status response interrupted");
        return;
      case Route::SAVE:
        handle_save_(request);
        return;
      case Route::TEST:
        handle_test_(request);
        return;
      case Route::NONE:
        break;
    }
    request->send(404, "application/json", R"({"ok":false,"error":"not_found"})");
  }

 private:
  enum class Route : uint8_t { NONE, STATUS, SAVE, TEST };

  // Keep decoded URL scratch out of response generation and mutation call chains.
  __attribute__((noinline)) static Route route_(AsyncWebServerRequest* request) {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(url_buf);
    if (request->method() == HTTP_GET && url == "/openquatt/mail/status") return Route::STATUS;
    if (request->method() == HTTP_POST && url == "/openquatt/mail/save") return Route::SAVE;
    if (request->method() == HTTP_POST && url == "/openquatt/mail/test") return Route::TEST;
    return Route::NONE;
  }

  __attribute__((noinline)) void handle_save_(AsyncWebServerRequest* request) {
    OpenQuattMailNotify::SaveRequest save;
    save.enabled = truthy(request->arg("enabled"));
    save.provider = request->arg("provider");
    save.smtp_host = request->arg("smtp_host");
    save.smtp_port = request->arg("smtp_port");
    save.username = request->arg("username");
    save.secret = request->arg("secret");
    save.clear_secret = truthy(request->arg("clear_secret"));
    save.domain = request->arg("domain");
    save.from = request->arg("from");
    save.recipients = request->arg("recipients");
    save.kinds = request->arg("kinds");
    save.recovery = truthy(request->arg("recovery"));
    oq_mail::ValidationError error = oq_mail::ValidationError::NONE;
    uint32_t revision = 0;
    const auto result = owner_->submit(save, error, revision);
    if (!save.secret.empty()) mbedtls_platform_zeroize(&save.secret[0], save.secret.size());
    char body[96];
    switch (result) {
      case OpenQuattMailNotify::SubmitResult::ACCEPTED:
        std::snprintf(body, sizeof(body), R"({"ok":true,"revision":%u})", static_cast<unsigned>(revision));
        send_http_status(*request, HttpStatus::ACCEPTED, body);
        return;
      case OpenQuattMailNotify::SubmitResult::INVALID:
        std::snprintf(body, sizeof(body), R"({"ok":false,"error":"%s"})", oq_mail::validation_error_key(error));
        request->send(409, "application/json", body);
        return;
      case OpenQuattMailNotify::SubmitResult::BUSY:
        send_http_status(*request, HttpStatus::SERVICE_UNAVAILABLE, R"({"ok":false,"error":"busy"})");
        return;
      case OpenQuattMailNotify::SubmitResult::UNAVAILABLE:
        send_http_status(*request, HttpStatus::SERVICE_UNAVAILABLE, R"({"ok":false,"error":"unavailable"})");
        return;
    }
  }

  __attribute__((noinline)) void handle_test_(AsyncWebServerRequest* request) {
    uint32_t test_id = 0;
    const auto result = owner_->request_test(test_id);
    char body[96];
    const char* error = nullptr;
    switch (result) {
      case OpenQuattMailNotify::TestResult::ACCEPTED:
        std::snprintf(body, sizeof(body), R"({"ok":true,"test_id":%u})", static_cast<unsigned>(test_id));
        send_http_status(*request, HttpStatus::ACCEPTED, body);
        return;
      case OpenQuattMailNotify::TestResult::UNAVAILABLE:
        send_http_status(*request, HttpStatus::SERVICE_UNAVAILABLE, R"({"ok":false,"error":"unavailable"})");
        return;
      case OpenQuattMailNotify::TestResult::NOT_CONFIGURED:
        error = "not_configured";
        break;
      case OpenQuattMailNotify::TestResult::SAVE_PENDING:
        error = "save_pending";
        break;
      case OpenQuattMailNotify::TestResult::TEST_BUSY:
        error = "test_busy";
        break;
      case OpenQuattMailNotify::TestResult::RATE_LIMITED:
        error = "rate_limited";
        break;
    }
    std::snprintf(body, sizeof(body), R"({"ok":false,"error":"%s"})", error);
    request->send(409, "application/json", body);
  }

  OpenQuattMailNotify* owner_;
};

const char* save_state_key(uint8_t state) {
  static const char* const keys[] = {"idle", "pending", "stored", "failed"};
  return state < 4 ? keys[state] : "idle";
}

const char* test_state_key(uint8_t state) {
  static const char* const keys[] = {"idle", "queued", "sending", "sent", "failed"};
  return state < 5 ? keys[state] : "idle";
}

const char* last_state_key(uint8_t state) {
  static const char* const keys[] = {"none", "sent", "failed"};
  return state < 3 ? keys[state] : "none";
}

}  // namespace

void register_http_handler(OpenQuattMailNotify* owner) {
  if (web_server_base::global_web_server_base != nullptr)
    web_server_base::global_web_server_base->add_handler(new MailHandler(owner));
}

bool OpenQuattMailNotify::write_status(httpd_req_t* req) {
  auto& state = storage_[0];
  Published snapshot;
  {
    xSemaphoreTake(lock_, portMAX_DELAY);
    state.http_config = state.active;
    snapshot = published_;
    xSemaphoreGive(lock_);
  }
  // The status never contains the credential itself.
  const bool secret_set = state.http_config.secret[0] != '\0';
  mbedtls_platform_zeroize(state.http_config.secret, sizeof(state.http_config.secret));
  const auto& config = state.http_config;
  const std::string csrf = csrf_token();

  oq_mail::Buffer out(state.status_json, sizeof(state.status_json));
  out.append(R"({"enabled":)");
  out.append(config.enabled != 0U ? "true" : "false");
  out.append(R"(,"configured":)");
  out.append(snapshot.configured ? "true" : "false");
  out.append(R"(,"provider":)");
  oq_mail::append_json_string(out, oq_mail::provider_key(static_cast<oq_mail::Provider>(config.provider)));
  out.append(R"(,"smtp_host":)");
  oq_mail::append_json_string(out, config.smtp_host);
  out.appendf(R"(,"smtp_port":%u,"username":)", static_cast<unsigned>(config.smtp_port));
  oq_mail::append_json_string(out, config.username);
  out.append(R"(,"secret_set":)");
  out.append(secret_set ? "true" : "false");
  out.append(R"(,"domain":)");
  oq_mail::append_json_string(out, config.domain);
  out.append(R"(,"from":)");
  oq_mail::append_json_string(out, config.from);
  out.append(R"(,"recipients":)");
  oq_mail::append_json_string(out, config.recipients);
  out.append(R"(,"kinds":{)");
  for (uint8_t kind = 0; kind < oq_mail::kKindCount; ++kind) {
    if (kind > 0) out.append_char(',');
    oq_mail::append_json_string(out, oq_mail::kind_key(static_cast<oq_mail::Kind>(kind)));
    out.append((config.kind_mask & (1U << kind)) != 0U ? ":true" : ":false");
  }
  out.append(R"(},"recovery":)");
  out.append(config.recovery != 0U ? "true" : "false");
  out.appendf(R"(,"save":{"revision":%u,"state":)", static_cast<unsigned>(snapshot.save_revision));
  oq_mail::append_json_string(out, save_state_key(static_cast<uint8_t>(snapshot.save_state)));
  out.append(R"(,"error":)");
  oq_mail::append_json_string(out, snapshot.save_error);
  out.appendf(R"(},"test":{"id":%u,"state":)", static_cast<unsigned>(snapshot.test_id));
  oq_mail::append_json_string(out, test_state_key(static_cast<uint8_t>(snapshot.test_state)));
  out.append(R"(,"error":)");
  oq_mail::append_json_string(out, snapshot.test_error);
  out.appendf(R"(,"code":%d},"last":{"state":)", snapshot.test_code);
  oq_mail::append_json_string(out, last_state_key(static_cast<uint8_t>(snapshot.last_state)));
  out.appendf(R"(,"epoch":%u,"error":)", static_cast<unsigned>(snapshot.last_epoch));
  oq_mail::append_json_string(out, snapshot.last_error);
  out.appendf(R"(,"code":%d},"queued":%u,"sent_24h":%u,"daily_limit":%u,"csrf_token":)", snapshot.last_code,
              static_cast<unsigned>(snapshot.queued), static_cast<unsigned>(snapshot.sent_24h),
              static_cast<unsigned>(oq_mail::kDailyLimit));
  oq_mail::append_json_string(out, csrf.c_str());
  out.append_char('}');
  if (!out.ok()) {
    send_http_status(req, HttpStatus::SERVICE_UNAVAILABLE, R"({"ok":false,"error":"status_too_large"})");
    return false;
  }
  const bool sent = httpd_resp_set_type(req, "application/json; charset=utf-8") == ESP_OK &&
                    httpd_resp_set_hdr(req, "Cache-Control", "no-store") == ESP_OK &&
                    httpd_resp_send(req, state.status_json, static_cast<ssize_t>(out.length())) == ESP_OK;
  if (!sent) shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
  return sent;
}

}  // namespace esphome::openquatt_mail_notify
