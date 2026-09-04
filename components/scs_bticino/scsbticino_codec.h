#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::scs_bticino {

static constexpr uint8_t SCS_START = 0xA8;
static constexpr uint8_t SCS_END = 0xA3;
static constexpr uint8_t SCS_ACK = 0xA5;
static constexpr uint8_t SCS_STANDARD_SIZE = 7;
static constexpr uint8_t SCS_EXTENDED_SIZE = 11;

// F461/MX SCS bit timing: a 104 us cell is a 35 us dominant pulse followed by
// 69 us released; non-final bytes add a measured 70 us released gap.
static constexpr uint32_t SCS_CELL_US = 104;
static constexpr uint32_t SCS_DOMINANT_US = 35;
static constexpr uint32_t SCS_RELEASE_US = SCS_CELL_US - SCS_DOMINANT_US;
static constexpr uint32_t SCS_INTER_BYTE_GAP_US = 70;

struct ScsBticinoRun {
  bool released;
  uint32_t duration_us;
};

struct ScsBticinoData {
  std::array<uint8_t, SCS_EXTENDED_SIZE> bytes{};
  uint8_t length{0};

  bool is_ack() const { return this->length == 1 && this->bytes[0] == SCS_ACK; }
  // Matches OEM RX: the final captured byte is intentionally not validated.
  bool is_valid() const;
  // TX is stricter: it only emits properly terminated locally built frames.
  bool is_transmittable() const;
  uint8_t get_size() const;
  std::vector<uint8_t> get_bytes() const;
  std::vector<uint8_t> get_payload() const;
  std::string to_string() const;

  static ScsBticinoData acknowledgment();
  static bool from_payload(ScsBticinoData &data, const uint8_t *payload, uint8_t payload_length);
  static bool from_bytes(ScsBticinoData &data, const uint8_t *bytes, uint8_t length);
};

class ScsBticinoCodec {
 public:
  static bool encode(const ScsBticinoData &src, std::vector<ScsBticinoRun> *runs);
  static bool decode(const std::vector<ScsBticinoRun> &runs, ScsBticinoData *data);
};

}  // namespace esphome::scs_bticino
