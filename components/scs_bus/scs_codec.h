#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace scs_bus {

constexpr uint8_t SCS_TELEGRAM_START = 0xA8;
constexpr uint8_t SCS_TELEGRAM_END = 0xA3;
constexpr uint8_t SCS_ACK = 0xA5;
constexpr size_t SCS_STANDARD_TELEGRAM_SIZE = 7;
constexpr size_t SCS_EXTENDED_TELEGRAM_SIZE = 11;

enum class ScsTelegramType : uint8_t {
  NONE,
  ACK,
  STANDARD,
  EXTENDED,
};

// A native SCS telegram in wire order. ACK telegrams use only bytes[0].
struct ScsTelegram {
  uint8_t bytes[SCS_EXTENDED_TELEGRAM_SIZE]{};
  ScsTelegramType type{ScsTelegramType::NONE};

  size_t size() const;
  size_t payload_size() const;
  const uint8_t *payload() const;
  uint8_t checksum() const;
  bool is_ack() const;
  bool is_valid() const;

  static ScsTelegram acknowledgment();
  static bool build(ScsTelegram &telegram, const uint8_t *payload, size_t payload_size);
};

// Packs the area and point nibbles used by common SCS automation devices.
constexpr uint8_t scs_pack_address(uint8_t area, uint8_t point) {
  return static_cast<uint8_t>((area << 4) | (point & 0x0F));
}

constexpr uint8_t scs_address_area(uint8_t address) { return address >> 4; }
constexpr uint8_t scs_address_point(uint8_t address) { return address & 0x0F; }

enum class ScsTelegramParseResult : uint8_t {
  NONE,
  TELEGRAM,
  INVALID,
};

// Stateful byte-stream assembler. The 300EOS receiver selects 11-byte telegrams
// from a D* or E* first payload byte; all other payloads use seven bytes.
class ScsTelegramAssembler {
 public:
  ScsTelegramParseResult push(uint8_t byte, ScsTelegram &telegram);
  void reset();

 private:
  void begin_telegram();
  void resynchronize();

  uint8_t buffer_[SCS_EXTENDED_TELEGRAM_SIZE]{};
  size_t size_{0};
  size_t expected_size_{0};
};

}  // namespace scs_bus
}  // namespace esphome
