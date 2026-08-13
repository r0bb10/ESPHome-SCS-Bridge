#include "scs_button.h"

#include "esphome/core/log.h"

namespace esphome {
namespace scs_bus {

static const char *const TAG = "scs_bus.button";

void ScsButton::press_action() {
  if (bus_ == nullptr) {
    ESP_LOGE(TAG, "SCS bus is not configured");
    return;
  }
  if (command_ == DOOR_UNLOCK && !bus_->send_door_unlock(address_))
    ESP_LOGW(TAG, "Could not queue door unlock for address 0x%02X", address_);
}

}  // namespace scs_bus
}  // namespace esphome
