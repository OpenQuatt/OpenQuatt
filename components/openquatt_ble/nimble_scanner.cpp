#include "nimble_scanner.h"

#ifdef USE_OPENQUATT_BLE_NIMBLE

#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
}

namespace esphome::openquatt_ble {
namespace {
constexpr char TAG[] = "oq_nimble";
constexpr uint16_t SCAN_INTERVAL = 512;  // 320 ms in 0.625 ms units.
constexpr uint16_t SCAN_WINDOW = 48;     // 30 ms in 0.625 ms units.
}  // namespace

NimbleScanner* NimbleScanner::instance_{};

void NimbleScanner::set_enabled(bool enabled) {
  if (!enabled) {
    enabled_ = false;
    if (started_) {
      const int result = nimble_port_stop();
      if (result != 0) {
        ESP_LOGE(TAG, "NimBLE host did not stop (%d)", result);
        failed_ = true;
        return;
      }
      // The stop event has drained the host loop. Delete its parked task before
      // deinitializing resources that the next enable will recreate.
      esp_nimble_disable();
      started_ = false;
      if (const auto error = nimble_port_deinit(); error != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE deinit failed (%s)", esp_err_to_name(error));
        failed_ = true;
      }
    }
    synced_ = scanning_ = false;
    std::lock_guard guard(queue_lock_);
    head_ = count_ = 0;
    return;
  }
  if (started_ || failed_) return;
  if (!queue_.allocate()) {
    ESP_LOGE(TAG, "Could not allocate advertisement queue in PSRAM");
    failed_ = true;
    return;
  }
  if (const auto error = nimble_port_init(); error != ESP_OK) {
    ESP_LOGE(TAG, "NimBLE init failed (%s)", esp_err_to_name(error));
    failed_ = true;
    return;
  }
  instance_ = this;
  ble_hs_cfg.sync_cb = on_sync_;
  ble_hs_cfg.reset_cb = on_reset_;
  enabled_ = true;
  if (const auto error = esp_nimble_enable(reinterpret_cast<void*>(host_task_)); error != ESP_OK) {
    ESP_LOGE(TAG, "NimBLE host task failed to start (%s)", esp_err_to_name(error));
    enabled_ = false;
    nimble_port_deinit();
    failed_ = true;
    return;
  }
  started_ = true;
  last_scan_attempt_ = millis() - 1000;
}

void NimbleScanner::service(uint32_t now) {
  if (!enabled_ || !synced_ || scanning_ || now - last_scan_attempt_ < 1000) return;
  last_scan_attempt_ = now;
  uint8_t own_address_type;
  if (ble_hs_id_infer_auto(0, &own_address_type) != 0) return;
  ble_gap_disc_params params{};
  params.itvl = SCAN_INTERVAL;
  params.window = SCAN_WINDOW;
  params.passive = 1;
  // Some thermometers reuse a MAC for updated readings. Decoders handle duplicate packets.
  params.filter_duplicates = 0;
  scanning_ = true;
  const int result = ble_gap_disc(own_address_type, BLE_HS_FOREVER, &params, on_gap_, this);
  if (result != 0 && result != BLE_HS_EALREADY) {
    scanning_ = false;
    ESP_LOGW(TAG, "NimBLE scan could not start (%d)", result);
  }
}

bool NimbleScanner::pop(Packet& packet) {
  std::lock_guard guard(queue_lock_);
  if (count_ == 0) return false;
  packet = queue_[head_];
  head_ = (head_ + 1) % QUEUE_CAPACITY;
  --count_;
  return true;
}

void NimbleScanner::host_task_(void*) {
  nimble_port_run();
  vTaskSuspend(nullptr);
}

void NimbleScanner::on_sync_() { instance_->synced_ = true; }

void NimbleScanner::on_reset_(int reason) {
  auto& scanner = *instance_;
  scanner.synced_ = scanner.scanning_ = false;
  std::lock_guard guard(scanner.queue_lock_);
  scanner.head_ = scanner.count_ = 0;
  ESP_LOGW(TAG, "NimBLE host reset (%d)", reason);
}

int NimbleScanner::on_gap_(ble_gap_event* event, void* arg) {
  auto& scanner = *static_cast<NimbleScanner*>(arg);
  if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) scanner.scanning_ = false;
  if (event->type != BLE_GAP_EVENT_DISC || !scanner.enabled_ || !scanner.synced_) return 0;
  const auto& message = event->disc;
  if (!message.data || message.length_data == 0 || message.length_data > Packet{}.data.size()) return 0;

  // The host only hands off bounded raw packets. All protocol and registry work
  // stays on the ESPHome loop. Skip a packet when the queue is full or contended.
  std::unique_lock guard(scanner.queue_lock_, std::try_to_lock);
  if (!guard || !scanner.enabled_ || scanner.count_ == QUEUE_CAPACITY) return 0;
  auto& packet = scanner.queue_[(scanner.head_ + scanner.count_) % QUEUE_CAPACITY];
  packet.address = 0;
  for (size_t i = sizeof(message.addr.val); i > 0; --i)
    packet.address = (packet.address << 8) | message.addr.val[i - 1];
  packet.rssi = message.rssi;
  packet.received_at = millis();
  packet.kind = message.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP ? AdvertisementKind::SCAN_RESPONSE
                                                                      : AdvertisementKind::ADVERTISEMENT;
  packet.length = message.length_data;
  std::memcpy(packet.data.data(), message.data, packet.length);
  ++scanner.count_;
  return 0;
}

}  // namespace esphome::openquatt_ble
#endif
