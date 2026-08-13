#include "scs_binary_sensor.h"

namespace esphome {
namespace scs_bus {

void ScsBinarySensor::setup() {
  if (bus_ == nullptr) {
    mark_failed();
    return;
  }
  if (function_ == DOORBELL) {
    bus_->add_doorbell_listener(address_, [this]() { publish_state(true); });
  }
}

}  // namespace scs_bus
}  // namespace esphome
