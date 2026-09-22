#pragma once
namespace esphome::api {
struct NoiseContext {
  bool has_psk() const { return false; }
};
struct APIServer {
  NoiseContext get_noise_ctx() const { return {}; }
};
inline APIServer* global_api_server = nullptr;
}  // namespace esphome::api
