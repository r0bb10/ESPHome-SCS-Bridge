#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/component.h"
#include "scs_bus.h"

namespace esphome {
namespace scs_bus {

enum ScsButtonCommand : uint8_t { DOOR_UNLOCK };

class ScsButton : public button::Button, public Component {
 public:
  void set_scs_bus(ScsBus *bus) { bus_ = bus; }
  void set_address(uint8_t address) { address_ = address; }
  void set_command(ScsButtonCommand command) { command_ = command; }

 protected:
  void press_action() override;

  ScsBus *bus_{nullptr};
  uint8_t address_{0};
  ScsButtonCommand command_{ScsButtonCommand::DOOR_UNLOCK};
};

}  // namespace scs_bus
}  // namespace esphome
