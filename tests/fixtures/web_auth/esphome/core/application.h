#pragma once
#include "esphome/components/api/api_server.h"
namespace esphome {
inline unsigned test_reboots = 0;
struct Application {
  void safe_reboot() {
    ++test_reboots;
    if (api::global_api_server) api::global_api_server->teardown();
  }
};
inline Application App;
}  // namespace esphome
