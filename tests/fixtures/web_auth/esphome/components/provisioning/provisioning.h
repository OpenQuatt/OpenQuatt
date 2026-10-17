#pragma once
namespace esphome::provisioning {
struct ProvisioningManager {
  bool window_pending() const { return false; }
  bool closed() const { return false; }
};
inline ProvisioningManager* global_provisioning_manager = nullptr;
}  // namespace esphome::provisioning
