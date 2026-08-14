#include <cassert>

#include "../components/scs_bus/scs_codec.h"

using esphome::scs_bus::ScsTelegram;
using esphome::scs_bus::ScsTelegramAssembler;
using esphome::scs_bus::ScsTelegramParseResult;
using esphome::scs_bus::ScsTelegramType;

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

  const ScsTelegram ack = ScsTelegram::acknowledgment();
  assert(assembler.push(ack.bytes[0], received) == ScsTelegramParseResult::TELEGRAM && received.is_ack());
  return 0;
}
