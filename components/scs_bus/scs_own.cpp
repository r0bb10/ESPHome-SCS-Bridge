#include "scs_own.h"

namespace esphome {
namespace scs_bus {

bool scs_decode_doorbell(const ScsTelegram &telegram, DoorbellEvent &event) {
  if (telegram.type != ScsTelegramType::STANDARD || !telegram.is_valid())
    return false;

  const uint8_t *payload = telegram.payload();
  if (payload[0] != 0x91 || payload[2] != 0x60 || payload[3] != 0x08)
    return false;

  event.address = payload[1];
  return true;
}

bool scs_build_door_unlock(const DoorUnlock &command, ScsTelegram &telegram) {
  const uint8_t payload[] = {
      0x96,
      static_cast<uint8_t>(0xA0 | (command.address & 0x0F)),
      0x6F,
      0xA4,
  };
  return ScsTelegram::build(telegram, payload, sizeof(payload));
}

}  // namespace scs_bus
}  // namespace esphome
