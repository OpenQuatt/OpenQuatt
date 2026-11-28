#pragma once

#include "esphome/core/defines.h"

#ifdef USE_OPENQUATT_BLE_NIMBLE

#include <array>
#include <atomic>
#include <mutex>

#include "PsramBuffer.h"
#include "ble_advertisement.h"

struct ble_gap_event;

namespace esphome::openquatt_ble {

class NimbleScanner {
 public:
  static constexpr size_t QUEUE_CAPACITY = 16;
  struct Packet {
    uint64_t address;
    uint32_t received_at;
    int8_t rssi;
    uint8_t length;
    AdvertisementKind kind;
    std::array<uint8_t, 31> data;

    Advertisement view() const { return {address, data.data(), length, rssi, received_at, kind}; }
  };

  void set_enabled(bool enabled);
  void service(uint32_t now);
  bool pop(Packet& packet);
  bool ready() const { return synced_ && scanning_; }
  bool failed() const { return failed_; }

 private:
  static void host_task_(void*);
  static void on_sync_();
  static void on_reset_(int reason);
  static int on_gap_(ble_gap_event* event, void* arg);

  static NimbleScanner* instance_;
  openquatt_common::PsramObjectArray<Packet, QUEUE_CAPACITY> queue_;
  std::mutex queue_lock_;
  size_t head_{}, count_{};
  std::atomic<bool> enabled_{false}, synced_{false}, scanning_{false};
  bool started_{}, failed_{};
  uint32_t last_scan_attempt_{};
};

}  // namespace esphome::openquatt_ble
#endif
