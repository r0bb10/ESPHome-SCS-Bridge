#include <cassert>
#include <cstdint>

#include "../components/scs_bus/scs_own.h"

using esphome::scs_bus::DoorUnlock;
using esphome::scs_bus::DoorbellEvent;
using esphome::scs_bus::ScsFrame;
using esphome::scs_bus::ScsFrameType;
using esphome::scs_bus::scs_build_door_unlock;
using esphome::scs_bus::scs_decode_doorbell;

int main() {
  const uint8_t doorbell_payload[] = {0x91, 0x42, 0x60, 0x08};
  ScsFrame doorbell;
  assert(ScsFrame::build(doorbell, doorbell_payload, sizeof(doorbell_payload)));
  assert(doorbell.is_valid());
  assert(doorbell.bytes[0] == 0xA8 && doorbell.bytes[5] == 0xBB && doorbell.bytes[6] == 0xA3);

  DoorbellEvent event{};
  assert(scs_decode_doorbell(doorbell, event));
  assert(event.address == 0x42);

  ScsFrame other = doorbell;
  other.bytes[1] = 0x90;
  assert(!scs_decode_doorbell(other, event));
  assert(!scs_decode_doorbell(ScsFrame::acknowledgment(), event));

  ScsFrame unlock;
  assert(scs_build_door_unlock(DoorUnlock{0x42}, unlock));
  assert(unlock.type == ScsFrameType::STANDARD && unlock.is_valid());
  const uint8_t expected_unlock[] = {0xA8, 0x96, 0xA2, 0x6F, 0xA4, 0xFF, 0xA3};
  for (size_t index = 0; index < sizeof(expected_unlock); ++index)
    assert(unlock.bytes[index] == expected_unlock[index]);

  assert(scs_build_door_unlock(DoorUnlock{0xF2}, unlock));
  assert(unlock.bytes[2] == 0xA2);
  return 0;
}
