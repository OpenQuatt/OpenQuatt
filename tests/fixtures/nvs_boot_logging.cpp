#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using esp_err_t = int;
using esp_log_level_t = int;
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
[[maybe_unused]] constexpr int ESP_LOG_INFO = 3;
constexpr int CHIP_ESP32S3 = 9, ESP_IMAGE_VERIFY = 0, NVS_READWRITE = 1;
constexpr int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
constexpr int ESP_OTA_IMG_NEW = 0, ESP_OTA_IMG_PENDING_VERIFY = 1, ESP_OTA_IMG_VALID = 2, ESP_OTA_IMG_UNDEFINED = -1;
using esp_ota_img_states_t = int;
struct esp_chip_info_t {
  int model;
};
struct esp_partition_t {
  uint32_t address, size;
};
using esp_partition_pos_t = esp_partition_t;
struct esp_image_metadata_t {
  uint32_t image_len;
};
struct Halt {};
struct Restart {};
struct UnconstructedLogger {};
static std::vector<uint8_t> flash_bytes(0x1000000, 0xFF);
static esp_partition_t running{0x20000, 0x500000};
static std::string logs;
static unsigned app_calls, nvs_calls, writes, erases, logger_calls, level_calls;
static int chip_model = CHIP_ESP32S3;
static bool allocation_fails, nvs_fails;

[[maybe_unused]] static int esp_rom_printf(const char* format, ...) {
  char line[512];
  va_list args;
  va_start(args, format);
  int length = std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  assert(length >= 0 && static_cast<size_t>(length) < sizeof(line));
  logs.append(line, static_cast<size_t>(length));
  return length;
}
[[maybe_unused]] static void unavailable_logger(const char*, ...) {
  ++logger_calls;
  throw UnconstructedLogger{};
}
// Model ESPHome's PCH remapping before Logger::pre_setup() has run.
#define ESP_LOGI(tag, ...) unavailable_logger(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) unavailable_logger(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) unavailable_logger(tag, __VA_ARGS__)
[[maybe_unused]] static int esp_log_level_get(const char*) {
  ++level_calls;
  return 1;
}
[[maybe_unused]] static void esp_log_level_set(const char*, int) { ++level_calls; }
static void* heap_caps_malloc(size_t n, int) { return allocation_fails ? nullptr : std::malloc(n); }
static void heap_caps_free(void* p) { std::free(p); }
static void vTaskDelay(unsigned) {
  if (logs.find("HALT") != std::string::npos) throw Halt{};
}
#define pdMS_TO_TICKS(value) (value)
static void esp_chip_info(esp_chip_info_t* info) { info->model = chip_model; }
static int esp_flash_get_size(void*, uint32_t* size) {
  *size = flash_bytes.size();
  return ESP_OK;
}
static bool esp_secure_boot_enabled() { return false; }
static bool esp_flash_encryption_enabled() { return false; }
static const esp_partition_t* esp_ota_get_running_partition() { return &running; }
static int esp_image_verify(int, const esp_partition_pos_t*, esp_image_metadata_t* image) {
  image->image_len = 4;
  return ESP_OK;
}
static int esp_ota_get_state_partition(const esp_partition_t*, int* state) {
  *state = ESP_OTA_IMG_UNDEFINED;
  return ESP_OK;
}
static int esp_ota_mark_app_valid_cancel_rollback() { return ESP_OK; }
static int esp_flash_read(void*, void* output, uint32_t address, uint32_t size) {
  assert(static_cast<uint64_t>(address) + size <= flash_bytes.size());
  std::memcpy(output, flash_bytes.data() + address, size);
  return ESP_OK;
}
static int esp_flash_write(void*, const void* input, uint32_t address, uint32_t size) {
  assert(static_cast<uint64_t>(address) + size <= flash_bytes.size());
  const auto* bytes = static_cast<const uint8_t*>(input);
  for (uint32_t i = 0; i < size; ++i) {
    assert((flash_bytes[address + i] & bytes[i]) == bytes[i]);
    flash_bytes[address + i] &= bytes[i];
  }
  ++writes;
  return ESP_OK;
}
static int esp_flash_erase_region(void*, uint32_t address, uint32_t size) {
  assert(address % 4096 == 0 && size % 4096 == 0);
  assert(static_cast<uint64_t>(address) + size <= flash_bytes.size());
  std::fill_n(flash_bytes.begin() + address, size, 0xFF);
  ++erases;
  return ESP_OK;
}
[[noreturn]] static void esp_restart() { throw Restart{}; }
static int nvs_flash_init() {
  ++nvs_calls;
  return nvs_fails ? ESP_FAIL : ESP_OK;
}
static int nvs_open(const char*, int, nvs_handle_t* handle) {
  *handle = 1;
  return ESP_OK;
}
static void nvs_close(nvs_handle_t) {}
extern "C" void __real_app_main() { ++app_calls; }

#include "components/openquatt_partition_migration/migration.cpp"

static void reset(bool old = false) {
  std::fill(flash_bytes.begin(), flash_bytes.end(), 0xFF);
  using namespace esphome::openquatt_partition_migration;
  const auto& table = old ? OLD_TABLE : NEW_TABLE;
  std::copy(table.begin(), table.end(), flash_bytes.begin() + 0x8000);
  app_calls = nvs_calls = writes = erases = logger_calls = level_calls = 0;
  chip_model = CHIP_ESP32S3;
  allocation_fails = nvs_fails = false;
  logs.clear();
}
static void unchanged_logging_state() { assert(logger_calls == 0 && level_calls == 0); }
static void halted(const char* reason) {
  try {
    __wrap_app_main();
    assert(false);
  } catch (const Halt&) {
  }
  assert(app_calls == 0 && logs.find(reason) != std::string::npos);
  unchanged_logging_state();
}
int main() {
  try {
    reset();
    __wrap_app_main();
    assert(app_calls == 1 && nvs_calls == 1 && writes == 0 && erases == 0);
    assert(logs.find("EARLY before ESPHome") != std::string::npos);
    assert(logs.find("verified running=") != std::string::npos);
    assert(logs.find("migration flash writes=0 erased_sectors=0") != std::string::npos);
    assert(logs.find("READY actual table is 64 KiB") != std::string::npos);
    unchanged_logging_state();

    reset(true);
    try {
      __wrap_app_main();
      assert(false);
    } catch (const Restart&) {
    }
    assert(app_calls == 0 && nvs_calls == 0 && writes > 0 && erases > 0);
    assert(logs.find("erase address=") != std::string::npos);
    assert(logs.find("migration committed; restarting") != std::string::npos);
    unchanged_logging_state();
    __wrap_app_main();
    assert(app_calls == 1 && nvs_calls == 1);
    unchanged_logging_state();

    reset();
    chip_model = 0;
    halted("hardware/security mismatch");
    assert(nvs_calls == 0 && writes == 0);
    reset();
    allocation_fails = true;
    halted("internal scratch allocation failed");
    assert(nvs_calls == 0 && writes == 0);
    reset();
    flash_bytes[0x8000] = 0;
    halted("preflight, journal or flash verification failed");
    assert(nvs_calls == 0 && writes == 0);
    reset();
    nvs_fails = true;
    halted("NVS preflight failed");
    assert(nvs_calls == 1 && writes == 0);
    std::puts("PASS: pre-logger READY, migration/restart and fail-closed HALT; all seven ROM log sites");
  } catch (const UnconstructedLogger&) {
    std::fputs("FAIL: ESPHome logger used before construction\n", stderr);
    return 1;
  }
}
