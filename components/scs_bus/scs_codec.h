#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace scs_bus {

constexpr uint8_t SCS_FRAME_START = 0xA8;
constexpr uint8_t SCS_FRAME_END = 0xA3;
constexpr uint8_t SCS_ACK = 0xA5;
constexpr size_t SCS_STANDARD_FRAME_SIZE = 7;
constexpr size_t SCS_EXTENDED_FRAME_SIZE = 11;

enum class ScsFrameType : uint8_t {
  NONE,
  ACK,
  STANDARD,
  EXTENDED,
};

// A native SCS frame in wire order. ACK frames use only bytes[0].
struct ScsFrame {
  uint8_t bytes[SCS_EXTENDED_FRAME_SIZE]{};
  ScsFrameType type{ScsFrameType::NONE};

  size_t size() const;
  size_t payload_size() const;
  const uint8_t *payload() const;
  uint8_t checksum() const;
  bool is_ack() const;
  bool is_valid() const;

  static ScsFrame acknowledgment();
  static bool build(ScsFrame &frame, const uint8_t *payload, size_t payload_size);
};

// Packs the area and point nibbles used by common SCS automation devices.
constexpr uint8_t scs_pack_address(uint8_t area, uint8_t point) {
  return static_cast<uint8_t>((area << 4) | (point & 0x0F));
}

constexpr uint8_t scs_address_area(uint8_t address) { return address >> 4; }
constexpr uint8_t scs_address_point(uint8_t address) { return address & 0x0F; }

enum class ScsParseResult : uint8_t {
  NONE,
  FRAME,
  INVALID,
};

// Stateful byte-stream assembler. A non-terminator seventh byte is treated as
// an extended frame candidate, matching observed native parser behavior.
class ScsFrameAssembler {
 public:
  ScsParseResult push(uint8_t byte, ScsFrame &frame);
  void reset();

 private:
  void begin_frame();
  void resynchronize();

  uint8_t buffer_[SCS_EXTENDED_FRAME_SIZE]{};
  size_t size_{0};
};

}  // namespace scs_bus
}  // namespace esphome
