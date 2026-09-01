#include "scsbticino_protocol.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>

namespace esphome::scs_bticino {

namespace {

static const char *const TAG = "remote.scs_bticino";

static constexpr uint32_t CELL_US = 104;
static constexpr uint32_t DOMINANT_US = 35;
static constexpr uint32_t RELEASE_US = CELL_US - DOMINANT_US;
static constexpr uint32_t INTER_BYTE_GAP_US = 70;
static constexpr uint32_t ACTIVE_MIN_US = 20;
static constexpr uint32_t ACTIVE_MAX_US = 55;
static constexpr uint32_t TIMING_TOLERANCE_US = 26;
static constexpr uint32_t STREAM_BOUNDARY_US = 260;
static constexpr uint8_t MAX_ENCODED_RUNS_PER_BYTE = 19;

struct Run {
  bool released;
  uint32_t duration_us;
};

uint8_t payload_xor(const uint8_t *payload, uint8_t length) {
  uint8_t result = 0;
  for (uint8_t index = 0; index < length; index++)
    result ^= payload[index];
  return result;
}

bool plausible_dominant(const Run &run) {
  return !run.released && run.duration_us >= ACTIVE_MIN_US && run.duration_us <= ACTIVE_MAX_US;
}

bool level_at(const std::vector<Run> &runs, size_t start, uint32_t offset_us, bool *released) {
  uint32_t elapsed_us = 0;
  for (size_t index = start; index < runs.size(); index++) {
    if (offset_us < elapsed_us + runs[index].duration_us) {
      *released = runs[index].released;
      return true;
    }
    elapsed_us += runs[index].duration_us;
  }
  return false;
}

bool decode_byte(const std::vector<Run> &runs, size_t start, size_t *next, bool *message_end, uint8_t *value) {
  if (start >= runs.size() || !plausible_dominant(runs[start]))
    return false;

  uint8_t byte = 0;
  for (uint8_t bit = 0; bit < 8; bit++) {
    bool released = false;
    // Sample at the middle of the dominant pulse. A release run can span a
    // logical one and the release portion of a later zero, so it is not a
    // one-to-one representation of cells.
    if (!level_at(runs, start, (bit + 1) * CELL_US + DOMINANT_US / 2, &released))
      return false;
    if (released)
      byte |= static_cast<uint8_t>(1U << bit);
  }

  bool stop_released = false;
  if (!level_at(runs, start, 9 * CELL_US + DOMINANT_US / 2, &stop_released) || !stop_released)
    return false;

  *value = byte;
  uint32_t elapsed_us = 0;
  for (size_t index = start; index < runs.size(); index++) {
    if (!runs[index].released && elapsed_us >= 10 * CELL_US - TIMING_TOLERANCE_US) {
      *next = index;
      *message_end = elapsed_us >= 10 * CELL_US + STREAM_BOUNDARY_US;
      return true;
    }
    elapsed_us += runs[index].duration_us;
  }

  *next = runs.size();
  *message_end = true;
  return true;
}

}  // namespace

bool ScsBticinoData::is_valid() const {
  if (this->is_ack())
    return true;
  if (this->length != SCS_STANDARD_SIZE && this->length != SCS_EXTENDED_SIZE)
    return false;
  if (this->bytes[0] != SCS_START)
    return false;
  return this->bytes[this->length - 2] == payload_xor(this->bytes.data() + 1, this->length - 3);
}

std::vector<uint8_t> ScsBticinoData::get_bytes() const {
  return std::vector<uint8_t>(this->bytes.begin(), this->bytes.begin() + this->get_size());
}

std::vector<uint8_t> ScsBticinoData::get_payload() const {
  if (this->length != SCS_STANDARD_SIZE && this->length != SCS_EXTENDED_SIZE)
    return {};
  return std::vector<uint8_t>(this->bytes.begin() + 1, this->bytes.begin() + this->length - 2);
}

std::string ScsBticinoData::to_string() const {
  if (this->length == 0)
    return "Invalid";
  return format_hex_pretty(this->bytes.data(), this->get_size(), '.');
}

ScsBticinoData ScsBticinoData::acknowledgment() {
  ScsBticinoData data;
  data.bytes[0] = SCS_ACK;
  data.length = 1;
  return data;
}

bool ScsBticinoData::from_payload(ScsBticinoData &data, const uint8_t *payload, uint8_t payload_length) {
  if (payload == nullptr || (payload_length != 4 && payload_length != 8))
    return false;
  data = ScsBticinoData{};
  data.length = payload_length == 4 ? SCS_STANDARD_SIZE : SCS_EXTENDED_SIZE;
  data.bytes[0] = SCS_START;
  std::copy_n(payload, payload_length, data.bytes.begin() + 1);
  data.bytes[data.length - 2] = payload_xor(payload, payload_length);
  data.bytes[data.length - 1] = SCS_END;
  return true;
}

bool ScsBticinoData::from_bytes(ScsBticinoData &data, const uint8_t *bytes, uint8_t length) {
  if (bytes == nullptr || length > SCS_EXTENDED_SIZE)
    return false;
  data = ScsBticinoData{};
  std::copy_n(bytes, length, data.bytes.begin());
  data.length = length;
  return data.is_valid();
}

void ScsBticinoProtocol::encode_byte_(remote_base::RemoteTransmitData *dst, uint8_t byte, bool final_byte) const {
  dst->mark(DOMINANT_US);  // Start bit.
  dst->space(RELEASE_US);
  for (uint8_t bit = 0; bit < 8; bit++) {
    if (byte & (1U << bit)) {
      dst->space(CELL_US);
    } else {
      dst->mark(DOMINANT_US);
      dst->space(RELEASE_US);
    }
  }
  dst->space(CELL_US + (final_byte ? 0 : INTER_BYTE_GAP_US));
}

void ScsBticinoProtocol::encode(remote_base::RemoteTransmitData *dst, const ScsBticinoData &src) {
  if (!src.is_valid()) {
    ESP_LOGW(TAG, "Refusing invalid SCS frame");
    return;
  }
  dst->set_carrier_frequency(0);
  dst->reserve(src.length * MAX_ENCODED_RUNS_PER_BYTE);
  for (uint8_t index = 0; index < src.length; index++)
    this->encode_byte_(dst, src.bytes[index], index + 1 == src.length);
  ESP_LOGD(TAG, "Transmitting: %s", src.to_string().c_str());
}

optional<ScsBticinoData> ScsBticinoProtocol::decode(remote_base::RemoteReceiveData src) {
  std::vector<Run> runs;
  runs.reserve(src.get_raw_data().size());
  for (const int32_t timing : src.get_raw_data()) {
    if (timing == 0)
      continue;
    const bool released = timing > 0;
    const uint32_t duration = timing < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(timing))
                                         : static_cast<uint32_t>(timing);
    if (!runs.empty() && runs.back().released == released) {
      runs.back().duration_us += duration;
    } else {
      runs.push_back({released, duration});
    }
  }

  // A corrupt candidate must not hide a later complete telegram in the capture.
  for (size_t index = 0; index < runs.size(); index++) {
    if (!plausible_dominant(runs[index])) {
      continue;
    }

    size_t next = index;
    bool message_end = false;
    std::array<uint8_t, SCS_EXTENDED_SIZE> bytes{};
    if (!decode_byte(runs, index, &next, &message_end, &bytes[0]))
      continue;

    // An ACK is a complete one-byte telegram, not a byte within another frame.
    if (bytes[0] == SCS_ACK) {
      if (message_end)
        return ScsBticinoData::acknowledgment();
      continue;
    }
    if (bytes[0] != SCS_START || message_end)
      continue;

    if (!decode_byte(runs, next, &next, &message_end, &bytes[1]) || message_end)
      continue;
    // Modern M4 frames use D*/E* extensions; validate a seven-byte frame first
    // so legacy standard frames with the same leading byte still decode.
    const bool extended_candidate = (bytes[1] & 0xF0U) == 0xD0U || (bytes[1] & 0xF0U) == 0xE0U;
    bool complete = true;
    for (uint8_t byte_index = 2; byte_index < SCS_STANDARD_SIZE; byte_index++) {
      if (!decode_byte(runs, next, &next, &message_end, &bytes[byte_index]) ||
          (byte_index + 1 < SCS_STANDARD_SIZE && message_end)) {
        complete = false;
        break;
      }
    }
    if (!complete)
      continue;

    ScsBticinoData data;
    if (ScsBticinoData::from_bytes(data, bytes.data(), SCS_STANDARD_SIZE))
      return data;
    if (!extended_candidate || message_end)
      continue;

    for (uint8_t byte_index = SCS_STANDARD_SIZE; byte_index < SCS_EXTENDED_SIZE; byte_index++) {
      if (!decode_byte(runs, next, &next, &message_end, &bytes[byte_index]) ||
          (byte_index + 1 < SCS_EXTENDED_SIZE && message_end)) {
        complete = false;
        break;
      }
    }
    if (complete && ScsBticinoData::from_bytes(data, bytes.data(), SCS_EXTENDED_SIZE))
      return data;
  }
  return {};
}

void ScsBticinoProtocol::dump(const ScsBticinoData &data) {
  ESP_LOGI(TAG, "Received: %s", data.to_string().c_str());
}

}  // namespace esphome::scs_bticino
