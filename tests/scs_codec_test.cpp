#include <cassert>

#include "../components/scs_bus/scs_codec.h"

using esphome::scs_bus::ScsTelegram;
using esphome::scs_bus::ScsTelegramAssembler;
using esphome::scs_bus::ScsTelegramDeduplicator;
using esphome::scs_bus::ScsTelegramParseResult;
using esphome::scs_bus::ScsTelegramType;

static ScsTelegram make_telegram(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3) {
  ScsTelegram telegram;
  const uint8_t payload[] = {byte0, byte1, byte2, byte3};
  assert(ScsTelegram::build(telegram, payload, sizeof(payload)));
  return telegram;
}

int main() {
  const uint8_t payload[] = {0x34, 0x00, 0x12, 0x01};
  ScsTelegram transmitted;
  assert(ScsTelegram::build(transmitted, payload, sizeof(payload)));
  assert(transmitted.type == ScsTelegramType::STANDARD && transmitted.is_valid());

  ScsTelegramAssembler assembler;
  ScsTelegram received;
  for (size_t index = 0; index < transmitted.size(); index++)
    assert(assembler.push(transmitted.bytes[index], received) ==
           (index + 1 == transmitted.size() ? ScsTelegramParseResult::TELEGRAM : ScsTelegramParseResult::NONE));
  assert(received.is_valid());

  // MX rx_end checks the start marker and XOR, but intentionally does not
  // require the final byte to be A3 even though its TX path emits A3.
  ScsTelegram mx_accepted = transmitted;
  mx_accepted.bytes[mx_accepted.size() - 1] = 0;
  assert(mx_accepted.is_valid());

  const ScsTelegram ack = ScsTelegram::acknowledgment();
  assert(assembler.push(ack.bytes[0], received) == ScsTelegramParseResult::TELEGRAM && received.is_ack());

  // ACK telegrams are never duplicates and do not disturb tracking.
  {
    ScsTelegramDeduplicator deduplicator;
    const ScsTelegram telegram = make_telegram(0xB1, 0x00, 0x15, 0x00);
    assert(!deduplicator.is_duplicate(telegram, 1000));
    assert(!deduplicator.is_duplicate(ScsTelegram::acknowledgment(), 1010));
    assert(deduplicator.is_duplicate(telegram, 1020));
    assert(!deduplicator.is_duplicate(ScsTelegram::acknowledgment(), 1030));
    assert(deduplicator.suppressed() == 1);
  }

  // An OEM no-ACK burst of eight copies is suppressed to a single delivery.
  {
    ScsTelegramDeduplicator deduplicator;
    const ScsTelegram telegram = make_telegram(0xE4, 0x00, 0x00, 0x00);
    assert(!deduplicator.is_duplicate(telegram, 1000));
    for (uint32_t copy = 1; copy < 8; copy++)
      assert(deduplicator.is_duplicate(telegram, 1000 + copy * 32000));
    assert(deduplicator.suppressed() == 7);
    // Past the window since the last observed copy the telegram is a new event.
    assert(!deduplicator.is_duplicate(telegram, 1000 + 7 * 32000 + 150000));
    assert(deduplicator.suppressed() == 7);
  }

  // A different telegram is delivered and becomes the new baseline.
  {
    ScsTelegramDeduplicator deduplicator;
    assert(!deduplicator.is_duplicate(make_telegram(0xB1, 0x01, 0x15, 0x00), 1000));
    assert(!deduplicator.is_duplicate(make_telegram(0xB1, 0x00, 0x15, 0x00), 1100));
    assert(deduplicator.is_duplicate(make_telegram(0xB1, 0x00, 0x15, 0x00), 1200));
    assert(!deduplicator.is_duplicate(make_telegram(0xB1, 0x01, 0x15, 0x00), 1300));
    assert(deduplicator.suppressed() == 1);
  }

  // A custom window expires between copies spaced further apart.
  {
    ScsTelegramDeduplicator deduplicator(1000);
    const ScsTelegram telegram = make_telegram(0xB1, 0x00, 0x15, 0x00);
    assert(!deduplicator.is_duplicate(telegram, 1000));
    assert(deduplicator.is_duplicate(telegram, 1800));
    assert(!deduplicator.is_duplicate(telegram, 2801));
  }
  return 0;
}
