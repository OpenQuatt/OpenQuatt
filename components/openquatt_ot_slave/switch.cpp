#include "switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace OpenQuattOTSlave {
static const char* const TAG = "oq.ot.thermostat.switch";

void OpenQuattOTSlaveSwitch::write_state(bool state) {
  if (this->write_callback_) {
    this->write_callback_(state);
  }
  this->publish_state(state);
}

void OpenQuattOTSlaveSwitch::setup() {
  auto initial_state = this->get_initial_state_with_restore_mode();
  if (initial_state.has_value()) {
    ESP_LOGD(TAG, "Herstelde switchstate %s", ONOFF(initial_state.value()));
    this->publish_state(initial_state.value());
  }
}

}  // namespace OpenQuattOTSlave
}  // namespace esphome
