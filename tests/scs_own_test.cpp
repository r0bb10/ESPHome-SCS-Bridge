#include <cassert>

#include "../components/scs_bus/scs_own.h"

using esphome::scs_bus::DoorUnlock;
using esphome::scs_bus::DoorbellEvent;
using esphome::scs_bus::ScsTelegram;
using esphome::scs_bus::scs_build_door_unlock;
using esphome::scs_bus::scs_decode_doorbell;

int main() {
  const uint8_t payload[] = {0x91, 0x42, 0x60, 0x08};
  ScsTelegram doorbell;
  assert(ScsTelegram::build(doorbell, payload, sizeof(payload)));
  DoorbellEvent event{};
  assert(scs_decode_doorbell(doorbell, event) && event.address == 0x42);

  ScsTelegram unlock;
  assert(scs_build_door_unlock(DoorUnlock{0x42}, unlock) && unlock.is_valid());
  return 0;
}
