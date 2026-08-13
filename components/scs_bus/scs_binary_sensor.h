#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "scs_bus.h"

namespace esphome {
namespace scs_bus {

enum ScsBinarySensorFunction : uint8_t { DOORBELL };

class ScsBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override;
  void set_scs_bus(ScsBus *bus) { bus_ = bus; }
  void set_address(uint8_t address) { address_ = address; }
  void set_function(ScsBinarySensorFunction function) { function_ = function; }

 protected:
  ScsBus *bus_{nullptr};
  uint8_t address_{0};
  ScsBinarySensorFunction function_{ScsBinarySensorFunction::DOORBELL};
};

}  // namespace scs_bus
}  // namespace esphome
