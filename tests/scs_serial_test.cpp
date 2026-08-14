#include <cassert>
#include <cstdint>

#include "../components/scs_bus/scs_serial.h"

using esphome::scs_bus::ScsSerialReceiver;

int main() {
  ScsSerialReceiver receiver;
  uint8_t byte = 0;

  assert(receiver.start());
  assert(!receiver.start());
  assert(ScsSerialReceiver::CLOCK_HZ == 80000000);
  assert(ScsSerialReceiver::ACTIVE_TICKS == 2774);
  assert(ScsSerialReceiver::CELL_TICKS == 8321);
  assert(receiver.next_sample_delay_ticks() == 1828);
  assert(receiver.sample(0, &byte) == ScsSerialReceiver::SampleResult::NONE);
  assert(receiver.state() == ScsSerialReceiver::State::DATA);
  assert(receiver.next_sample_delay_ticks() == 7880);

  constexpr uint8_t expected = 0xA5;
  for (uint8_t bit = 0; bit < 8; bit++) {
    assert(receiver.bit_index() == bit);
    assert(receiver.sample((expected >> bit) & 1U, &byte) == ScsSerialReceiver::SampleResult::NONE);
  }
  assert(receiver.state() == ScsSerialReceiver::State::STOP);
  assert(receiver.next_sample_delay_ticks() == 8321);
  assert(receiver.sample(1, &byte) == ScsSerialReceiver::SampleResult::BYTE);
  assert(byte == expected);
  assert(!receiver.receiving());

  assert(receiver.start());
  assert(receiver.sample(1, &byte) == ScsSerialReceiver::SampleResult::INVALID);
  assert(!receiver.receiving());

  assert(receiver.start());
  assert(receiver.sample(0, &byte) == ScsSerialReceiver::SampleResult::NONE);
  for (uint8_t bit = 0; bit < 8; bit++)
    assert(receiver.sample(0, &byte) == ScsSerialReceiver::SampleResult::NONE);
  assert(receiver.sample(0, &byte) == ScsSerialReceiver::SampleResult::INVALID);
  return 0;
}
