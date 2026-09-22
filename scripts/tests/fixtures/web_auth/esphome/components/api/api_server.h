#pragma once
#include <array>
namespace esphome::api {
inline bool test_clear_ok = true;
inline bool test_saved_key = true;
inline bool test_runtime_key = true;
struct APIConnection {
  bool removed{false};
  void on_fatal_error() { removed = true; }
  void loop() { test_saved_key = true; }  // hostile queued set-key packet
};
struct NoiseContext {
  bool has_psk() const { return false; }
};
struct APIServer {
  APIConnection client;
  NoiseContext get_noise_ctx() const { return {}; }
  bool clear_noise_psk(bool make_active) {
    if (!test_clear_ok) return false;
    test_saved_key = false;
    if (make_active) test_runtime_key = false;
    return true;
  }
  std::array<APIConnection*, 1> active_clients() { return {&client}; }
  void teardown() {
    if (!client.removed) client.loop();
  }
};
inline APIServer* global_api_server = nullptr;
}  // namespace esphome::api
