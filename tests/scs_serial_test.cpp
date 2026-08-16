#include <cassert>
#include <cstdint>

#include "../components/scs_bus/scs_serial.h"

using esphome::scs_bus::ScsSerialReceiver;
using esphome::scs_bus::ScsSerialRun;
using esphome::scs_bus::scs_encode_serial_byte;

int main() {
  ScsSerialReceiver receiver;
  uint8_t byte = 0;

  assert(receiver.start());
  assert(!receiver.start());
  assert(ScsSerialReceiver::CLOCK_HZ == 40000000);
  assert(ScsSerialReceiver::ACTIVE_TICKS == 1387);
  assert(ScsSerialReceiver::CELL_TICKS == 4160);
  assert(receiver.next_sample_delay_ticks() == 914);
  assert(receiver.sample(0, &byte) == ScsSerialReceiver::SampleResult::NONE);
  assert(receiver.state() == ScsSerialReceiver::State::DATA);
  assert(receiver.next_sample_delay_ticks() == 3939);

  constexpr uint8_t expected = 0xA5;
  for (uint8_t bit = 0; bit < 8; bit++) {
    assert(receiver.bit_index() == bit);
    assert(receiver.sample((expected >> bit) & 1U, &byte) == ScsSerialReceiver::SampleResult::NONE);
  }
  assert(receiver.state() == ScsSerialReceiver::State::STOP);
  assert(receiver.next_sample_delay_ticks() == 4160);
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

  ScsSerialRun runs[19]{};
  const size_t run_count = scs_encode_serial_byte(0xA5, runs, 19);
  assert(run_count == 10);
  assert(runs[0].level == false && runs[0].duration_ticks == 1387);
  assert(runs[1].level == true && runs[1].duration_ticks == 6933);
  assert(runs[2].level == false && runs[2].duration_ticks == 1387);
  assert(runs[3].level == true && runs[3].duration_ticks == 6933);
  assert(runs[8].level == false && runs[8].duration_ticks == 1387);
  assert(runs[9].level == true && runs[9].duration_ticks == 11093);
  assert(scs_encode_serial_byte(0x00, runs, 1) == 0);

  for (uint16_t value = 0; value <= 0xFF; value++) {
    const size_t encoded_runs = scs_encode_serial_byte(static_cast<uint8_t>(value), runs, 19);
    assert(encoded_runs != 0);
    uint32_t total_ticks = 0;
    for (size_t index = 0; index < encoded_runs; index++)
      total_ticks += runs[index].duration_ticks;
    assert(total_ticks == ScsSerialReceiver::CELL_TICKS * 10);

    size_t run_index = 0;
    uint32_t run_end = runs[0].duration_ticks;
    for (uint8_t cell = 0; cell < 10; cell++) {
      const uint32_t sample_at = cell * ScsSerialReceiver::CELL_TICKS + ScsSerialReceiver::SAMPLE_TICKS;
      while (sample_at >= run_end) {
        run_index++;
        run_end += runs[run_index].duration_ticks;
      }
      const bool expected_level = cell == 0 ? false :
                                  cell == 9 ? true : (value & (1U << (cell - 1))) != 0;
      assert(runs[run_index].level == expected_level);
    }
  }
  return 0;
}
