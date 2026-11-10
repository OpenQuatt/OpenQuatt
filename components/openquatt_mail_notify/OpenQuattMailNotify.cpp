#include "OpenQuattMailNotify.h"

#include <cstdio>
#include <cstring>

#include "OpenQuattMailNotifyText.h"
#include "esphome/components/network/util.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"

#ifdef USE_OPENQUATT_WEATHER
#include "esphome/components/openquatt_weather/WeatherHttpsSlot.h"
#endif

namespace esphome::openquatt_mail_notify {

static const char* const TAG = "openquatt.mail";
static constexpr uint32_t MAIL_TASK_STACK_SIZE = 6144;
static constexpr uint32_t OTA_QUIET_TIMEOUT_MS = 180000;
static constexpr uint64_t COUNTER_PUBLISH_MS = 1000;

namespace dl = openquatt_decision_log;
static_assert(oq_mail::event::ATTENTION_PATTERN == dl::EVENT_ATTENTION_PATTERN, "decision-log mirror");
static_assert(oq_mail::event::FROST_PROTECTION_START == dl::EVENT_FROST_PROTECTION_START, "decision-log mirror");
static_assert(oq_mail::event::FROST_PROTECTION_CLEAR == dl::EVENT_FROST_PROTECTION_CLEAR, "decision-log mirror");
static_assert(oq_mail::event::INCIDENT_START == dl::EVENT_INCIDENT_START, "decision-log mirror");
static_assert(oq_mail::event::INCIDENT_CLEAR == dl::EVENT_INCIDENT_CLEAR, "decision-log mirror");
static_assert(oq_mail::event::BOILER_FALLBACK_START == dl::EVENT_BOILER_FALLBACK_START, "decision-log mirror");
static_assert(oq_mail::event::BOILER_FALLBACK_STOP == dl::EVENT_BOILER_FALLBACK_STOP, "decision-log mirror");
static_assert(oq_mail::reason::START_STOP_RATE_HIGH == dl::REASON_START_STOP_RATE_HIGH, "decision-log mirror");
static_assert(oq_mail::reason::HP_FAULT == dl::REASON_HP_FAULT, "decision-log mirror");
static_assert(oq_mail::reason::HP_PROTECTION == dl::REASON_HP_PROTECTION, "decision-log mirror");
static_assert(oq_mail::reason::HP_PREHEAT == dl::REASON_HP_PREHEAT, "decision-log mirror");
static_assert(oq_mail::reason::HP_LINK_LOSS == dl::REASON_HP_LINK_LOSS, "decision-log mirror");
static_assert(oq_mail::reason::HP_START_FAILED == dl::REASON_HP_START_FAILED, "decision-log mirror");
static_assert(oq_mail::reason::HP_STOP_UNCONFIRMED == dl::REASON_HP_STOP_UNCONFIRMED, "decision-log mirror");
static_assert(oq_mail::reason::HP_PERSISTENCE_FAILURE == dl::REASON_HP_PERSISTENCE_FAILURE, "decision-log mirror");
static_assert(oq_mail::severity::NORMAL == dl::SEVERITY_NORMAL, "decision-log mirror");
static_assert(oq_mail::severity::LIMITED == dl::SEVERITY_LIMITED, "decision-log mirror");
static_assert(oq_mail::severity::ATTENTION == dl::SEVERITY_ATTENTION, "decision-log mirror");
static_assert(oq_mail::severity::FAULT == dl::SEVERITY_FAULT, "decision-log mirror");
static_assert(oq_mail::subject::HP1 == dl::SUBJECT_HP1, "decision-log mirror");
static_assert(oq_mail::subject::HP2 == dl::SUBJECT_HP2, "decision-log mirror");
static_assert(oq_mail::subject::BOTH == dl::SUBJECT_BOTH, "decision-log mirror");
static_assert(oq_mail::subject::CV == dl::SUBJECT_CV, "decision-log mirror");
static_assert(oq_mail::flag::LATCHED == dl::FLAG_LATCHED, "decision-log mirror");
static_assert(oq_mail::flag::MANUAL_RESET_REQUIRED == dl::FLAG_MANUAL_RESET_REQUIRED, "decision-log mirror");
static_assert(oq_mail::flag::RESTORED_AFTER_BOOT == dl::FLAG_RESTORED_AFTER_BOOT, "decision-log mirror");

static uint64_t monotonic_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL; }

namespace {
class Locked {
 public:
  explicit Locked(SemaphoreHandle_t lock) : lock_(lock) { xSemaphoreTake(lock_, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(lock_); }
  Locked(const Locked&) = delete;
  Locked& operator=(const Locked&) = delete;

 private:
  SemaphoreHandle_t lock_;
};
}  // namespace

// ---------------------------------------------------------------------------
// Setup and configuration
// ---------------------------------------------------------------------------

void OpenQuattMailNotify::setup() {
  lock_ = xSemaphoreCreateMutexStatic(&lock_storage_);
  if (lock_ == nullptr || !storage_.allocate()) {
    ESP_LOGE(TAG, "Strict PSRAM allocation failed; e-mail notifications disabled");
    storage_.release();
    register_http_handler(this);  // Fail closed with 503 instead of a missing route.
    mark_failed();
    return;
  }
  auto& state = storage_[0];
  state.error = "none";
  load_config_();
  load_budget_();
  apply_active_runtime_();
  queue_restart_notice_();
  if (pause_ != nullptr) {
    pause_->add_on_state_callback([this](bool paused) {
      if (paused) cancel_.store(true, std::memory_order_release);
    });
  }
  if (decision_log_ == nullptr || !decision_log_->add_event_listener(&OpenQuattMailNotify::on_decision_event_, this)) {
    ESP_LOGE(TAG, "Decision-log listener unavailable; only test mails can be sent");
  }
  ota::get_global_ota_callback()->add_global_state_listener(this);
  register_http_handler(this);
}

void OpenQuattMailNotify::load_config_() {
  auto& state = storage_[0];
  state.active = oq_mail::default_config();
  nvs_handle_t handle;
  esp_err_t result = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (result == ESP_OK) {
    size_t size = sizeof(state.inbox);
    result = nvs_get_blob(handle, kNvsKey, &state.inbox, &size);
    nvs_close(handle);
    if (result == ESP_OK && size == sizeof(state.inbox) && oq_mail::valid_config_record(state.inbox) &&
        oq_mail::validate(state.inbox, state.inbox.enabled != 0U) == oq_mail::ValidationError::NONE) {
      state.active = state.inbox;
    } else if (result != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGE(TAG, "Stored e-mail configuration is invalid; notifications stay off until saved again");
      Locked locked(lock_);
      published_.save_state = SaveState::FAILED;
      published_.save_error = "stored_config_invalid";
    }
  } else if (result != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "E-mail configuration unreadable (%s)", esp_err_to_name(result));
  }
  std::memset(&state.inbox, 0, sizeof(state.inbox));
}

// The daily limit survives reboots, so a crash or brownout loop cannot flood
// the recipients (each boot may queue a restart notice).
void OpenQuattMailNotify::load_budget_() {
  auto& state = storage_[0];
  nvs_handle_t handle;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;
  size_t size = sizeof(state.budget_record);
  const esp_err_t result = nvs_get_blob(handle, kNvsBudgetKey, &state.budget_record, &size);
  nvs_close(handle);
  if (result == ESP_OK && size == sizeof(state.budget_record) && oq_mail::valid_budget_record(state.budget_record)) {
    budget_.load(state.budget_record.sent_s);
  } else if (result != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Stored e-mail send budget ignored");
  }
}

// Main loop only. Publishes the runtime gate used by the decision-log listener.
void OpenQuattMailNotify::apply_active_runtime_() {
  const auto& active = storage_[0].active;
  const bool complete = oq_mail::config_complete(active);
  kind_mask_.store(active.kind_mask, std::memory_order_relaxed);
  recovery_.store(active.recovery != 0U, std::memory_order_relaxed);
  accepting_.store(active.enabled != 0U && complete, std::memory_order_release);
  Locked locked(lock_);
  published_.configured = complete;
}

void OpenQuattMailNotify::queue_restart_notice_() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
      restart_cause_ = oq_mail::reset_cause::PANIC;
      break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      restart_cause_ = oq_mail::reset_cause::WATCHDOG;
      break;
    case ESP_RST_BROWNOUT:
      restart_cause_ = oq_mail::reset_cause::BROWNOUT;
      break;
    default:
      return;
  }
  restart_notice_pending_ = true;
}

// ---------------------------------------------------------------------------
// Event intake
// ---------------------------------------------------------------------------

// Runs in the emitter's context (main loop). Bounded: classify and copy only.
void OpenQuattMailNotify::on_decision_event_(void* context, const dl::DecisionEvent& event) {
  auto* self = static_cast<OpenQuattMailNotify*>(context);
  if (!self->accepting_.load(std::memory_order_acquire)) return;
  oq_mail::EventInput input;
  input.epoch_s = event.epoch_s;
  input.duration_s = event.duration_s;
  input.value_a = event.value_a;
  input.value_b = event.value_b;
  input.threshold_a = event.threshold_a;
  input.event_type = event.event_type;
  input.subject = event.subject;
  input.reason_code = event.reason_code;
  input.severity = event.severity;
  input.flags = event.flags;
  oq_mail::Notice notice;
  if (!oq_mail::classify(input, notice) || !oq_mail::selected(notice, self->kind_mask_.load(std::memory_order_relaxed),
                                                              self->recovery_.load(std::memory_order_relaxed))) {
    return;
  }
  auto& state = self->storage_[0];
  portENTER_CRITICAL(&self->event_mux_);
  if (self->ring_count_ < kRingSize) {
    state.ring[(self->ring_head_ + self->ring_count_) % kRingSize] = notice;
    ++self->ring_count_;
  } else {
    ++self->ring_dropped_;
  }
  portEXIT_CRITICAL(&self->event_mux_);
}

void OpenQuattMailNotify::drain_ring_(uint64_t now_ms) {
  auto& state = storage_[0];
  oq_mail::Notice drained[kRingSize];
  size_t count = 0;
  uint32_t dropped = 0;
  portENTER_CRITICAL(&event_mux_);
  while (ring_count_ > 0) {
    drained[count++] = state.ring[ring_head_];
    ring_head_ = (ring_head_ + 1) % kRingSize;
    --ring_count_;
  }
  dropped = ring_dropped_;
  ring_dropped_ = 0;
  portEXIT_CRITICAL(&event_mux_);

  if (restart_notice_pending_ && accepting_.load(std::memory_order_relaxed)) {
    oq_mail::Notice notice;
    notice.kind = static_cast<uint8_t>(oq_mail::Kind::CONTROLLER_RESTART);
    notice.subject = oq_mail::subject::CONTROLLER;
    notice.value_a = restart_cause_;
    if (oq_mail::selected(notice, kind_mask_.load(std::memory_order_relaxed), false) && count < kRingSize) {
      drained[count++] = notice;
    }
  }
  restart_notice_pending_ = false;

  if (!accepting_.load(std::memory_order_relaxed)) return;
  if ((count > 0 || dropped > 0) && batch_count_ == 0 && batch_overflow_ == 0) batch_started_ms_ = now_ms;
  for (size_t index = 0; index < count; ++index) {
    if (batch_count_ < kBatchMax) {
      state.batch[batch_count_++] = drained[index];
    } else {
      ++batch_overflow_;
    }
  }
  batch_overflow_ += dropped;
}

void OpenQuattMailNotify::clear_pending_notifications_() {
  batch_count_ = 0;
  batch_overflow_ = 0;
  outgoing_ready_ = false;
  outgoing_attempts_ = 0;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void OpenQuattMailNotify::loop() {
  if (!storage_) return;
  const uint64_t now_ms = monotonic_ms();
  update_ota_hold_();
  if (phase_.load(std::memory_order_acquire) == Phase::READY) handle_result_(now_ms);
  drain_ring_(now_ms);
  if (!accepting_.load(std::memory_order_relaxed)) clear_pending_notifications_();
  if (phase_.load(std::memory_order_acquire) == Phase::IDLE && !dispatch_save_() && !dispatch_budget_save_() &&
      !dispatch_test_(now_ms)) {
    prepare_outgoing_(now_ms);
    dispatch_notification_(now_ms);
  }
  publish_counters_(now_ms);
}

void OpenQuattMailNotify::update_ota_hold_() {
  if (ota_hold_.load(std::memory_order_acquire) &&
      millis() - ota_progress_ms_.load(std::memory_order_relaxed) >= OTA_QUIET_TIMEOUT_MS) {
    ota_hold_.store(false, std::memory_order_release);
  }
}

bool OpenQuattMailNotify::paused_() const {
  return ota_hold_.load(std::memory_order_acquire) || (pause_ != nullptr && pause_->state);
}

bool OpenQuattMailNotify::current_epoch_(uint32_t& epoch_s) const {
  if (clock_ == nullptr) return false;
  const ESPTime now = clock_->now();
  if (!now.is_valid()) return false;
  epoch_s = static_cast<uint32_t>(now.timestamp);
  return true;
}

void OpenQuattMailNotify::on_ota_global_state(ota::OTAState state, float, uint8_t, ota::OTAComponent*) {
  if (state == ota::OTA_STARTED || state == ota::OTA_IN_PROGRESS) {
    ota_progress_ms_.store(millis(), std::memory_order_relaxed);
    ota_hold_.store(true, std::memory_order_release);
    // HTTP OTA waits briefly for the shared HTTPS slot: abort the exchange now.
    cancel_.store(true, std::memory_order_release);
  } else if (state == ota::OTA_ERROR || state == ota::OTA_ABORT) {
    ota_hold_.store(false, std::memory_order_release);
  }
}

void OpenQuattMailNotify::on_shutdown() { cancel_.store(true, std::memory_order_release); }

bool OpenQuattMailNotify::acquire_https_slot_() {
#ifdef USE_OPENQUATT_WEATHER
  // Yields to a waiting OTA manifest check, like the weather client.
  return openquatt_weather::https_slot.try_acquire_weather();
#else
  return true;
#endif
}

void OpenQuattMailNotify::release_https_slot_() {
#ifdef USE_OPENQUATT_WEATHER
  openquatt_weather::https_slot.release();
#endif
}

bool OpenQuattMailNotify::start_job_(Job job, uint64_t now_ms) {
  auto& state = storage_[0];
  if (worker_ == nullptr && !worker_attempted_) {
    worker_attempted_ = true;
    if (xTaskCreate(&OpenQuattMailNotify::worker_entry_, "oq_mail", MAIL_TASK_STACK_SIZE, this, tskIDLE_PRIORITY + 1,
                    &worker_) != pdPASS) {
      worker_ = nullptr;
      ESP_LOGE(TAG, "Mail worker task allocation failed");
    }
  }
  if (worker_ == nullptr) return false;
  state.job = job;
  state.result_ok = false;
  state.error = "none";
  state.code = 0;
  state.permanent = false;
  state.request_ms = now_ms;
  cancel_.store(false, std::memory_order_release);
  // An OTA or pause that started before the reset above must still cancel this job.
  if (paused_()) cancel_.store(true, std::memory_order_release);
  phase_.store(Phase::REQUESTED, std::memory_order_release);
  xTaskNotifyGive(worker_);
  return true;
}

bool OpenQuattMailNotify::dispatch_save_() {
  auto& state = storage_[0];
  if (paused_()) return false;  // Keep flash work for after OTA or a service hold.
  {
    Locked locked(lock_);
    if (!published_.inbox_pending) return false;
    state.job_config = state.inbox;
    job_revision_ = inbox_revision_;
    published_.inbox_pending = false;
  }
  if (!start_job_(Job::SAVE, monotonic_ms())) {
    Locked locked(lock_);
    if (job_revision_ == inbox_revision_) {
      published_.save_state = SaveState::FAILED;
      published_.save_error = "worker_unavailable";
    }
  }
  return true;
}

bool OpenQuattMailNotify::dispatch_budget_save_() {
  if (!budget_dirty_ || paused_()) return false;
  storage_[0].budget_record = oq_mail::budget_record(budget_);
  budget_dirty_ = false;
  if (!start_job_(Job::SAVE_BUDGET, monotonic_ms())) {
    ESP_LOGW(TAG, "Send budget not persisted: worker unavailable");
    return false;
  }
  return true;
}

bool OpenQuattMailNotify::dispatch_test_(uint64_t now_ms) {
  auto& state = storage_[0];
  uint32_t test_id = 0;
  {
    Locked locked(lock_);
    if (!published_.test_requested) return false;
    test_id = published_.test_id;
    const char* failure = nullptr;
    if (!published_.configured) {
      failure = "not_configured";
    } else if (!network::is_connected()) {
      failure = "network_unavailable";
    } else if (paused_()) {
      failure = "cancelled";
    }
    if (failure != nullptr) {
      published_.test_requested = false;
      published_.test_state = TestState::FAILED;
      published_.test_error = failure;
      published_.test_code = 0;
      return false;
    }
  }
  if (!acquire_https_slot_()) return false;  // Retry on a later loop.
  oq_mail::Buffer subject(state.test_subject, sizeof(state.test_subject));
  oq_mail::append_test_subject(subject, device_name_());
  char url[64];
  web_url_(url, sizeof(url));
  oq_mail::Buffer body(state.test_body, sizeof(state.test_body));
  oq_mail::append_test_body(body, device_name_(), url);
  state.job_config = state.active;
  prepare_job_headers_();
  job_test_id_ = test_id;
  if (!start_job_(Job::SEND_TEST, now_ms)) {
    release_https_slot_();
    Locked locked(lock_);
    published_.test_requested = false;
    published_.test_state = TestState::FAILED;
    published_.test_error = "worker_unavailable";
    return false;
  }
  Locked locked(lock_);
  published_.test_requested = false;
  published_.test_state = TestState::SENDING;
  return true;
}

void OpenQuattMailNotify::prepare_outgoing_(uint64_t now_ms) {
  if (outgoing_ready_ || (batch_count_ == 0 && batch_overflow_ == 0) || !accepting_.load(std::memory_order_relaxed))
    return;
  // The persisted daily limit is kept in wall-clock time: wait for a valid clock.
  uint32_t now_s = 0;
  if (now_ms - batch_started_ms_ < oq_mail::kCoalesceWindowMs || !current_epoch_(now_s) || !budget_.allowed(now_s))
    return;
  auto& state = storage_[0];
  // A restart notice is queued before the clock is valid; date it at boot.
  const uint32_t boot_epoch = now_s - static_cast<uint32_t>(now_ms / 1000ULL);
  for (size_t index = 0; index < batch_count_; ++index) {
    if (state.batch[index].epoch_s == 0U &&
        state.batch[index].kind == static_cast<uint8_t>(oq_mail::Kind::CONTROLLER_RESTART)) {
      state.batch[index].epoch_s = boot_epoch;
    }
  }
  char url[64];
  web_url_(url, sizeof(url));
  oq_mail::Buffer subject(state.subject, sizeof(state.subject));
  oq_mail::append_subject(subject, device_name_(), state.batch, batch_count_, batch_overflow_);
  oq_mail::Buffer body(state.body, sizeof(state.body));
  oq_mail::append_body(body, device_name_(), url, state.batch, batch_count_, batch_overflow_,
                       [this](uint32_t epoch, char* out, size_t size) { return format_local_time_(epoch, out, size); });
  if (!subject.ok() || !body.ok()) ESP_LOGW(TAG, "Notification text truncated to its bounded buffer");
  outgoing_lines_ = static_cast<uint32_t>(batch_count_) + batch_overflow_;
  batch_count_ = 0;
  batch_overflow_ = 0;
  outgoing_ready_ = true;
  outgoing_attempts_ = 0;
  outgoing_next_ms_ = now_ms;
}

void OpenQuattMailNotify::dispatch_notification_(uint64_t now_ms) {
  uint32_t now_s = 0;
  if (!outgoing_ready_ || now_ms < outgoing_next_ms_ || !accepting_.load(std::memory_order_relaxed) || paused_() ||
      !network::is_connected() || !current_epoch_(now_s)) {
    return;
  }
  if (!acquire_https_slot_()) return;
  job_epoch_s_ = now_s;
  auto& state = storage_[0];
  state.job_config = state.active;
  prepare_job_headers_();
  if (!start_job_(Job::SEND_NOTIFICATION, now_ms)) {
    release_https_slot_();
    outgoing_next_ms_ = now_ms + oq_mail::retry_delay_ms(1);
  }
}

void OpenQuattMailNotify::prepare_job_headers_() {
  auto& state = storage_[0];
  state.date_header[0] = '\0';
  if (clock_ != nullptr && clock_->now().is_valid()) {
    ESPTime utc = ESPTime::from_epoch_utc(clock_->now().timestamp);
    utc.strftime(state.date_header, sizeof(state.date_header), "%a, %d %b %Y %H:%M:%S +0000");
  }
  // RFC 5321 wants an FQDN or an address literal; the device has no FQDN.
  std::snprintf(state.ehlo_host, sizeof(state.ehlo_host), "openquatt");
  for (const auto& address : network::get_ip_addresses()) {
    if (!address.is_set() || !address.is_ip4()) continue;
    char text[network::IP_ADDRESS_BUFFER_SIZE];
    address.str_to(text);
    std::snprintf(state.ehlo_host, sizeof(state.ehlo_host), "[%s]", text);
    break;
  }
  std::snprintf(state.message_id, sizeof(state.message_id), "%08x%08x.%u@openquatt",
                static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(millis()));
}

void OpenQuattMailNotify::handle_result_(uint64_t now_ms) {
  auto& state = storage_[0];
  if (state.internal_largest_low != 0U &&
      (worker_largest_low_ == 0U || state.internal_largest_low < worker_largest_low_))
    worker_largest_low_ = state.internal_largest_low;
  if (state.stack_free != 0U && (worker_stack_min_ == 0U || state.stack_free < worker_stack_min_))
    worker_stack_min_ = state.stack_free;
  const bool cancelled = std::strcmp(state.error, "cancelled") == 0;

  switch (state.job) {
    case Job::SAVE: {
      if (state.result_ok) {
        {
          // HTTPD copies `active` under the lock; never let it observe a torn record.
          Locked locked(lock_);
          state.active = state.job_config;
        }
        apply_active_runtime_();
        if (!accepting_.load(std::memory_order_relaxed)) clear_pending_notifications_();
      }
      Locked locked(lock_);
      if (job_revision_ == inbox_revision_) {
        published_.save_state = state.result_ok ? SaveState::STORED : SaveState::FAILED;
        published_.save_error = state.result_ok ? "none" : state.error;
      }
      ESP_LOGI(TAG, "Configuration revision %u %s", static_cast<unsigned>(job_revision_),
               state.result_ok ? "stored" : "not stored");
      break;
    }
    case Job::SAVE_BUDGET:
      if (!state.result_ok) ESP_LOGW(TAG, "Send budget not persisted: %s", state.error);
      break;
    case Job::SEND_TEST: {
      Locked locked(lock_);
      if (published_.test_id == job_test_id_) {
        published_.test_state = state.result_ok ? TestState::SENT : TestState::FAILED;
        published_.test_error = state.error;
        published_.test_code = state.code;
      }
      break;
    }
    case Job::SEND_NOTIFICATION: {
      bool final_failure = false;
      if (state.result_ok) {
        budget_.record(job_epoch_s_);
        budget_dirty_ = true;
        outgoing_ready_ = false;
        outgoing_attempts_ = 0;
      } else if (!cancelled) {
        ++outgoing_attempts_;
        final_failure = state.permanent || outgoing_attempts_ >= oq_mail::kMaxSendAttempts;
        if (final_failure) {
          outgoing_ready_ = false;
          ESP_LOGW(TAG, "Notification dropped after %u attempt(s): %s (%d)", static_cast<unsigned>(outgoing_attempts_),
                   state.error, state.code);
        } else {
          outgoing_next_ms_ = now_ms + oq_mail::retry_delay_ms(outgoing_attempts_);
        }
      }
      if (!cancelled) {
        Locked locked(lock_);
        published_.last_state = state.result_ok ? LastState::SENT : LastState::FAILED;
        published_.last_epoch = clock_ != nullptr && clock_->now().is_valid() ? clock_->now().timestamp : 0U;
        published_.last_error = state.error;
        published_.last_code = state.code;
      }
      break;
    }
  }
  ESP_LOGI(TAG, "Job done: ok=%d error=%s code=%d duration=%u ms internal=%u/min=%u/largest=%u B stack=%u B free",
           state.result_ok, state.error, state.code, static_cast<unsigned>(state.duration_ms),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(state.stack_free));
  phase_.store(Phase::IDLE, std::memory_order_release);
}

void OpenQuattMailNotify::publish_counters_(uint64_t now_ms) {
  if (now_ms - last_counter_publish_ms_ < COUNTER_PUBLISH_MS) return;
  last_counter_publish_ms_ = now_ms;
  const uint32_t queued =
      static_cast<uint32_t>(batch_count_) + batch_overflow_ + (outgoing_ready_ ? outgoing_lines_ : 0U);
  uint32_t now_s = 0;
  const uint32_t sent = current_epoch_(now_s) ? budget_.used(now_s) : 0U;
  Locked locked(lock_);
  published_.queued = queued;
  published_.sent_24h = sent;
}

// ---------------------------------------------------------------------------
// Rendering helpers (main loop)
// ---------------------------------------------------------------------------

const char* OpenQuattMailNotify::device_name_() const {
  const std::string& friendly = App.get_friendly_name();
  return friendly.empty() ? App.get_name().c_str() : friendly.c_str();
}

void OpenQuattMailNotify::web_url_(char* out, size_t size) const {
  out[0] = '\0';
  for (const auto& address : network::get_ip_addresses()) {
    if (!address.is_set() || !address.is_ip4()) continue;
    char text[network::IP_ADDRESS_BUFFER_SIZE];
    address.str_to(text);
    std::snprintf(out, size, "http://%s/", text);
    return;
  }
}

bool OpenQuattMailNotify::format_local_time_(uint32_t epoch_s, char* out, size_t size) const {
  if (epoch_s == 0U) return false;
  ESPTime local = ESPTime::from_epoch_local(static_cast<time_t>(epoch_s));
  return local.is_valid() && local.strftime(out, size, "%d-%m-%Y %H:%M") > 0;
}

// ---------------------------------------------------------------------------
// HTTPD-facing state transitions
// ---------------------------------------------------------------------------

bool OpenQuattMailNotify::authenticated(AsyncWebServerRequest* request) const {
  return web_auth_ != nullptr && web_auth_->request_is_authenticated(request);
}

std::string OpenQuattMailNotify::csrf_token() const { return web_auth_ != nullptr ? web_auth_->get_csrf_token() : ""; }

static void trim(std::string& text) {
  const size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    text.clear();
    return;
  }
  // Erase in place: no reallocated copy is left behind (the secret goes through here).
  text.erase(text.find_last_not_of(" \t\r\n") + 1);
  text.erase(0, start);
}

OpenQuattMailNotify::SubmitResult OpenQuattMailNotify::submit(const SaveRequest& request,
                                                              oq_mail::ValidationError& error, uint32_t& revision) {
  using oq_mail::ValidationError;
  error = ValidationError::NONE;
  if (!available()) return SubmitResult::UNAVAILABLE;
  auto& candidate = storage_[0].http_config;
  {
    // Build on the newest accepted revision, also while its NVS write runs:
    // `active` only changes once that write has been verified.
    Locked locked(lock_);
    const bool newer = published_.inbox_pending || published_.save_state == SaveState::PENDING;
    candidate = newer ? storage_[0].inbox : storage_[0].active;
  }
  const uint8_t previous_provider = candidate.provider;

  oq_mail::Provider provider = oq_mail::Provider::NONE;
  if (!oq_mail::parse_provider(request.provider.c_str(), provider)) {
    error = ValidationError::INVALID_PROVIDER;
    return SubmitResult::INVALID;
  }
  candidate.provider = static_cast<uint8_t>(provider);
  candidate.enabled = request.enabled ? 1U : 0U;
  candidate.recovery = request.recovery ? 1U : 0U;

  std::string host = request.smtp_host, username = request.username, domain = request.domain, from = request.from;
  std::string secret = request.secret;
  struct WipeSecret {
    std::string& text;
    ~WipeSecret() {
      if (!text.empty()) oq_mail::secure_wipe(&text[0], text.size());
    }
  } wipe_secret{secret};
  trim(host);
  trim(username);
  trim(domain);
  trim(from);
  if (oq_mail::is_api_provider(provider)) trim(secret);
  if (!oq_mail::copy_field(candidate.smtp_host, host.c_str(), host.size()) ||
      !oq_mail::copy_field(candidate.username, username.c_str(), username.size()) ||
      !oq_mail::copy_field(candidate.domain, domain.c_str(), domain.size()) ||
      !oq_mail::copy_field(candidate.from, from.c_str(), from.size())) {
    error = ValidationError::FIELD_TOO_LONG;
    return SubmitResult::INVALID;
  }

  // The port belongs to SMTP only; other providers keep the stored draft value.
  if (provider != oq_mail::Provider::SMTP) {
    if (candidate.smtp_port == 0U) candidate.smtp_port = oq_mail::kDefaultSmtpPort;
  } else if (request.smtp_port.empty()) {
    candidate.smtp_port = oq_mail::kDefaultSmtpPort;
  } else {
    char* end = nullptr;
    const unsigned long port = std::strtoul(request.smtp_port.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || port == 0UL || port > 65535UL) {
      error = ValidationError::INVALID_PORT;
      return SubmitResult::INVALID;
    }
    candidate.smtp_port = static_cast<uint16_t>(port);
  }

  // Keep a stored secret unless replaced or cleared. A provider switch never
  // carries one provider's credential over to another.
  if (request.clear_secret || (secret.empty() && candidate.provider != previous_provider)) {
    std::memset(candidate.secret, 0, sizeof(candidate.secret));
  } else if (!secret.empty() && !oq_mail::copy_field(candidate.secret, secret.c_str(), secret.size())) {
    error = ValidationError::FIELD_TOO_LONG;
    return SubmitResult::INVALID;
  }

  if (request.recipients.size() > oq_mail::kRecipientsMax * 2U) {
    error = ValidationError::FIELD_TOO_LONG;
    return SubmitResult::INVALID;
  }
  size_t count = 0;
  error = oq_mail::canonicalize_recipients(request.recipients.c_str(), candidate.recipients,
                                           sizeof(candidate.recipients), count);
  if (error != ValidationError::NONE) return SubmitResult::INVALID;

  uint16_t kinds = 0;
  if (!oq_mail::parse_kind_list(request.kinds.c_str(), kinds)) {
    error = ValidationError::INVALID_KINDS;
    return SubmitResult::INVALID;
  }
  candidate.kind_mask = kinds;

  error = oq_mail::validate(candidate, candidate.enabled != 0U);
  if (error != ValidationError::NONE) return SubmitResult::INVALID;
  oq_mail::seal(candidate);

  Locked locked(lock_);
  storage_[0].inbox = candidate;
  published_.inbox_pending = true;
  ++inbox_revision_;
  if (inbox_revision_ == 0U) inbox_revision_ = 1U;
  published_.save_revision = inbox_revision_;
  published_.save_state = SaveState::PENDING;
  published_.save_error = "none";
  revision = inbox_revision_;
  return SubmitResult::ACCEPTED;
}

OpenQuattMailNotify::TestResult OpenQuattMailNotify::request_test(uint32_t& test_id) {
  if (!available()) return TestResult::UNAVAILABLE;
  Locked locked(lock_);
  if (published_.inbox_pending || published_.save_state == SaveState::PENDING) return TestResult::SAVE_PENDING;
  if (!published_.configured) return TestResult::NOT_CONFIGURED;
  if (published_.test_requested || published_.test_state == TestState::SENDING) return TestResult::TEST_BUSY;
  const uint32_t now = millis();
  if (published_.test_id != 0U && now - published_.last_test_request_ms < oq_mail::kTestMinIntervalMs) {
    return TestResult::RATE_LIMITED;
  }
  ++published_.test_id;
  if (published_.test_id == 0U) published_.test_id = 1U;
  published_.test_requested = true;
  published_.test_state = TestState::QUEUED;
  published_.test_error = "none";
  published_.test_code = 0;
  published_.last_test_request_ms = now;
  test_id = published_.test_id;
  return TestResult::ACCEPTED;
}

void OpenQuattMailNotify::dump_config() {
  ESP_LOGCONFIG(TAG, "E-mail notifications");
  if (!storage_) {
    ESP_LOGCONFIG(TAG, "  Unavailable: strict PSRAM allocation failed");
    return;
  }
  const auto& active = storage_[0].active;
  ESP_LOGCONFIG(TAG, "  Enabled: %s, provider: %s, complete: %s", YESNO(active.enabled != 0U),
                oq_mail::provider_key(static_cast<oq_mail::Provider>(active.provider)),
                YESNO(oq_mail::config_complete(active)));
  ESP_LOGCONFIG(TAG, "  Strict PSRAM: %u B; internal worker stack: %u B (created on first job)",
                static_cast<unsigned>(sizeof(Storage)), static_cast<unsigned>(MAIL_TASK_STACK_SIZE));
}

}  // namespace esphome::openquatt_mail_notify
