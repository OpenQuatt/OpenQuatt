#pragma once

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "nvs.h"
#include "nvs_flash.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/log.h"

namespace oq_nvs_cleanup {

static constexpr const char* TAG = "openquatt.nvs";
static constexpr const char* ESPHOME_NAMESPACE = "esphome";

inline void log_stats(const char* phase) {
  nvs_stats_t stats{};
  const esp_err_t err = nvs_get_stats(nullptr, &stats);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "NVS stats unavailable at %s: %s", phase, esp_err_to_name(err));
    return;
  }
  ESP_LOGI(TAG, "NVS %s: used=%u free=%u available=%u total=%u namespaces=%u", phase,
           static_cast<unsigned>(stats.used_entries), static_cast<unsigned>(stats.free_entries),
           static_cast<unsigned>(stats.available_entries), static_cast<unsigned>(stats.total_entries),
           static_cast<unsigned>(stats.namespace_count));
}

inline bool erase_esphome_preferences(std::span<const uint32_t> keys, const char* reason) {
  nvs_handle_t handle{};
  const esp_err_t open_err = nvs_open(ESPHOME_NAMESPACE, NVS_READWRITE, &handle);
  if (open_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not open retired preferences for %s: %s", reason, esp_err_to_name(open_err));
    log_stats("cleanup-open-failed");
    return false;
  }

  size_t erased = 0U;
  size_t failed = 0U;
  for (const uint32_t key : keys) {
    char key_text[12];
    std::snprintf(key_text, sizeof(key_text), "%" PRIu32, key);
    const esp_err_t erase_err = nvs_erase_key(handle, key_text);
    if (erase_err == ESP_OK) {
      ++erased;
    } else if (erase_err != ESP_ERR_NVS_NOT_FOUND) {
      ++failed;
      ESP_LOGE(TAG, "Could not retire preference key %s for %s: %s", key_text, reason, esp_err_to_name(erase_err));
    }
  }

  esp_err_t commit_err = ESP_OK;
  if (erased > 0U) commit_err = nvs_commit(handle);
  nvs_close(handle);

  if (commit_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not commit retired preferences for %s: %s", reason, esp_err_to_name(commit_err));
    ++failed;
  }
  if (erased > 0U || failed > 0U) {
    ESP_LOGI(TAG, "Retired preferences for %s: erased=%u failed=%u", reason, static_cast<unsigned>(erased),
             static_cast<unsigned>(failed));
    log_stats("after-cleanup");
  }
  return failed == 0U;
}

inline bool erase_esphome_blob_if_size(uint32_t key, size_t expected_size, const char* reason) {
  nvs_handle_t handle{};
  const esp_err_t open_err = nvs_open(ESPHOME_NAMESPACE, NVS_READONLY, &handle);
  if (open_err == ESP_ERR_NVS_NOT_FOUND) return true;
  if (open_err != ESP_OK) {
    ESP_LOGW(TAG, "Could not inspect retired preference for %s: %s", reason, esp_err_to_name(open_err));
    return false;
  }

  char key_text[12];
  std::snprintf(key_text, sizeof(key_text), "%" PRIu32, key);
  size_t length = 0U;
  const esp_err_t read_err = nvs_get_blob(handle, key_text, nullptr, &length);
  nvs_close(handle);
  if (read_err == ESP_ERR_NVS_NOT_FOUND) return true;
  if (read_err != ESP_OK || length != expected_size) {
    ESP_LOGW(TAG, "Keeping unexpected retired key %s for %s: %s, bytes=%u", key_text, reason, esp_err_to_name(read_err),
             static_cast<unsigned>(length));
    return false;
  }

  // These retired keys have no current writer. Inspect only type/length;
  // never allocate or log their payload (which may contain credentials).
  const std::array<uint32_t, 1> keys{{key}};
  return erase_esphome_preferences(keys, reason);
}

inline uint32_t legacy_entity_preference_key(esphome::EntityBase* entity) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  const uint32_t key = entity->get_preference_hash();
#pragma GCC diagnostic pop
  return key;
}

template <size_t N>
inline bool erase_entity_preferences(const std::array<esphome::EntityBase*, N>& entities, const char* reason) {
  std::array<uint32_t, N> keys{};
  for (size_t index = 0; index < N; ++index) keys[index] = legacy_entity_preference_key(entities[index]);
  return erase_esphome_preferences(keys, reason);
}

// Called from both existing boot-hook forms because package merging can
// replace dict-form hooks with lists or vice versa. Repeated calls are safe.
inline void retire_openquatt_preferences(esphome::EntityBase* air_purge) {
  const std::array<esphome::EntityBase*, 1> retired_air_purge_preferences{{
      air_purge,
  }};
  oq_nvs_cleanup::erase_entity_preferences(retired_air_purge_preferences, "air purge session choice");
  // Restoring globals: 1944399030U XOR the first 32 bits of MD5(id).
  constexpr std::array<uint32_t, 8> retired_cycling_alert_preferences{{
      3739268635U,  // oq_compressor_cycling_alert_latched
      2888263739U,  // oq_compressor_cycling_alert_first_seen_epoch
      556120657U,   // oq_compressor_cycling_alert_last_seen_epoch
      3778223577U,  // oq_compressor_cycling_alert_alternating
      3433032332U,  // oq_compressor_cycling_alert_hp1_peak_2h_value
      390691303U,   // oq_compressor_cycling_alert_hp1_peak_72h_value
      393124903U,   // oq_compressor_cycling_alert_hp2_peak_2h_value
      3948348002U,  // oq_compressor_cycling_alert_hp2_peak_72h_value
  }};
  oq_nvs_cleanup::erase_esphome_preferences(retired_cycling_alert_preferences, "session compressor cycling alerts");
  // FNV-1("openquatt_api_security_store"): retired 40-byte custom
  // API security blob. Native ESPHome Noise PSK (88491486) stays intact.
  oq_nvs_cleanup::erase_esphome_blob_if_size(1156115452U, 40U, "legacy API security store");
  // CrashRecord v1 moved to openquatt_data. Retire only its old
  // 2812-byte NVS blob; keep the separate crash consent state.
  // FNV-1("openquatt_crash_telemetry_record"). No payload allocation.
  oq_nvs_cleanup::erase_esphome_blob_if_size(752195988U, 2812U, "legacy crash record in NVS");
}

}  // namespace oq_nvs_cleanup
