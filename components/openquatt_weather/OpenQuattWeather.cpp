#include "OpenQuattWeather.h"
#include "WeatherHttpsSlot.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "esphome/components/network/util.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"

namespace esphome::openquatt_weather {

static const char* const TAG = "openquatt.weather";
static constexpr uint32_t WEATHER_TASK_STACK_SIZE = 6144;
static uint64_t monotonic_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL; }

static const char* const CONSENT_PENDING = "consent_persistence_pending";
static const char* const CONSENT_FAILED = "consent_persistence_failed";

static esp_err_t read_consent(uint8_t& value) {
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open("oq_weather", NVS_READONLY, &handle);
  if (opened != ESP_OK) return opened;
  const esp_err_t result = nvs_get_u8(handle, "enabled", &value);
  nvs_close(handle);
  return result;
}

void WeatherEnabledSwitch::write_state(bool enabled) {
  if (parent_) parent_->set_consent(enabled);
}

void WeatherLocationText::control(const std::string& value) {
  if (parent_) parent_->set_location_query(value);
}

bool OpenQuattWeather::allowed_() const {
  return consent_state_ == ConsentState::ON && enabled_ && enabled_->state && pause_ && !pause_->state &&
         !ota_paused_ && !configuration_error_ && !pending_save_;
}

void OpenQuattWeather::publish_location_(const char* value, const char* request, SaveState state) {
  text_snapshots_.publish(static_cast<EntityBase*>(location_text_), value, request, state);
  location_text_->publish_state(value);
}

void OpenQuattWeather::publish_text_(text_sensor::TextSensor* entity, const char* value) {
  if (!entity) return;
  const auto consent = entity == status_ ? consent_state_ : ConsentState::NONE;
  text_snapshots_.publish(static_cast<EntityBase*>(entity), value, "", SaveState::NONE, consent);
  entity->publish_state(value);
}

void OpenQuattWeather::load_consent_() {
  uint8_t value = 0;
  const auto result = read_consent(value);
  // Only the dedicated record authorizes outbound requests. The former
  // optimistic switch preference is never read, including on upgrade.
  if (result == ESP_ERR_NVS_NOT_FOUND || (result == ESP_OK && value == 0))
    consent_state_ = ConsentState::OFF;
  else if (result == ESP_OK && value == 1)
    consent_state_ = ConsentState::ON;
  else
    consent_state_ = ConsentState::FAILED;
  enabled_->publish_state(consent_state_ == ConsentState::ON);
}

void OpenQuattWeather::load_location_() {
  nvs_handle_t handle;
  oq_weather::LocationRecord record;
  size_t size = sizeof(record);
  const esp_err_t opened = nvs_open("oq_weather", NVS_READONLY, &handle);
  esp_err_t result = opened;
  if (opened == ESP_OK) {
    result = nvs_get_blob(handle, "location", &record, &size);
    nvs_close(handle);
  }
  if (result == ESP_OK && size == sizeof(record) && oq_weather::valid_location_record(record)) {
    oq_weather::parse_location(record.query, std::strlen(record.query), storage_[0].location);
    location_resolved_ = storage_[0].location.kind == oq_weather::LocationKind::COORDINATES;
  } else if (result != ESP_ERR_NVS_NOT_FOUND) {
    configuration_error_ = true;
    last_error_ = "stored_location_invalid";
  }
  publish_location_(storage_[0].location.query);
}

void OpenQuattWeather::setup() {
  EntityBase* text_entities[]{location_text_, status_, resolved_, issued_epoch_};
  for (size_t slot = 0; slot < WeatherTextSnapshots::kSlots; ++slot) {
    auto* entity = text_entities[slot];
    if (entity)
      text_snapshots_.bind(slot, entity,
                           {static_cast<uint8_t>(entity->get_entity_category()), entity->is_disabled_by_default()});
  }
  text_snapshots_.bind_location_handler(this, [](void* context, const char* value, const char* request) {
    static_cast<OpenQuattWeather*>(context)->set_location_query(value, request);
  });
  published_weather_snapshots.store(&text_snapshots_, std::memory_order_release);
  if (!storage_.allocate()) {
#ifdef USE_ESP32
    setup_forecast_endpoint_();  // Register a fail-closed 503 route even on allocation failure.
#endif
    ESP_LOGE(TAG, "Strict PSRAM allocation failed; weather experiment disabled");
    consent_state_ = ConsentState::FAILED;
    enabled_->publish_state(false);
    publish_text_(status_, "psram_allocation_failed");
    mark_failed();
    return;
  }
  forecast_snapshots_.bind(&storage_[0].public_snapshot, &storage_[0].request_snapshot);
#ifdef USE_ESP32
  setup_forecast_endpoint_();
#endif
  load_consent_();
  load_location_();
  ota::get_global_ota_callback()->add_global_state_listener(this);
  pause_->add_on_state_callback([this](bool paused) {
    if (paused) invalidate_();
  });
  next_fetch_ms_ = monotonic_ms() + 30000;
  publish_();
}

void OpenQuattWeather::invalidate_() {
  cancel_.store(true, std::memory_order_release);
  oq_weather::cancel_queued_work(work_gate_);
  if (++generation_ == 0) generation_ = 1;
  active_ = false;
  next_fetch_ms_ = monotonic_ms();
  backoff_ms_ = 60000;
  if (storage_) {
    storage_[0].current.generation = 0;
    oq_power_house::forecast::discard_pending(storage_[0].forecast);
    storage_[0].issued_scenario = {};
  }
  publish_forecast_();
}

void OpenQuattWeather::set_consent(bool enabled) {
  if (!storage_) return;  // Setup already published failed proof and the allocation diagnostic.
  if (consent_state_ == (enabled ? ConsentState::ON : ConsentState::OFF)) return;
  if (++consent_revision_ == 0) consent_revision_ = 1;
  consent_target_ = enabled;
  consent_state_ = ConsentState::PENDING;
  // Stop outbound work immediately. The switch continues to show the last
  // verified value so a failed revocation can be retried with another OFF.
  invalidate_();
  publish_text_(status_, CONSENT_PENDING);
  if (!worker_) worker_attempted_ = false;  // One new attempt per explicit action.
  request_pending_save_();
  publish_();
}

void OpenQuattWeather::set_location_query(const std::string& query, const std::string& request) {
  if (!storage_) return;
  if (!request.empty() && !valid_save_request(request.data(), request.size())) return;
  auto& state = storage_[0];
  oq_weather::Location location;
  if (oq_weather::parse_location(query.data(), query.size(), location) == oq_weather::LocationKind::INVALID) {
    last_error_ = "invalid_location";
    // A FAILED receipt can trigger another HTTP request immediately. Publish
    // the invalid atomic scenario before announcing that new receipt.
    publish_forecast_();
    publish_location_(state.location.query, request.c_str(), SaveState::FAILED);
    publish_();
    return;
  }
  if (!pending_save_ && !configuration_error_ && std::strcmp(location.query, state.location.query) == 0) {
    last_error_ = "none";
    publish_location_(state.location.query, request.c_str(), SaveState::STORED);
    return;
  }
  invalidate_();
  for (auto& errors : state.forecast.errors) errors = {};
  if (++configuration_ == 0) configuration_ = 1;
  state.pending_location = location;
  std::memcpy(state.pending_save_request, request.c_str(), request.size() + 1);
  pending_save_ = true;
  configuration_error_ = false;
  last_error_ = "location_saving";
  publish_location_(state.location.query, request.c_str(), SaveState::PENDING);
  publish_text_(resolved_, "");
  request_pending_save_();
  publish_();
}

void OpenQuattWeather::on_shutdown() { invalidate_(); }

void OpenQuattWeather::on_ota_global_state(ota::OTAState state, float, uint8_t, ota::OTAComponent* owner) {
  if (state == ota::OTA_STARTED || state == ota::OTA_IN_PROGRESS) {
    ota_pause_.progress(owner, monotonic_ms());
    ota_paused_ = true;
    invalidate_();
  } else if (state == ota::OTA_ERROR || state == ota::OTA_ABORT) {
    ota_pause_.failed(owner);
    ota_paused_ = ota_pause_.active(monotonic_ms(), pause_ && pause_->state);
  }
}

void OpenQuattWeather::update() {
  if (!storage_) return;
  ota_paused_ = ota_pause_.active(monotonic_ms(), pause_->state);
  if (!allowed_()) {
    if (active_) invalidate_();
    request_pending_save_();
    publish_();
    return;
  }
  if (!active_) {
    active_ = true;
    next_fetch_ms_ = monotonic_ms();
    backoff_ms_ = 60000;
  }
  const auto& location = storage_[0].location;
  if (location.kind != oq_weather::LocationKind::EMPTY && clock_->now().is_valid() && network::is_connected() &&
      monotonic_ms() >= next_fetch_ms_)
    request_(location_resolved_ ? Job::FETCH : Job::RESOLVE_LOCATION);
  publish_();
}

void OpenQuattWeather::request_pending_save_() {
  if (consent_state_ == ConsentState::PENDING)
    request_(Job::SAVE_CONSENT);
  else if (pending_save_)
    request_(Job::SAVE_LOCATION);
}

void OpenQuattWeather::request_(Job job) {
  const bool saving = job == Job::SAVE_CONSENT || job == Job::SAVE_LOCATION;
  if (saving ? ota_paused_ || (pause_ && pause_->state) : !allowed_()) return;
  if (phase_.load(std::memory_order_acquire) != oq_weather::MailboxPhase::IDLE) return;
  if (!saving && !https_slot.try_acquire_weather()) return;  // Keep the request due for the next poll.
  auto& state = storage_[0];
  if (!worker_ && !worker_attempted_) {
    worker_attempted_ = true;
    if (xTaskCreate(worker_entry_, "oq_weather", WEATHER_TASK_STACK_SIZE, this, tskIDLE_PRIORITY + 1, &worker_) !=
        pdPASS)
      worker_ = nullptr;
  }
  if (!worker_) {
    if (!saving) https_slot.release();
    if (job == Job::SAVE_CONSENT) {
      consent_state_ = ConsentState::FAILED;
      publish_text_(status_, CONSENT_FAILED);
    } else {
      pending_save_ = false;
      configuration_error_ = true;
      last_error_ = "worker_allocation_failed";
      if (job == Job::SAVE_LOCATION)
        publish_location_(state.location.query, state.pending_save_request, SaveState::FAILED);
    }
    return;
  }
  state.job = job;
  state.request_location = job == Job::SAVE_LOCATION ? state.pending_location : state.location;
  state.request_epoch = clock_->now().is_valid() ? clock_->now().timestamp : 0;
  state.request_ms = monotonic_ms();
  state.request_generation = generation_;
  state.request_configuration = job == Job::SAVE_CONSENT ? consent_revision_ : configuration_;
  state.request_consent = consent_target_;
  if (!saving) ++request_count_;
  cancel_.store(false, std::memory_order_release);
  work_gate_.store(oq_weather::WorkGate::QUEUED, std::memory_order_release);
  phase_.store(oq_weather::MailboxPhase::REQUESTED, std::memory_order_release);
  xTaskNotifyGive(worker_);
}

void OpenQuattWeather::worker_entry_(void* argument) {
  auto* self = static_cast<OpenQuattWeather*>(argument);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (self->phase_.load(std::memory_order_acquire) != oq_weather::MailboxPhase::REQUESTED) continue;
    auto& state = self->storage_[0];
    const uint64_t started = monotonic_ms();
    if (state.job == Job::SAVE_CONSENT || state.job == Job::SAVE_LOCATION) {
      // A queued flash job can be cancelled by OTA or a newer action.
      // After the CAS, finish the transaction and verify its durable bytes.
      if (!oq_weather::begin_flash_work(self->work_gate_)) {
        state.error = "save_deferred";
        state.result_ok = false;
      } else {
        state.result_ok = state.job == Job::SAVE_CONSENT ? self->save_consent_() : self->save_location_();
      }
    } else {
      state.result_ok = self->fetch_();
    }
    state.duration_ms = static_cast<uint32_t>(monotonic_ms() - started);
    state.stack_free = uxTaskGetStackHighWaterMark(nullptr);
    self->phase_.store(oq_weather::MailboxPhase::READY, std::memory_order_release);
  }
}

bool OpenQuattWeather::save_consent_() {
  auto& state = storage_[0];
  state.error = CONSENT_FAILED;
  nvs_handle_t handle;
  if (nvs_open("oq_weather", NVS_READWRITE, &handle) != ESP_OK) return false;
  const uint8_t value = state.request_consent ? 1 : 0;
  const bool saved = nvs_set_u8(handle, "enabled", value) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  uint8_t verified = 0;
  const bool ok = saved && read_consent(verified) == ESP_OK && verified == value;
  if (ok) state.error = "none";
  return ok;
}

bool OpenQuattWeather::save_location_() {
  auto& state = storage_[0];
  state.error = "location_persistence_failed";
  nvs_handle_t handle;
  if (nvs_open("oq_weather", NVS_READWRITE, &handle) != ESP_OK) return false;
  const auto record = oq_weather::location_record(state.request_location);
  const bool ok = oq_weather::persist_location_verified(
      record,
      [handle](const oq_weather::LocationRecord& value) {
        return nvs_set_blob(handle, "location", &value, sizeof(value)) == ESP_OK;
      },
      [handle]() { return nvs_commit(handle) == ESP_OK; },
      [](oq_weather::LocationRecord& value) {
        nvs_handle_t verify;
        if (nvs_open("oq_weather", NVS_READONLY, &verify) != ESP_OK) return false;
        size_t size = sizeof(value);
        const esp_err_t result = nvs_get_blob(verify, "location", &value, &size);
        nvs_close(verify);
        return result == ESP_OK && size == sizeof(value);
      });
  nvs_close(handle);
  if (ok) state.error = "none";
  return ok;
}

void OpenQuattWeather::loop() {
  if (!storage_) return;
  const bool was_paused = ota_paused_;
  ota_paused_ = ota_pause_.active(monotonic_ms(), pause_->state);
  if (ota_paused_ && !was_paused) invalidate_();
  if (phase_.load(std::memory_order_acquire) != oq_weather::MailboxPhase::READY) {
    request_pending_save_();
    return;
  }
  auto& state = storage_[0];
  last_duration_ms_ = state.duration_ms;
  if (state.internal_largest_low && (!worker_largest_low_ || state.internal_largest_low < worker_largest_low_))
    worker_largest_low_ = state.internal_largest_low;
  if (worker_stack_min_ == 0 || state.stack_free < worker_stack_min_) worker_stack_min_ = state.stack_free;
  if (state.job == Job::SAVE_CONSENT) {
    if (std::strcmp(state.error, "save_deferred") != 0) {
      if (state.request_configuration == consent_revision_) {
        if (state.result_ok)
          consent_state_ = state.request_consent ? ConsentState::ON : ConsentState::OFF;
        else
          consent_state_ = ConsentState::FAILED;
      }
      // A superseded but verified write changes the durable switch value,
      // while proof for the newest request remains pending.
      if (state.result_ok) enabled_->publish_state(state.request_consent);
    }
  } else if (state.job == Job::SAVE_LOCATION) {
    if (state.request_configuration == configuration_) {
      if (std::strcmp(state.error, "save_deferred") == 0) {
        phase_.store(oq_weather::MailboxPhase::IDLE, std::memory_order_release);
        return;  // Keep the draft pending until the OTA/service hold ends.
      }
      pending_save_ = false;
      configuration_error_ = !state.result_ok;
      last_error_ = state.error;
      if (state.result_ok) {
        state.location = state.request_location;
        location_resolved_ = state.location.kind == oq_weather::LocationKind::COORDINATES;
        publish_location_(state.location.query, state.pending_save_request, SaveState::STORED);
        next_fetch_ms_ = monotonic_ms();
      } else
        publish_location_(state.location.query, state.pending_save_request, SaveState::FAILED);
    }
  } else if (allowed_() && state.request_generation == generation_ && state.request_configuration == configuration_) {
    const auto clock = clock_->now();
    bool accepted = state.result_ok && clock.is_valid();
    if (state.job == Job::FETCH) accepted &= oq_weather::usable(state.candidate, clock.timestamp, monotonic_ms());
    if (accepted) {
      last_error_ = "none";
      backoff_ms_ = 60000;
      if (state.job == Job::RESOLVE_LOCATION) {
        state.location.latitude = state.result_location.latitude;
        state.location.longitude = state.result_location.longitude;
        location_resolved_ = true;
        if (resolved_) {
          char label[128];
          std::snprintf(label, sizeof(label), "%s (%.5f, %.5f)", state.resolved_name, state.location.latitude,
                        state.location.longitude);
          publish_text_(resolved_, label);
        }
        next_fetch_ms_ = monotonic_ms();
      } else {
        state.current = state.candidate;
        if (++accepted_serial_ == 0) ++accepted_serial_;
        state.current.generation = accepted_serial_;
        next_fetch_ms_ = monotonic_ms() + 900000;
      }
    } else {
      ++failure_count_;
      last_error_ = state.result_ok ? "clock_changed" : state.error;
      if (state.job == Job::RESOLVE_LOCATION &&
          (std::strcmp(last_error_, "location_not_found") == 0 || std::strcmp(last_error_, "location_ambiguous") == 0))
        configuration_error_ = true;
      next_fetch_ms_ = monotonic_ms() + backoff_ms_;
      backoff_ms_ = std::min<uint32_t>(backoff_ms_ * 2U, 3600000U);
      ESP_LOGW(TAG, "Weather job rejected: %s; retaining previous series", last_error_);
    }
    ESP_LOGI(TAG, "Job: ok=%d duration=%u ms internal=%u/min=%u/largest=%u B worker stack=%u B free", accepted,
             static_cast<unsigned>(state.duration_ms),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(state.stack_free));
  } else if (state.job != Job::SAVE_LOCATION) {
    ++cancelled_requests_;
  }
  const bool finished_https = state.job == Job::RESOLVE_LOCATION || state.job == Job::FETCH;
  phase_.store(oq_weather::MailboxPhase::IDLE, std::memory_order_release);
  request_pending_save_();
  publish_();
  if (finished_https) https_slot.release();
}

void OpenQuattWeather::observe(const oq_power_house::forecast::Observation& observation) {
  if (!storage_ || !allowed_()) return;
  auto& state = storage_[0];
  auto checked = observation;
  const uint64_t now_ms = monotonic_ms();
  if (checked.monotonic_ms > now_ms || now_ms - checked.monotonic_ms > 15000) checked.valid = false;
  oq_power_house::forecast::observe(state.forecast, checked);
  const bool model_ready = oq_power_house::forecast::valid_model(checked);
  if (!model_ready) {
    // Revoke the displayed issue without erasing earlier diagnostic tickets.
    state.forecast.latest_issue_epoch = 0;
    state.forecast.last_issue_ms = 0;
    for (auto& value : state.forecast.latest) value = NAN;
  }
  if (!state.forecast.latest_issue_epoch) state.issued_scenario = {};
  if (model_ready && oq_weather::usable(state.current, checked.epoch, now_ms)) {
    float forcing[oq_power_house::forecast::kForcingSteps + 1];
    bool complete = true;
    for (size_t i = 0; i <= oq_power_house::forecast::kForcingSteps; ++i)
      complete &= oq_weather::temperature_at(state.current, checked.epoch + i * 900U, forcing[i]);
    if (complete && oq_power_house::forecast::issue(state.forecast, forcing, state.current.generation)) {
      state.issued_scenario = {};
      for (const auto& ticket : state.forecast.pending) {
        if (!ticket.active || ticket.epoch != state.forecast.latest_issue_epoch ||
            ticket.monotonic_ms != state.forecast.last_issue_ms)
          continue;
        auto& issued = state.issued_scenario;
        issued.monotonic_ms = ticket.monotonic_ms;
        issued.epoch = ticket.epoch;
        issued.configuration = configuration_;
        issued.weather_generation = ticket.weather_generation;
        issued.weather_fetched_epoch = state.current.fetched_epoch;
        issued.weather_start_epoch = state.current.time[0];
        issued.model_context = ticket.context;
        issued.model_samples = ticket.model_samples;
        // actual_inputs still equals the initial room temperature immediately
        // after issue(); later scoring may mutate it, never this issue copy.
        issued.room_start_c = static_cast<float>(ticket.actual_inputs);
        issued.held_heat_w = static_cast<float>(ticket.held_heat);
        issued.h_w_per_k = static_cast<float>(ticket.h);
        issued.c_wh_per_k = static_cast<float>(ticket.c);
        for (size_t i = 0; i < 3; ++i) issued.temperature_c[i] = static_cast<float>(ticket.issued[i]);
        break;
      }
      ESP_LOGI(TAG, "Issued epoch=%u weather=%u model_samples=%u H=%.2f C=%.2f held_heat=%.1f W room=%.2f/%.2f/%.2f C",
               static_cast<unsigned>(checked.epoch), static_cast<unsigned>(state.current.generation),
               static_cast<unsigned>(checked.model_samples), checked.h_w_per_k, checked.c_wh_per_k, checked.heat_w,
               state.forecast.latest[0], state.forecast.latest[1], state.forecast.latest[2]);
    }
  }
  publish_();
}

void OpenQuattWeather::publish_() {
  auto& state = storage_[0];
  const auto clock = clock_->now();
  const uint64_t now_ms = monotonic_ms();
  const bool usable = allowed_() && clock.is_valid() && oq_weather::usable(state.current, clock.timestamp, now_ms);
  publish_forecast_();
  const char* status = consent_state_ == ConsentState::PENDING                  ? CONSENT_PENDING
                       : consent_state_ == ConsentState::FAILED                 ? CONSENT_FAILED
                       : pending_save_                                          ? "location_saving"
                       : configuration_error_                                   ? last_error_
                       : !enabled_->state                                       ? "disabled"
                       : !allowed_()                                            ? "paused"
                       : state.location.kind == oq_weather::LocationKind::EMPTY ? "location_required"
                       : !clock.is_valid()                                      ? "waiting_for_clock"
                       : std::strcmp(last_error_, "none") != 0                  ? last_error_
                       : !location_resolved_                                    ? "resolving_location"
                       : !usable                                                ? "waiting_for_weather"
                                                                                : state.forecast.status;
  if (status_ && status_->state != status) publish_text_(status_, status);
  if (resolved_ && location_resolved_ && state.location.kind == oq_weather::LocationKind::COORDINATES &&
      resolved_->state != state.location.query)
    publish_text_(resolved_, state.location.query);
  if (age_)
    age_->publish_state(state.current.generation && now_ms >= state.current.fetched_ms
                            ? (now_ms - state.current.fetched_ms) / 1000.0f
                            : NAN);
  if (requests_) requests_->publish_state(request_count_);
  if (failures_) failures_->publish_state(failure_count_);
  if (stack_free_) stack_free_->publish_state(worker_stack_min_ ? worker_stack_min_ : NAN);
  if (duration_) duration_->publish_state(last_duration_ms_);
  if (allowed_() && millis() - last_report_ms_ >= 900000) {
    last_report_ms_ = millis();
    for (size_t i = 0; i < 3; ++i) {
      const auto& e = state.forecast.errors[i];
      ESP_LOGI(TAG, "Horizon=%u s n=%u MAE=%.4f persistence=%.4f model=%.4f weather=%.4f heat=%.4f K",
               static_cast<unsigned>(oq_power_house::forecast::kHorizonS[i]), static_cast<unsigned>(e.issued.count),
               e.issued.mae(), e.persistence.mae(), e.actual_inputs.mae(), e.weather_contribution.mae(),
               e.heat_assumption.mae());
    }
  }
}

void OpenQuattWeather::publish_forecast_() {
  if (!storage_ || !clock_) return;
  const auto& state = storage_[0];
  const auto now = clock_->now();
  const uint64_t now_ms = monotonic_ms();
  const bool valid = allowed_() && now.is_valid() && oq_weather::usable(state.current, now.timestamp, now_ms);
  ScenarioSnapshot scenario;
  scenario.issued = state.issued_scenario;
  scenario.observation_ms = state.forecast.previous.monotonic_ms;
  scenario.observation_epoch = state.forecast.previous.epoch;
  scenario.reason =
      !enabled_ || !enabled_->state ? ScenarioReason::DISABLED
      : pending_save_               ? ScenarioReason::LOCATION_SAVING
      : configuration_error_ || std::strcmp(last_error_, "invalid_location") == 0 ||
              std::strcmp(last_error_, "location_persistence_failed") == 0
          ? ScenarioReason::LOCATION_INVALID
      : !allowed_()                                                           ? ScenarioReason::PAUSED
      : !now.is_valid() || std::strcmp(last_error_, "clock_changed") == 0     ? ScenarioReason::CLOCK_INVALID
      : !valid                                                                ? ScenarioReason::WEATHER_UNAVAILABLE
      : !oq_power_house::forecast::valid_observation(state.forecast.previous) ? ScenarioReason::OBSERVATION_INVALID
      : !oq_power_house::forecast::valid_model(state.forecast.previous)       ? ScenarioReason::MODEL_UNAVAILABLE
      : !scenario.issued.epoch || scenario.issued.epoch != state.forecast.latest_issue_epoch ? ScenarioReason::NO_ISSUE
      : scenario.issued.configuration != configuration_                  ? ScenarioReason::CONFIGURATION_CHANGED
      : scenario.issued.model_context != state.forecast.previous.context ? ScenarioReason::CONTEXT_CHANGED
                                                                         : ScenarioReason::READY;
  scenario.valid = scenario.reason == ScenarioReason::READY;
  scenario.horizon_valid_mask = scenario.valid ? 7U : 0U;
  age_scenario_snapshot(scenario, now.is_valid() ? now.timestamp : 0U, now_ms, valid, configuration_);
  forecast_snapshots_.publish([&](ForecastSnapshot& snapshot) {
    snapshot.published_ms = now_ms;
    snapshot.fetched_ms = state.current.fetched_ms;
    snapshot.now_epoch = now.is_valid() ? now.timestamp : 0;
    snapshot.fetched_epoch = state.current.fetched_epoch;
    snapshot.generation = state.current.generation;
    snapshot.configuration = configuration_;
    snapshot.start_epoch = state.current.time[0];
    snapshot.enabled = enabled_ && enabled_->state;
    snapshot.valid = valid;
    snapshot.current = state.current.current;
    snapshot.current_wind = state.current.current_wind;
    snapshot.current_wind.valid = valid && oq_weather::valid_current_wind(snapshot.current_wind, snapshot.now_epoch);
    snapshot.current_wind.direction_valid &=
        snapshot.current_wind.valid && oq_weather::valid_wind_direction(snapshot.current_wind.direction_deg);
    snapshot.latitude = state.current.latitude;
    snapshot.longitude = state.current.longitude;
    snapshot.wind_direction_valid_mask = state.current.wind_direction_valid_mask;
    snapshot.scenario = scenario;
    snapshot.current.valid = valid && oq_weather::valid_current(snapshot.current, snapshot.now_epoch);
    std::memcpy(snapshot.temperature_c, state.current.temperature, sizeof(snapshot.temperature_c));
    std::memcpy(snapshot.wind_mps, state.current.wind_mps, sizeof(snapshot.wind_mps));
    std::memcpy(snapshot.wind_direction_deg, state.current.wind_direction_deg, sizeof(snapshot.wind_direction_deg));
    // These main-loop-owned strings are copied only by their owner. HTTP sees
    // bounded immutable bytes, never EntityBase flags or live std::strings.
    const char* label = resolved_ && !resolved_->state.empty() ? resolved_->state.c_str() : state.location.query;
    std::strncpy(snapshot.location, label, sizeof(snapshot.location) - 1);
    snapshot.location[sizeof(snapshot.location) - 1] = '\0';
    std::memcpy(snapshot.configured_location, state.location.query, sizeof(snapshot.configured_location));
  });
  // REST and the loose entities share the same issue and validity decision.
  if (issued_epoch_) {
    char issued[16]{};
    if (scenario.valid) std::snprintf(issued, sizeof(issued), "%u", static_cast<unsigned>(scenario.issued.epoch));
    if (issued_epoch_->state != issued) publish_text_(issued_epoch_, issued);
  }
  for (size_t i = 0; i < 3; ++i)
    if (temperatures_[i])
      temperatures_[i]->publish_state(
          scenario.valid && (scenario.horizon_valid_mask & (1U << i)) ? scenario.issued.temperature_c[i] : NAN);
}

void OpenQuattWeather::dump_config() {
  ESP_LOGCONFIG(TAG, "Experimental passive forecast; no control outputs");
  ESP_LOGCONFIG(TAG, "  Model: %s, hourly temperatures: %u", oq_weather::kModel,
                static_cast<unsigned>(oq_weather::kHours));
  ESP_LOGCONFIG(TAG, "  Strict PSRAM: %u B; internal worker stack: %u B (created on first job)",
                static_cast<unsigned>(sizeof(Storage)), static_cast<unsigned>(WEATHER_TASK_STACK_SIZE));
}

}  // namespace esphome::openquatt_weather
