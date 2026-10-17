#include "OpenQuattRestartHandoff.h"

#include <cstring>

#include "OpenQuattIncidentManager.h"
#include "OpenQuattRestartHandoffPolicy.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::openquatt_incident_manager {
namespace {

using restart_handoff::BootContext;
using restart_handoff::Record;

constexpr const char* TAG = "openquatt.restart";
constexpr const char* NVS_NAMESPACE = "openquatt";
constexpr const char* NVS_KEY = "oq_restart_v1";

bool storage_ready{false};
RTC_NOINIT_ATTR volatile uint32_t consume_incomplete;
restart_handoff::ResumeState restored_resume{};
PowerHouseResumeReader power_house_reader{nullptr};
OtaResumeGuardReader ota_guard_reader{nullptr};
OpenQuattOtaHandoff* ota_handoff{nullptr};
uint32_t restored_credit_ms[2]{0U, 0U};
uint32_t configured_minimum_off_ms{0U};

uint32_t current_configuration_hash(uint32_t minimum_off_ms) {
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
  constexpr bool controller_q = true;
#else
  constexpr bool controller_q = false;
#endif
#if OQ_TOPOLOGY_DUO
  return restart_handoff::configuration_hash(minimum_off_ms, true, controller_q);
#else
  return restart_handoff::configuration_hash(minimum_off_ms, false, controller_q);
#endif
}

bool valid_handoff_credit(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms) {
  return storage_ready && restart_handoff::valid_minimum_off_ms(configured_minimum_off_ms) &&
         hp1_credit_ms <= configured_minimum_off_ms && hp2_credit_ms <= configured_minimum_off_ms;
}

bool open_storage(nvs_handle_t* handle) {
  if (handle == nullptr) return false;
  const esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, handle);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "Could not open restart handoff storage: %s", esp_err_to_name(result));
    return false;
  }
  return true;
}

class NvsStorage {
 public:
  explicit NvsStorage(nvs_handle_t handle) : handle_(handle) {}

  restart_handoff::StorageReadResult read(Record* record) const {
    if (record == nullptr) return restart_handoff::StorageReadResult::ERROR;
    size_t size = sizeof(*record);
    const esp_err_t result = nvs_get_blob(this->handle_, NVS_KEY, record, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) return restart_handoff::StorageReadResult::ABSENT;
    if (result != ESP_OK || size != sizeof(*record)) {
      ESP_LOGW(TAG, "Restart handoff record is unreadable (result=%s, size=%u)", esp_err_to_name(result),
               static_cast<unsigned>(size));
      return restart_handoff::StorageReadResult::ERROR;
    }
    return restart_handoff::StorageReadResult::PRESENT;
  }

  bool erase_and_verify_absent() const {
    const esp_err_t erase_result = nvs_erase_key(this->handle_, NVS_KEY);
    if (erase_result != ESP_OK && erase_result != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGE(TAG, "Could not erase restart handoff: %s", esp_err_to_name(erase_result));
      return false;
    }
    if (erase_result == ESP_OK && nvs_commit(this->handle_) != ESP_OK) {
      ESP_LOGE(TAG, "Could not commit restart handoff consume");
      return false;
    }
    Record verified{};
    return this->read(&verified) == restart_handoff::StorageReadResult::ABSENT;
  }

  bool write_commit_and_verify(const Record& record) const {
    if (nvs_set_blob(this->handle_, NVS_KEY, &record, sizeof(record)) != ESP_OK) return false;
    if (nvs_commit(this->handle_) != ESP_OK) return false;
    Record verified{};
    return this->read(&verified) == restart_handoff::StorageReadResult::PRESENT &&
           std::memcmp(&record, &verified, sizeof(record)) == 0;
  }

 private:
  nvs_handle_t handle_;
};

bool capture_boot_context(uint32_t minimum_off_ms, BootContext* context) {
  if (context == nullptr || !restart_handoff::valid_minimum_off_ms(minimum_off_ms)) return false;
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_app_desc_t* description = esp_app_get_description();
  if (running == nullptr || boot == nullptr || description == nullptr) return false;

  esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_result = esp_ota_get_state_partition(running, &image_state);
  const bool image_valid =
      (state_result == ESP_OK && (image_state == ESP_OTA_IMG_VALID || image_state == ESP_OTA_IMG_UNDEFINED)) ||
      state_result == ESP_ERR_NOT_FOUND;
  // Older deployed bootloaders can leave the first OTA boot in NEW instead of
  // transitioning it to PENDING_VERIFY. Exact partition+image matching still
  // makes that a safe unconfirmed state for this one-shot handoff.
  const bool image_pending_verify =
      state_result == ESP_OK && (image_state == ESP_OTA_IMG_PENDING_VERIFY || image_state == ESP_OTA_IMG_NEW);
  if (!image_valid && !image_pending_verify) {
    ESP_LOGW(TAG, "Restart handoff rejected unavailable image state: %s", esp_err_to_name(state_result));
    return false;
  }

  context->software_reset = esp_reset_reason() == ESP_RST_SW;
  context->running_partition_matches_boot = running->address == boot->address;
  context->image_valid = image_valid;
  context->image_confirmed = state_result == ESP_OK && image_state == ESP_OTA_IMG_VALID;
  context->image_pending_verify = image_pending_verify;
  context->minimum_off_ms = minimum_off_ms;
  context->config_hash = current_configuration_hash(minimum_off_ms);
  context->boot_partition_address = running->address;
  std::memcpy(context->image_hash, description->app_elf_sha256, sizeof(context->image_hash));
  return restart_handoff::has_image_hash(context->image_hash);
}

bool confirm_pending_image_for_controlled_restart() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  if (running == nullptr || boot == nullptr || running->address != boot->address) return false;

  esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_result = esp_ota_get_state_partition(running, &image_state);
  if (state_result != ESP_OK || image_state != ESP_OTA_IMG_PENDING_VERIFY) return true;

  // SafeMode confirms this same image in App.safe_reboot(). Do it before the
  // handoff snapshot so the first controlled restart after OTA can be armed.
  const esp_err_t confirm_result = esp_ota_mark_app_valid_cancel_rollback();
  if (confirm_result != ESP_OK) {
    ESP_LOGE(TAG, "Could not confirm pending image for controlled restart: %s", esp_err_to_name(confirm_result));
    return false;
  }
  return true;
}

// A failed journal invalidation must not select a previously armed OTA target.
// This is independent of NVS and verified against the OTA partition selection.
bool select_running_after_storage_failure() {
  storage_ready = false;
  const auto* running = esp_ota_get_running_partition();
  if (running == nullptr || esp_ota_set_boot_partition(running) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to cancel OTA boot after handoff storage failure");
    return false;
  }
  const auto* boot = esp_ota_get_boot_partition();
  const bool selected = boot != nullptr && boot->address == running->address;
  if (!selected) ESP_LOGE(TAG, "OTA boot cancellation could not be verified");
  return selected;
}

bool discard_ota_handoff() {
  nvs_handle_t handle{};
  if (!open_storage(&handle)) {
    select_running_after_storage_failure();
    return false;
  }
  NvsStorage storage(handle);
  const bool erased = storage.erase_and_verify_absent();
  nvs_close(handle);
  if (!erased) select_running_after_storage_failure();
  return erased;
}

bool persist_handoff_record(const Record& record) {
  nvs_handle_t handle{};
  if (!open_storage(&handle)) return false;
  NvsStorage storage(handle);
  const bool persisted = restart_handoff::persist_record(storage, record);
  nvs_close(handle);
  return persisted;
}

bool persist_target_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms, uint32_t boot_partition_address,
                            const uint8_t* image_hash, uint8_t resume_mask,
                            const restart_handoff::PowerHouseResumeState& power_house, bool completed_ota) {
  if (!valid_handoff_credit(hp1_credit_ms, hp2_credit_ms) || image_hash == nullptr) return false;

  Record record{};
  record.magic = restart_handoff::kRecordMagic;
  record.version = restart_handoff::kRecordVersion;
  record.state = completed_ota ? restart_handoff::kRecordCompletedOta : restart_handoff::kRecordArmed;
  record.resume_mask = resume_mask;
  record.minimum_off_ms = configured_minimum_off_ms;
  record.config_hash = current_configuration_hash(configured_minimum_off_ms);
  record.boot_partition_address = boot_partition_address;
  record.hp1_credit_ms = hp1_credit_ms;
  record.hp2_credit_ms = hp2_credit_ms;
  std::memcpy(record.image_hash, image_hash, sizeof(record.image_hash));
  if (restart_handoff::valid_power_house_resume(power_house)) record.power_house = power_house;
  restart_handoff::finalize_record(&record);
  return persist_handoff_record(record);
}

}  // namespace

bool initialize_restart_handoff(uint32_t minimum_off_ms) {
  storage_ready = false;
  restored_resume = {};
  restored_credit_ms[0] = 0U;
  restored_credit_ms[1] = 0U;
  configured_minimum_off_ms = 0U;

  const bool previous_consume_complete = restart_handoff::begin_boot_consume(consume_incomplete);
  __sync_synchronize();
  BootContext context{};
  const bool context_available = capture_boot_context(minimum_off_ms, &context);
  if (!previous_consume_complete) context.software_reset = false;
  // Retry storage within this boot, retaining its original reset reason.
  // A software reboot here could turn a cold/crash record into a planned one.
  bool consumed = false;
  for (unsigned attempt = 0; attempt < 3U && !consumed; ++attempt) {
    nvs_handle_t handle{};
    if (!open_storage(&handle)) continue;
    NvsStorage storage(handle);
    consumed = restart_handoff::consume_record(storage, context, &restored_credit_ms[0], &restored_credit_ms[1],
                                               &restored_resume);
    nvs_close(handle);
  }
  if (!consumed) {
    ESP_LOGE(TAG, "Restart handoff could not be durably consumed; control remains inhibited");
    return false;
  }

  restart_handoff::finish_boot_consume(consume_incomplete);
  __sync_synchronize();
  storage_ready = true;
  configured_minimum_off_ms = minimum_off_ms;
  if (context_available && (restored_credit_ms[0] != 0U || restored_credit_ms[1] != 0U)) {
    ESP_LOGI(TAG, "Restored one-shot confirmed-off credit HP1=%us HP2=%us",
             static_cast<unsigned>(restored_credit_ms[0] / 1000U),
             static_cast<unsigned>(restored_credit_ms[1] / 1000U));
  }
  return true;
}

bool restart_handoff_storage_ready() { return storage_ready; }

bool boot_resume_eligible(uint8_t hp_index) {
  return storage_ready &&
         (hp_index == 0U ? restored_resume.hp_mask != 0U
                         : hp_index <= 2U && (restored_resume.hp_mask & (1U << (hp_index - 1U))) != 0U);
}

bool boot_resume_image_confirmed() {
  BootContext context{};
  return capture_boot_context(configured_minimum_off_ms, &context) &&
         restart_handoff::may_adopt_running_image(restored_resume, context);
}

void set_ota_resume_guard_reader(OtaResumeGuardReader reader) { ota_guard_reader = reader; }

uint8_t current_running_resume_mask() { return ota_guard_reader == nullptr ? 0U : ota_guard_reader() & 3U; }

bool ota_cycle_hold(uint8_t hp_index, uint32_t now_ms) {
  return ota_handoff != nullptr && ota_handoff->cycle_hold(hp_index, now_ms);
}

void set_power_house_resume_reader(PowerHouseResumeReader reader) { power_house_reader = reader; }

bool take_power_house_resume(restart_handoff::PowerHouseResumeState* state) {
  const auto saved = restored_resume.power_house;
  restored_resume.power_house = {};
  if (state == nullptr || !boot_resume_eligible() || !restart_handoff::valid_power_house_resume(saved)) return false;
  *state = saved;
  return true;
}

uint32_t restored_off_credit_ms(uint8_t hp_index) {
  if (!storage_ready || hp_index < 1U || hp_index > 2U) return 0U;
  return restored_credit_ms[hp_index - 1U];
}

bool arm_restart_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms, uint8_t resume_mask) {
  if (!valid_handoff_credit(hp1_credit_ms, hp2_credit_ms)) return false;
  if (!confirm_pending_image_for_controlled_restart()) return false;

  BootContext context{};
  if (!capture_boot_context(configured_minimum_off_ms, &context) || !context.running_partition_matches_boot ||
      !context.image_valid) {
    ESP_LOGE(TAG, "Restart handoff arm rejected current image context");
    return false;
  }

#if !OQ_TOPOLOGY_DUO
  resume_mask &= 1U;
#endif
  const auto power_house =
      power_house_reader == nullptr ? restart_handoff::PowerHouseResumeState{} : power_house_reader();
  const bool persisted = persist_target_handoff(hp1_credit_ms, hp2_credit_ms, context.boot_partition_address,
                                                context.image_hash, resume_mask, power_house, false);
  if (!persisted) ESP_LOGE(TAG, "Restart handoff arm was not durably verified");
  return persisted;
}

bool arm_completed_ota_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms, uint8_t resume_mask,
                               const restart_handoff::PowerHouseResumeState& power_house) {
  if (!valid_handoff_credit(hp1_credit_ms, hp2_credit_ms)) return false;

  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* target = esp_ota_get_boot_partition();
  if (running == nullptr || target == nullptr || running->address == target->address) {
    ESP_LOGE(TAG, "Completed OTA handoff has no distinct selected boot partition");
    return false;
  }

  esp_app_desc_t target_description{};
  const esp_err_t description_result = esp_ota_get_partition_description(target, &target_description);
  if (description_result != ESP_OK) {
    ESP_LOGE(TAG, "Completed OTA handoff could not read target image: %s", esp_err_to_name(description_result));
    return false;
  }

  const bool persisted = persist_target_handoff(hp1_credit_ms, hp2_credit_ms, target->address,
                                                target_description.app_elf_sha256, resume_mask, power_house, true);
  if (!persisted) {
    ESP_LOGE(TAG, "Completed OTA handoff was not durably verified");
    return false;
  }

  ESP_LOGI(TAG, "Controlled OTA handoff armed for boot partition 0x%08X", static_cast<unsigned>(target->address));
  return true;
}

void OpenQuattOtaHandoff::setup() {
  ota_handoff = this;
#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
}

void OpenQuattOtaHandoff::loop() {
  if (!this->ota_active_) return;
  // Native/download OTA blocks the loop. Background web uploads do not: keep
  // checking permission, live telemetry and the existing incident guards.
  this->cycle_hold(1U, millis());
  for (uint8_t hp = 1U; hp <= 2U; ++hp) {
    if (this->full_credit_if_confirmed_(hp, millis()) == 0U) {
      (hp == 1U ? this->ota_hp1_credit_ms_ : this->ota_hp2_credit_ms_) = 0U;
    }
  }
}

bool OpenQuattOtaHandoff::cycle_hold(uint8_t hp_index, uint32_t now_ms) {
  return this->ota_resume_.holding(hp_index, now_ms, ota_guard_reader == nullptr ? 0U : ota_guard_reader());
}

uint32_t OpenQuattOtaHandoff::full_credit_if_confirmed_(uint8_t hp_index, uint32_t now_ms) const {
  if (this->incident_manager_ == nullptr || this->incident_manager_->is_failed() ||
      !this->incident_manager_->storage_ready() || !this->incident_manager_->hp_configured(hp_index)) {
    return 0U;
  }

  const auto outputs = this->incident_manager_->get_outputs(hp_index);
  if (outputs.link_state != oq_incidents::LinkState::HEALTHY || outputs.run_state != oq_incidents::RunState::STOPPED ||
      !outputs.stop_confirmed || outputs.stop_confirmation_pending ||
      this->incident_manager_->minimum_off_remaining_ms(hp_index, now_ms) != 0U) {
    return 0U;
  }
  return this->minimum_off_ms_;
}

void OpenQuattOtaHandoff::clear_ota_snapshot_() {
  this->ota_hp1_credit_ms_ = 0U;
  this->ota_hp2_credit_ms_ = 0U;
  this->ota_handoff_attempted_ = false;
  this->ota_active_ = false;
  this->ota_resume_.reset();
  this->ota_power_house_ = {};
}

#ifdef USE_OTA_STATE_LISTENER
void OpenQuattOtaHandoff::on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                                              ota::OTAComponent* component) {
  (void)progress;
  (void)error;

  if (state == ota::OTA_STARTED) {
    if (!discard_ota_handoff()) {
      this->ota_resume_.cancel();
      return;
    }
    if (this->ota_active_) {
      this->ota_resume_.cancel();
      this->ota_hp1_credit_ms_ = this->ota_hp2_credit_ms_ = 0U;
      this->ota_power_house_ = {};
      return;
    }
    this->clear_ota_snapshot_();
    const uint32_t now_ms = millis();
    this->ota_active_ = true;
    this->ota_resume_.begin(reinterpret_cast<uintptr_t>(component), now_ms,
                            ota_guard_reader == nullptr ? 0U : ota_guard_reader());
    this->ota_power_house_ =
        power_house_reader == nullptr ? restart_handoff::PowerHouseResumeState{} : power_house_reader();
    this->ota_hp1_credit_ms_ = this->full_credit_if_confirmed_(1U, now_ms);
    this->ota_hp2_credit_ms_ = this->full_credit_if_confirmed_(2U, now_ms);
    this->ota_handoff_attempted_ = this->ota_hp1_credit_ms_ != 0U || this->ota_hp2_credit_ms_ != 0U;
    ESP_LOGI(TAG, "Controlled OTA: captured full off-time credit HP1=%us HP2=%us%s",
             static_cast<unsigned>(this->ota_hp1_credit_ms_ / 1000U),
             static_cast<unsigned>(this->ota_hp2_credit_ms_ / 1000U), this->ota_handoff_attempted_ ? "" : " (none)");
    return;
  }

  if (state == ota::OTA_ABORT || state == ota::OTA_ERROR) {
    if (!this->ota_active_ || !this->ota_resume_.owns(reinterpret_cast<uintptr_t>(component))) return;
    discard_ota_handoff();
    if (this->ota_handoff_attempted_) {
      ESP_LOGI(TAG, "Controlled OTA failed; discarded captured off-time credit");
    }
    this->clear_ota_snapshot_();
    return;
  }

  if (state == ota::OTA_COMPLETED) {
    if (!storage_ready) {
      select_running_after_storage_failure();
      return;
    }
    if (!this->ota_active_) return;
    // Deferred web completion can precede the next main-loop guard check.
    // Native/download OTA blocks the loop and retains its short-transfer bound.
    if (this->background_ota_ != nullptr && component == this->background_ota_) this->loop();
    const uint8_t resume_mask = this->ota_resume_.complete(reinterpret_cast<uintptr_t>(component), millis());
    const bool saved = arm_completed_ota_handoff(this->ota_hp1_credit_ms_, this->ota_hp2_credit_ms_, resume_mask,
                                                 this->ota_power_house_);
    ESP_LOGI(TAG, "Controlled OTA completed: running mask=%u (%s)", resume_mask,
             saved ? "saved" : "conservative fallback");
    this->ota_hp1_credit_ms_ = this->ota_hp2_credit_ms_ = 0U;
    this->ota_power_house_ = {};
    // Keep the write hold through the short success-to-reboot interval.
  }
}
#endif

}  // namespace esphome::openquatt_incident_manager
