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

}  // namespace oq_nvs_cleanup
