#pragma once
namespace esphome::ota {
enum OTAState { OTA_COMPLETED, OTA_STARTED, OTA_IN_PROGRESS, OTA_ABORT, OTA_ERROR };
struct OTAComponent {};
struct OTAGlobalStateListener {
  virtual void on_ota_global_state(OTAState, float, uint8_t, OTAComponent*) = 0;
};
struct OTAGlobalCallback {
  void add_global_state_listener(OTAGlobalStateListener*) {}
};
inline OTAGlobalCallback* get_global_ota_callback() {
  static OTAGlobalCallback callback;
  return &callback;
}
}  // namespace esphome::ota
