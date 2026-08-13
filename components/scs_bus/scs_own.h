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

// Decodes the native doorbell notification frame for an addressed entrance.
bool scs_decode_doorbell(const ScsFrame &frame, DoorbellEvent &event);

// Builds the native standard frame that unlocks an addressed entrance.
bool scs_build_door_unlock(const DoorUnlock &command, ScsFrame &frame);

}  // namespace scs_bus
}  // namespace esphome
