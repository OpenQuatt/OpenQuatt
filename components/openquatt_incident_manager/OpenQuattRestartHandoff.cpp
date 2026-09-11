#include "OpenQuattRestartHandoff.h"

#include <cstring>

#include "OpenQuattIncidentManager.h"
#include "OpenQuattRestartHandoffPolicy.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include "esphome/core/application.h"
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
uint32_t restored_credit_ms[2]{0U, 0U};
uint32_t configured_minimum_off_ms{0U};

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
  // A normal controlled restart requires a valid image. A controlled OTA
  // handoff may additionally be consumed on the first PENDING_VERIFY boot.
  const bool image_valid =
      (state_result == ESP_OK && (image_state == ESP_OTA_IMG_VALID || image_state == ESP_OTA_IMG_UNDEFINED)) ||
      state_result == ESP_ERR_NOT_FOUND;
  const bool image_pending_verify = state_result == ESP_OK && image_state == ESP_OTA_IMG_PENDING_VERIFY;
  if (!image_valid && !image_pending_verify) {
    ESP_LOGW(TAG, "Restart handoff rejected unavailable image state: %s", esp_err_to_name(state_result));
    return false;
  }

  context->software_reset = esp_reset_reason() == ESP_RST_SW;
  context->running_partition_matches_boot = running->address == boot->address;
  context->image_valid = image_valid;
  context->image_pending_verify = image_pending_verify;
  context->minimum_off_ms = minimum_off_ms;
#if OQ_TOPOLOGY_DUO
  context->config_hash = restart_handoff::configuration_hash(minimum_off_ms, true);
#else
  context->config_hash = restart_handoff::configuration_hash(minimum_off_ms, false);
#endif
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

bool persist_handoff_record(const Record& record) {
  nvs_handle_t handle{};
  if (!open_storage(&handle)) return false;
  NvsStorage storage(handle);
  const bool persisted = restart_handoff::persist_record(storage, record);
  nvs_close(handle);
  return persisted;
}

}  // namespace

bool initialize_restart_handoff(uint32_t minimum_off_ms) {
  storage_ready = false;
  restored_credit_ms[0] = 0U;
  restored_credit_ms[1] = 0U;
  configured_minimum_off_ms = 0U;

  BootContext context{};
  const bool context_available = capture_boot_context(minimum_off_ms, &context);
  nvs_handle_t handle{};
  if (!open_storage(&handle)) return false;
  NvsStorage storage(handle);
  const bool consumed =
      restart_handoff::consume_record(storage, context, &restored_credit_ms[0], &restored_credit_ms[1]);
  nvs_close(handle);
  if (!consumed) {
    ESP_LOGE(TAG, "Restart handoff could not be durably consumed");
    return false;
  }

  storage_ready = true;
  configured_minimum_off_ms = minimum_off_ms;
  if (context_available && (restored_credit_ms[0] != 0U || restored_credit_ms[1] != 0U)) {
    ESP_LOGI(TAG, "Restored one-shot confirmed-off credit after controlled restart or OTA");
  }
  return true;
}

bool restart_handoff_storage_ready() { return storage_ready; }

uint32_t restored_off_credit_ms(uint8_t hp_index) {
  if (!storage_ready || hp_index < 1U || hp_index > 2U) return 0U;
  return restored_credit_ms[hp_index - 1U];
}

bool arm_restart_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms) {
  if (!storage_ready || !restart_handoff::valid_minimum_off_ms(configured_minimum_off_ms) ||
      hp1_credit_ms > configured_minimum_off_ms || hp2_credit_ms > configured_minimum_off_ms ||
      (hp1_credit_ms == 0U && hp2_credit_ms == 0U)) {
    return false;
  }

  if (!confirm_pending_image_for_controlled_restart()) return false;

  BootContext context{};
  if (!capture_boot_context(configured_minimum_off_ms, &context) || !context.running_partition_matches_boot ||
      !context.image_valid) {
    ESP_LOGE(TAG, "Restart handoff arm rejected current image context");
    return false;
  }

  Record record{};
  record.magic = restart_handoff::kRecordMagic;
  record.version = restart_handoff::kRecordVersion;
  record.state = restart_handoff::kRecordRestartArmed;
  record.minimum_off_ms = configured_minimum_off_ms;
  record.config_hash = context.config_hash;
  record.boot_partition_address = context.boot_partition_address;
  record.hp1_credit_ms = hp1_credit_ms;
  record.hp2_credit_ms = hp2_credit_ms;
  std::memcpy(record.image_hash, context.image_hash, sizeof(record.image_hash));
  restart_handoff::finalize_record(&record);

  const bool persisted = persist_handoff_record(record);
  if (!persisted) ESP_LOGE(TAG, "Restart handoff arm was not durably verified");
  return persisted;
}

bool arm_ota_handoff(uint32_t hp1_credit_ms, uint32_t hp2_credit_ms) {
  if (!storage_ready || !restart_handoff::valid_minimum_off_ms(configured_minimum_off_ms) ||
      hp1_credit_ms > configured_minimum_off_ms || hp2_credit_ms > configured_minimum_off_ms ||
      (hp1_credit_ms == 0U && hp2_credit_ms == 0U)) {
    return false;
  }

  BootContext context{};
  if (!capture_boot_context(configured_minimum_off_ms, &context) || !context.running_partition_matches_boot ||
      !context.image_valid) {
    ESP_LOGE(TAG, "OTA handoff arm rejected current image context");
    return false;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* target = running != nullptr ? esp_ota_get_next_update_partition(running) : nullptr;
  if (target == nullptr) {
    ESP_LOGE(TAG, "OTA handoff arm could not resolve the target partition");
    return false;
  }

  Record record{};
  record.magic = restart_handoff::kRecordMagic;
  record.version = restart_handoff::kRecordVersion;
  record.state = restart_handoff::kRecordOtaArmed;
  record.minimum_off_ms = configured_minimum_off_ms;
  record.config_hash = context.config_hash;
  record.boot_partition_address = target->address;
  record.hp1_credit_ms = hp1_credit_ms;
  record.hp2_credit_ms = hp2_credit_ms;
  // OTA does not know the target ELF hash before flashing. Store the source
  // hash so rollback-disabled boots can still prove that the image changed.
  std::memcpy(record.image_hash, context.image_hash, sizeof(record.image_hash));
  restart_handoff::finalize_record(&record);

  const bool persisted = persist_handoff_record(record);
  if (!persisted) ESP_LOGE(TAG, "OTA handoff arm was not durably verified");
  return persisted;
}

bool clear_restart_handoff() {
  if (!storage_ready) return false;
  nvs_handle_t handle{};
  if (!open_storage(&handle)) return false;
  NvsStorage storage(handle);
  const bool cleared = storage.erase_and_verify_absent();
  nvs_close(handle);
  if (!cleared) ESP_LOGE(TAG, "Restart handoff could not be durably cleared");
  return cleared;
}

void OpenQuattOtaHandoff::setup() {
#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
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

#ifdef USE_OTA_STATE_LISTENER
void OpenQuattOtaHandoff::on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                                              ota::OTAComponent* component) {
  (void)progress;
  (void)error;
  (void)component;

  if (state == ota::OTA_STARTED) {
    const uint32_t now_ms = millis();
    const uint32_t hp1_credit_ms = this->full_credit_if_confirmed_(1U, now_ms);
    const uint32_t hp2_credit_ms = this->full_credit_if_confirmed_(2U, now_ms);

    this->ota_handoff_attempted_ = hp1_credit_ms != 0U || hp2_credit_ms != 0U;
    this->ota_handoff_saved_ = this->ota_handoff_attempted_ && arm_ota_handoff(hp1_credit_ms, hp2_credit_ms);
    ESP_LOGI(TAG, "Controlled OTA: confirmed full off-time credit HP1=%us HP2=%us (%s)",
             static_cast<unsigned>(hp1_credit_ms / 1000U), static_cast<unsigned>(hp2_credit_ms / 1000U),
             this->ota_handoff_saved_ ? "saved" : "conservative fallback");
    return;
  }

  if (state == ota::OTA_ABORT || state == ota::OTA_ERROR) {
    if (this->ota_handoff_attempted_ && !clear_restart_handoff()) {
      ESP_LOGE(TAG, "Could not clear OTA handoff after failed OTA; rebooting fail-closed");
      App.safe_reboot();
      return;
    }
    this->ota_handoff_attempted_ = false;
    this->ota_handoff_saved_ = false;
    return;
  }

  if (state == ota::OTA_COMPLETED && this->ota_handoff_saved_) {
    ESP_LOGI(TAG, "Controlled OTA completed with confirmed off-time handoff armed");
  }
}
#endif

}  // namespace esphome::openquatt_incident_manager
