#include "OpenQuattRestartHandoff.h"

#include <cstring>

#include "OpenQuattRestartHandoffPolicy.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
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
  // Without otadata rollback is unavailable, so there is no unverified OTA image
  // to accidentally credit. Otherwise accept only an explicit valid/undefined state.
  const bool state_safe =
      (state_result == ESP_OK && (image_state == ESP_OTA_IMG_VALID || image_state == ESP_OTA_IMG_UNDEFINED)) ||
      state_result == ESP_ERR_NOT_FOUND;
  if (!state_safe) {
    ESP_LOGW(TAG, "Restart handoff rejected unverified or unavailable image state: %s", esp_err_to_name(state_result));
    return false;
  }

  context->software_reset = esp_reset_reason() == ESP_RST_SW;
  context->running_partition_matches_boot = running->address == boot->address;
  context->image_valid = state_safe;
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
    ESP_LOGI(TAG, "Restored one-shot confirmed-off credit after controlled restart");
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
  record.state = restart_handoff::kRecordArmed;
  record.minimum_off_ms = configured_minimum_off_ms;
  record.config_hash = context.config_hash;
  record.boot_partition_address = context.boot_partition_address;
  record.hp1_credit_ms = hp1_credit_ms;
  record.hp2_credit_ms = hp2_credit_ms;
  std::memcpy(record.image_hash, context.image_hash, sizeof(record.image_hash));
  restart_handoff::finalize_record(&record);

  nvs_handle_t handle{};
  if (!open_storage(&handle)) return false;
  NvsStorage storage(handle);
  const bool persisted = restart_handoff::persist_record(storage, record);
  nvs_close(handle);
  if (!persisted) ESP_LOGE(TAG, "Restart handoff arm was not durably verified");
  return persisted;
}

}  // namespace esphome::openquatt_incident_manager
