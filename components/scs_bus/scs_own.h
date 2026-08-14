#pragma once

#include <cstdint>

#include "scs_codec.h"

namespace esphome {
namespace scs_bus {

struct DoorbellEvent {
  uint8_t address;
};

struct DoorUnlock {
  uint8_t address;
};

// Decodes the native doorbell notification telegram for an addressed entrance.
bool scs_decode_doorbell(const ScsTelegram &telegram, DoorbellEvent &event);

// Builds the native standard telegram that unlocks an addressed entrance.
bool scs_build_door_unlock(const DoorUnlock &command, ScsTelegram &telegram);

}  // namespace scs_bus
}  // namespace esphome
