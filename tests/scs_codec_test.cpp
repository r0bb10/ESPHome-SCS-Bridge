#include <cassert>
#include <cstddef>
#include <cstdint>

#include "../components/scs_bus/scs_codec.h"

using esphome::scs_bus::ScsFrame;
using esphome::scs_bus::ScsFrameAssembler;
using esphome::scs_bus::ScsFrameType;
using esphome::scs_bus::ScsParseResult;

namespace {

ScsFrame feed(ScsFrameAssembler &assembler, const ScsFrame &input) {
  ScsFrame output;
  for (size_t index = 0; index < input.size(); ++index) {
    const ScsParseResult result = assembler.push(input.bytes[index], output);
    assert(index + 1 == input.size() ? result == ScsParseResult::FRAME : result == ScsParseResult::NONE);
  }
  return output;
}

}  // namespace

int main() {
  static_assert(esphome::scs_bus::scs_pack_address(0x3, 0x4) == 0x34, "address packing");
  assert(esphome::scs_bus::scs_address_area(0xAB) == 0x0A);
  assert(esphome::scs_bus::scs_address_point(0xAB) == 0x0B);

  const uint8_t standard_payload[] = {0x34, 0x00, 0x12, 0x01};
  ScsFrame standard;
  assert(ScsFrame::build(standard, standard_payload, sizeof(standard_payload)));
  assert(standard.type == ScsFrameType::STANDARD);
  assert(standard.size() == 7 && standard.checksum() == 0x27 && standard.is_valid());

  ScsFrameAssembler assembler;
  ScsFrame received = feed(assembler, standard);
  assert(received.type == ScsFrameType::STANDARD && received.is_valid());

  const uint8_t extended_payload[] = {0xEC, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
  ScsFrame extended;
  assert(ScsFrame::build(extended, extended_payload, sizeof(extended_payload)));
  assert(extended.type == ScsFrameType::EXTENDED && extended.size() == 11 && extended.is_valid());
  received = feed(assembler, extended);
  assert(received.type == ScsFrameType::EXTENDED && received.is_valid());

  // The M4 chooses extended length from a D*/E* first payload byte, not from
  // whether byte seven happens to be A3.
  const uint8_t d_extended_payload[] = {0xD2, 0x01, 0x03, 0x02, 0x04, 0x05, 0x06, 0x07};
  ScsFrame d_extended;
  assert(ScsFrame::build(d_extended, d_extended_payload, sizeof(d_extended_payload)));
  received = feed(assembler, d_extended);
  assert(received.type == ScsFrameType::EXTENDED && received.is_valid());

  ScsFrame ack = ScsFrame::acknowledgment();
  assert(assembler.push(ack.bytes[0], received) == ScsParseResult::FRAME);
  assert(received.is_ack() && received.is_valid());

  ScsFrame corrupt = standard;
  corrupt.bytes[5] ^= 0x01;
  for (size_t index = 0; index < corrupt.size(); ++index) {
    const ScsParseResult result = assembler.push(corrupt.bytes[index], received);
    assert(index + 1 == corrupt.size() ? result == ScsParseResult::INVALID : result == ScsParseResult::NONE);
  }

  // rx_end does not reject a non-A3 terminator when the native XOR is valid.
  ScsFrame alternate_terminator = standard;
  alternate_terminator.bytes[6] = 0x00;
  received = feed(assembler, alternate_terminator);
  assert(received.type == ScsFrameType::STANDARD && received.is_valid());

  // A later A8 abandons the malformed candidate and starts a fresh frame.
  corrupt = standard;
  corrupt.bytes[5] = 0xA8;
  for (size_t index = 0; index < 6; ++index) {
    const ScsParseResult result = assembler.push(corrupt.bytes[index], received);
    assert(result == ScsParseResult::NONE);
  }
  for (size_t index = 1; index < standard.size(); ++index) {
    const ScsParseResult result = assembler.push(standard.bytes[index], received);
    assert(index + 1 == standard.size() ? result == ScsParseResult::FRAME : result == ScsParseResult::NONE);
  }
  assert(received.is_valid() && received.type == ScsFrameType::STANDARD);

  // A malformed extended frame resynchronizes to the next frame.
  ScsFrame malformed = extended;
  malformed.bytes[9] ^= 0x01;
  for (size_t index = 0; index < malformed.size(); ++index) {
    const ScsParseResult result = assembler.push(malformed.bytes[index], received);
    assert(index + 1 == malformed.size() ? result == ScsParseResult::INVALID : result == ScsParseResult::NONE);
  }
  for (size_t index = 0; index < standard.size(); ++index) {
    const ScsParseResult result = assembler.push(standard.bytes[index], received);
    assert(index + 1 == standard.size() ? result == ScsParseResult::FRAME : result == ScsParseResult::NONE);
  }
  assert(received.is_valid() && received.type == ScsFrameType::STANDARD);

  assert(!ScsFrame::build(received, standard_payload, 3));
  return 0;
}
