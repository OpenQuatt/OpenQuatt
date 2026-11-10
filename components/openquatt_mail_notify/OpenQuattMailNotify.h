#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/openquatt_decision_log/OpenQuattDecisionLog.h"
#include "esphome/components/ota/ota_backend.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "lwip/ip_addr.h"
#include "OpenQuattMailNotifyPolicy.h"
#include "PsramBuffer.h"

struct esp_tls;

namespace esphome::openquatt_web_auth {
class OpenQuattWebAuth;
}

namespace esphome::openquatt_mail_notify {

// Outbound e-mail notifications for selected decision-log events.
//
// Ownership:
// - Main loop: active configuration, notification batch, outgoing message,
//   delivery policy and job dispatch. It is the only writer of `active` and
//   writes it under lock_.
// - HTTPD task: reads a locked copy of the configuration/status and submits a
//   validated candidate into the locked inbox. It never touches NVS or TLS.
// - Worker task: while a job is REQUESTED it owns the job fields only (NVS
//   write, DNS, TLS and the protocol exchange) and releases the HTTPS slot.
// The component never writes control state.
class OpenQuattMailNotify : public Component, public ota::OTAGlobalStateListener {
 public:
  enum class SubmitResult : uint8_t { ACCEPTED, INVALID, BUSY, UNAVAILABLE };
  enum class TestResult : uint8_t { ACCEPTED, NOT_CONFIGURED, SAVE_PENDING, TEST_BUSY, RATE_LIMITED, UNAVAILABLE };

  struct SaveRequest {
    bool enabled{false};
    std::string provider;
    std::string smtp_host;
    std::string smtp_port;
    std::string username;
    std::string secret;
    bool clear_secret{false};
    std::string domain;
    std::string from;
    std::string recipients;
    std::string kinds;
    bool recovery{true};
  };

  void set_decision_log(openquatt_decision_log::OpenQuattDecisionLog* value) { decision_log_ = value; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* value) { web_auth_ = value; }
  void set_clock(time::RealTimeClock* value) { clock_ = value; }
  // Runtime polling pause (OTA and service holds): no new mail work, abort a send.
  void set_pause_sensor(binary_sensor::BinarySensor* value) { pause_ = value; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_ota_global_state(ota::OTAState state, float, uint8_t, ota::OTAComponent*) override;

  // HTTPD-task interface. Each call must hold the HTTP scratch lease.
  bool available() const { return storage_ && lock_ != nullptr; }
  bool authenticated(AsyncWebServerRequest* request) const;
  std::string csrf_token() const;
  bool begin_http() { return !http_busy_.exchange(true, std::memory_order_acquire); }
  void end_http() { http_busy_.store(false, std::memory_order_release); }
  bool write_status(httpd_req_t* req);
  SubmitResult submit(const SaveRequest& request, oq_mail::ValidationError& error, uint32_t& revision);
  TestResult request_test(uint32_t& test_id);

 protected:
  static constexpr const char* kNvsNamespace = "oq_mail";
  static constexpr const char* kNvsKey = "config";
  static constexpr const char* kNvsBudgetKey = "budget";
  static constexpr size_t kRingSize = 16;
  static constexpr size_t kBatchMax = 12;
  static constexpr size_t kSubjectMax = 192;
  static constexpr size_t kBodyMax = 3072;
  static constexpr size_t kTestSubjectMax = 128;
  static constexpr size_t kTestBodyMax = 768;
  static constexpr size_t kRequestMax = 8192;
  static constexpr size_t kStatusJsonMax = 2048;

  enum class Phase : uint8_t { IDLE, REQUESTED, READY };
  enum class Job : uint8_t { SAVE, SAVE_BUDGET, SEND_NOTIFICATION, SEND_TEST };
  enum class SaveState : uint8_t { IDLE, PENDING, STORED, FAILED };
  enum class TestState : uint8_t { IDLE, QUEUED, SENDING, SENT, FAILED };
  enum class LastState : uint8_t { NONE, SENT, FAILED };

  struct Storage {
    oq_mail::Config active;               // Main-loop writer; HTTPD reads under lock_.
    oq_mail::Config inbox;                // Guarded by lock_.
    oq_mail::Config job_config;           // Worker-owned while REQUESTED.
    oq_mail::Config http_config;          // HTTPD-owned under the HTTP lease.
    oq_mail::BudgetRecord budget_record;  // Worker-owned while a SAVE_BUDGET job is REQUESTED.
    oq_mail::Notice ring[kRingSize];      // Guarded by event_mux_.
    oq_mail::Notice batch[kBatchMax];
    char subject[kSubjectMax];
    char body[kBodyMax];
    char test_subject[kTestSubjectMax];
    char test_body[kTestBodyMax];
    char date_header[48];
    char message_id[64];
    char ehlo_host[48];
    char request[kRequestMax];         // Worker scratch.
    char tls_chunk[512];               // Worker scratch.
    char status_json[kStatusJsonMax];  // HTTPD scratch under the HTTP lease.
    Job job;
    bool result_ok;
    const char* error;
    int code;
    bool permanent;
    uint32_t duration_ms;
    uint32_t stack_free;
    uint32_t internal_largest_low;
    uint64_t request_ms;
  };
  static_assert(sizeof(Storage) <= 20U * 1024U, "mail notification data must fit its strict PSRAM budget");

  // Published under lock_ for the HTTPD task.
  struct Published {
    uint32_t save_revision{0};
    SaveState save_state{SaveState::IDLE};
    const char* save_error{"none"};
    uint32_t test_id{0};
    TestState test_state{TestState::IDLE};
    const char* test_error{"none"};
    int test_code{0};
    bool test_requested{false};
    uint32_t last_test_request_ms{0};
    LastState last_state{LastState::NONE};
    uint32_t last_epoch{0};
    const char* last_error{"none"};
    int last_code{0};
    uint32_t queued{0};
    uint32_t sent_24h{0};
    bool configured{false};
    bool inbox_pending{false};
  };

  static void on_decision_event_(void* context, const openquatt_decision_log::DecisionEvent& event);
  static void worker_entry_(void* argument);
  static void dns_start_(void* argument);
  static void dns_complete_(const char*, const ip_addr_t* address, void* argument);

  void apply_active_runtime_();
  void load_config_();
  void queue_restart_notice_();
  void drain_ring_(uint64_t now_ms);
  void clear_pending_notifications_();
  void load_budget_();
  bool dispatch_save_();
  bool dispatch_budget_save_();
  bool paused_() const;
  bool current_epoch_(uint32_t& epoch_s) const;
  bool dispatch_test_(uint64_t now_ms);
  void prepare_outgoing_(uint64_t now_ms);
  void dispatch_notification_(uint64_t now_ms);
  bool start_job_(Job job, uint64_t now_ms);
  void handle_result_(uint64_t now_ms);
  void publish_counters_(uint64_t now_ms);
  void update_ota_hold_();
  bool acquire_https_slot_();
  static void release_https_slot_();
  void prepare_job_headers_();
  void web_url_(char* out, size_t size) const;
  const char* device_name_() const;
  bool format_local_time_(uint32_t epoch_s, char* out, size_t size) const;

  // Worker-side helpers (OpenQuattMailNotifyTransport.cpp).
  bool save_job_();
  bool save_budget_job_();
  bool send_job_();
  bool tls_write_(::esp_tls* tls, const char* data, size_t length);
  int tls_read_(::esp_tls* tls, char* data, size_t size);
  bool smtp_reply_(::esp_tls* tls, oq_mail::SmtpReplyParser& reply);
  bool smtp_command_(::esp_tls* tls, oq_mail::SmtpReplyParser& reply, const char* command, size_t length);
  bool smtp_exchange_(::esp_tls* tls, size_t message_length);
  bool http_exchange_(::esp_tls* tls, size_t request_length);
  bool cancelled_() const;
  bool resolve_dns_(const char* hostname, uint32_t& address);
  void sample_transport_memory_();
  bool fail_(const char* error, int code = 0, bool permanent = false);

  openquatt_common::PsramObjectArray<Storage, 1> storage_;
  openquatt_decision_log::OpenQuattDecisionLog* decision_log_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* web_auth_{nullptr};
  time::RealTimeClock* clock_{nullptr};
  binary_sensor::BinarySensor* pause_{nullptr};

  StaticSemaphore_t lock_storage_{};
  SemaphoreHandle_t lock_{nullptr};
  Published published_{};       // Guarded by lock_.
  uint32_t inbox_revision_{0};  // Guarded by lock_.

  mutable portMUX_TYPE event_mux_ = portMUX_INITIALIZER_UNLOCKED;
  size_t ring_head_{0};       // Guarded by event_mux_.
  size_t ring_count_{0};      // Guarded by event_mux_.
  uint32_t ring_dropped_{0};  // Guarded by event_mux_.

  std::atomic<Phase> phase_{Phase::IDLE};
  std::atomic<bool> cancel_{false};
  std::atomic<bool> accepting_{false};
  std::atomic<uint16_t> kind_mask_{0};
  std::atomic<bool> recovery_{false};
  std::atomic<bool> http_busy_{false};
  std::atomic<bool> ota_hold_{false};
  std::atomic<uint32_t> ota_progress_ms_{0};

  TaskHandle_t worker_{nullptr};
  bool worker_attempted_{false};
  // DNS callbacks target this permanent component, never request-local memory.
  char dns_host_[oq_mail::kHostMax + 1]{};  // Worker writes only while no lookup is outstanding.
  std::atomic<bool> dns_done_{false};
  std::atomic<uint32_t> dns_address_{0};
  bool dns_outstanding_{false};  // Worker-only.

  // Main-loop state.
  uint32_t job_revision_{0};
  uint32_t job_test_id_{0};
  uint32_t job_epoch_s_{0};
  size_t batch_count_{0};
  uint32_t batch_overflow_{0};
  uint64_t batch_started_ms_{0};
  bool outgoing_ready_{false};
  uint32_t outgoing_lines_{0};
  uint8_t outgoing_attempts_{0};
  uint64_t outgoing_next_ms_{0};
  oq_mail::SendBudget budget_{};
  bool budget_dirty_{false};
  int16_t restart_cause_{0};
  bool restart_notice_pending_{false};
  uint64_t last_counter_publish_ms_{0};
  uint32_t worker_stack_min_{0};
  uint32_t worker_largest_low_{0};
};

// Registers the /openquatt/mail/* routes (OpenQuattMailNotifyHttp.cpp).
void register_http_handler(OpenQuattMailNotify* owner);

}  // namespace esphome::openquatt_mail_notify
