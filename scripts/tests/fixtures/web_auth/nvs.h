#pragma once
#include "esphome/core/preferences.h"
using esp_err_t = int;
inline constexpr int ESP_OK = 0;
inline constexpr int ESP_ERR_NVS_NOT_FOUND = 1;
inline bool test_probe_ok = true;
inline int nvs_get_blob(uint32_t, const char*, void*, size_t* length) {
  if (!test_probe_ok) return 2;
  if (esphome::test_saved.empty()) return ESP_ERR_NVS_NOT_FOUND;
  *length = esphome::test_saved.size();
  return ESP_OK;
}
