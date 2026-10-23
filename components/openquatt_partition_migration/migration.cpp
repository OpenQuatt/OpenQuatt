// This runs before app_main constructs the ESPHome logger. Use the ROM console
// directly: ESP_LOG_* may already be remapped by the ESPHome precompiled header.

#include "migration_engine.h"
#include "partition_tables.h"

#include "esphome/core/defines.h"
#include "esp_flash.h"
#include "esp_flash_encrypt.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_rom_sys.h"
#include "esp_chip_info.h"
#include "esp_ota_ops.h"
#include "esp_secure_boot.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace esphome::openquatt_partition_migration {

class DeviceFlash : public Flash {
 public:
  DeviceFlash(const esp_partition_t* running, uint32_t image_size) : running_(running), image_size_(image_size) {
    this->scratch_ = static_cast<uint8_t*>(heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  ~DeviceFlash() override { heap_caps_free(this->scratch_); }
  bool allocated() const { return this->scratch_ != nullptr; }
  uint32_t writes() const { return this->writes_; }
  uint32_t erased_sectors() const { return this->erased_sectors_; }
  bool read(uint32_t address, void* data, uint32_t size) override {
    if (esp_flash_read(nullptr, data, address, size) != ESP_OK) return false;
    // Bound idle-task starvation during full-image CRC passes before ESPHome.
    this->read_bytes_ += size;
    if (this->read_bytes_ >= 16384) {
      this->read_bytes_ = 0;
      vTaskDelay(1);
    }
    return true;
  }
  bool write(uint32_t address, const void* data, uint32_t size) override {
    if (size > 4096 || this->scratch_ == nullptr ||
        !writable_region(address, size, this->running_->address, this->image_size_, false))
      return false;
    // Never pass flash/PSRAM-owned input into a cache-disabled write path.
    std::memcpy(this->scratch_, data, size);
    bool ok = esp_flash_write(nullptr, this->scratch_, address, size) == ESP_OK;
    if (ok) ++this->writes_;
    vTaskDelay(1);
    return ok;
  }
  bool erase(uint32_t address, uint32_t size) override {
    ::esp_rom_printf("oq.migration: erase address=0x%lx size=0x%lx\n", static_cast<unsigned long>(address),
                     static_cast<unsigned long>(size));
    if (address % 4096 != 0 || size % 4096 != 0 ||
        !writable_region(address, size, this->running_->address, this->image_size_, true))
      return false;
    for (uint32_t offset = 0; offset < size; offset += 4096) {
      if (esp_flash_erase_region(nullptr, address + offset, 4096) != ESP_OK) return false;
      ++this->erased_sectors_;
      vTaskDelay(1);
    }
    return true;
  }
  bool confirm_image() override {
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(this->running_, &state) != ESP_OK) return false;
    if (state == ESP_OTA_IMG_NEW || state == ESP_OTA_IMG_PENDING_VERIFY)
      return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
    return state == ESP_OTA_IMG_VALID || state == ESP_OTA_IMG_UNDEFINED;
  }

 private:
  uint8_t* scratch_{nullptr};
  uint32_t read_bytes_{0};
  uint32_t writes_{0};
  uint32_t erased_sectors_{0};
  const esp_partition_t* running_;
  uint32_t image_size_;
};

[[noreturn]] static void stop(const char* why) {
  ::esp_rom_printf("oq.migration: HALT before ESPHome/NVS init: %s; USB recovery required\n", why);
  // Do not hand a damaged NVS/partial transaction to ESPHome's erase-on-open-error
  // preference backend. No WiFi, OTA receiver or control callback has started.
  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

static void early_boot() {
  ::esp_rom_printf("oq.migration: EARLY before ESPHome app_main / preferences init\n");
  uint32_t size = 0;
  esp_chip_info_t chip{};
  esp_chip_info(&chip);
  if (chip.model != CHIP_ESP32S3 || esp_flash_get_size(nullptr, &size) != ESP_OK || size != 0x1000000 ||
      esp_secure_boot_enabled() || esp_flash_encryption_enabled())
    stop("hardware/security mismatch");
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr || running->size != 0x500000 || (running->address != 0x20000 && running->address != 0x520000))
    stop("running image outside fixed slots");
  esp_partition_pos_t position{running->address, running->size};
  esp_image_metadata_t image{};
  if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &image) != ESP_OK || image.image_len == 0 ||
      image.image_len > running->size || image.image_len % 4 != 0)
    stop("invalid running image");
  Result result;
  {
    DeviceFlash flash(running, image.image_len);
    if (!flash.allocated()) stop("internal scratch allocation failed");
    uint32_t crc = UINT32_MAX;
    uint8_t buffer[256];
    for (uint32_t offset = 0; offset < image.image_len;) {
      uint32_t count = image.image_len - offset;
      if (count > sizeof(buffer)) count = sizeof(buffer);
      if (!flash.read(running->address + offset, buffer, count)) stop("image read failed");
      crc = crc_update(crc, buffer, count);
      offset += count;
    }
    ::esp_rom_printf("oq.migration: verified running=0x%lx image_size=%lu; checking Q NVS migration\n",
                     static_cast<unsigned long>(running->address), static_cast<unsigned long>(image.image_len));
    Engine engine(flash, OLD_TABLE.data(), NEW_TABLE.data(), running->address, image.image_len, crc);
    result = engine.run();
    ::esp_rom_printf("oq.migration: migration flash writes=%lu erased_sectors=%lu\n",
                     static_cast<unsigned long>(flash.writes()), static_cast<unsigned long>(flash.erased_sectors()));
  }
  if (result == Result::FAILED) stop("preflight, journal or flash verification failed");
  if (result == Result::REBOOT || result == Result::INJECTED_RESET) {
    ::esp_rom_printf("oq.migration: %s; restarting before ESPHome starts\n",
                     result == Result::INJECTED_RESET ? "injected checkpoint reset" : "migration committed");
    esp_restart();
  }
  // Fail closed before ESPHome's preference backend can erase on open errors.
  nvs_handle_t handle;
  if (nvs_flash_init() != ESP_OK || nvs_open("esphome", NVS_READWRITE, &handle) != ESP_OK) stop("NVS preflight failed");
  nvs_close(handle);
  ::esp_rom_printf("oq.migration: READY actual table is 64 KiB; entering ESPHome application\n");
}
}  // namespace esphome::openquatt_partition_migration

extern "C" void __real_app_main();
extern "C" void __wrap_app_main() {
  esphome::openquatt_partition_migration::early_boot();
  __real_app_main();
}
