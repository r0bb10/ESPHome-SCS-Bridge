#include "scsbticino_codec.h"

#include <algorithm>
namespace esphome::scs_bticino {

namespace {

static constexpr uint32_t ACTIVE_MIN_US = 20;
static constexpr uint32_t ACTIVE_MAX_US = 55;
static constexpr uint32_t TIMING_TOLERANCE_US = 26;
static constexpr uint32_t STREAM_BOUNDARY_US = 260;

uint8_t payload_xor(const uint8_t *payload, uint8_t length) {
  uint8_t result = 0;
  for (uint8_t index = 0; index < length; index++)
    result ^= payload[index];
  return result;
}

bool plausible_dominant(const ScsBticinoRun &run) {
  return !run.released && run.duration_us >= ACTIVE_MIN_US && run.duration_us <= ACTIVE_MAX_US;
}

void append_run(std::vector<ScsBticinoRun> *runs, bool released, uint32_t duration_us) {
  if (duration_us == 0)
    return;
  if (!runs->empty() && runs->back().released == released) {
    runs->back().duration_us += duration_us;
  } else {
    runs->push_back({released, duration_us});
  }
}

class RunCursor {
 public:
  RunCursor(const std::vector<ScsBticinoRun> &runs, size_t start) : runs_(runs), index_(start) {}

  bool level_at(uint32_t offset_us, bool *released, uint32_t *run_start_us, uint32_t *run_duration_us) {
    while (this->index_ < this->runs_.size() &&
           offset_us >= this->run_start_us_ + this->runs_[this->index_].duration_us) {
      this->run_start_us_ += this->runs_[this->index_].duration_us;
      this->index_++;
    }
    if (this->index_ == this->runs_.size())
      return false;
    *released = this->runs_[this->index_].released;
    *run_start_us = this->run_start_us_;
    *run_duration_us = this->runs_[this->index_].duration_us;
    return true;
  }

 private:
  const std::vector<ScsBticinoRun> &runs_;
  size_t index_;
  uint32_t run_start_us_{0};
};

bool decode_byte(const std::vector<ScsBticinoRun> &runs, size_t start, size_t *next, bool *message_end,
                 uint8_t *value) {
  if (start >= runs.size() || !plausible_dominant(runs[start]))
    return false;

  RunCursor cursor(runs, start);
  uint8_t byte = 0;
  for (uint8_t bit = 0; bit < 8; bit++) {
    bool released = false;
    uint32_t run_start_us = 0;
    uint32_t run_duration_us = 0;
    const uint32_t cell_start_us = (bit + 1) * SCS_CELL_US;
    if (!cursor.level_at(cell_start_us + SCS_DOMINANT_US / 2, &released, &run_start_us, &run_duration_us))
      return false;
    if (released) {
      byte |= static_cast<uint8_t>(1U << bit);
      continue;
    }
    if (run_duration_us < ACTIVE_MIN_US || run_duration_us > ACTIVE_MAX_US ||
        run_start_us + TIMING_TOLERANCE_US < cell_start_us ||
        run_start_us > cell_start_us + TIMING_TOLERANCE_US) {
      return false;
    }
  }

  bool stop_released = false;
  uint32_t ignored_start_us = 0;
  uint32_t ignored_duration_us = 0;
  if (!cursor.level_at(9 * SCS_CELL_US + SCS_DOMINANT_US / 2, &stop_released, &ignored_start_us,
                       &ignored_duration_us) ||
      !stop_released) {
    return false;
  }

  uint32_t elapsed_us = 0;
  for (size_t index = start; index < runs.size(); index++) {
    if (!runs[index].released && elapsed_us >= 10 * SCS_CELL_US - TIMING_TOLERANCE_US) {
      *next = index;
      *message_end = elapsed_us >= 10 * SCS_CELL_US + STREAM_BOUNDARY_US;
      *value = byte;
      return true;
    }
    elapsed_us += runs[index].duration_us;
  }

  *next = runs.size();
  *message_end = true;
  *value = byte;
  return true;
}

bool is_extended(uint8_t first_payload_byte) {
  const uint8_t family = first_payload_byte & 0xF0U;
  return family == 0xD0U || family == 0xE0U;
}

}  // namespace

bool ScsBticinoData::is_valid() const {
  if (this->is_ack())
    return true;
  if (this->length != SCS_STANDARD_SIZE && this->length != SCS_EXTENDED_SIZE)
    return false;
  return this->bytes[0] == SCS_START &&
         this->bytes[this->length - 2] == payload_xor(this->bytes.data() + 1, this->length - 3);
}

bool ScsBticinoData::is_transmittable() const {
  return this->is_ack() || (this->is_valid() && this->bytes[this->length - 1] == SCS_END);
}

uint8_t ScsBticinoData::get_size() const {
  return std::min(this->length, SCS_EXTENDED_SIZE);
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
  static constexpr char HEX[] = "0123456789ABCDEF";
  if (this->length == 0)
    return "Invalid";
  std::string result;
  result.reserve(this->get_size() * 3 - 1);
  for (uint8_t index = 0; index < this->get_size(); index++) {
    if (index != 0)
      result.push_back('.');
    result.push_back(HEX[this->bytes[index] >> 4]);
    result.push_back(HEX[this->bytes[index] & 0x0F]);
  }
  return result;
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

bool ScsBticinoCodec::encode(const ScsBticinoData &src, std::vector<ScsBticinoRun> *runs) {
  if (runs == nullptr || !src.is_transmittable())
    return false;

  runs->clear();
  runs->reserve(src.length * 19);
  for (uint8_t index = 0; index < src.length; index++) {
    append_run(runs, false, SCS_DOMINANT_US);
    append_run(runs, true, SCS_RELEASE_US);
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (src.bytes[index] & (1U << bit)) {
        append_run(runs, true, SCS_CELL_US);
      } else {
        append_run(runs, false, SCS_DOMINANT_US);
        append_run(runs, true, SCS_RELEASE_US);
      }
    }
    append_run(runs, true, SCS_CELL_US + (index + 1 == src.length ? 0 : SCS_INTER_BYTE_GAP_US));
  }
  return true;
}

bool ScsBticinoCodec::decode(const std::vector<ScsBticinoRun> &runs, ScsBticinoData *data) {
  if (data == nullptr)
    return false;

  for (size_t index = 0; index < runs.size(); index++) {
    if (!plausible_dominant(runs[index]))
      continue;

    size_t next = index;
    bool message_end = false;
    std::array<uint8_t, SCS_EXTENDED_SIZE> bytes{};
    if (!decode_byte(runs, index, &next, &message_end, &bytes[0]))
      continue;
    if (bytes[0] == SCS_ACK) {
      if (message_end) {
        *data = ScsBticinoData::acknowledgment();
        return true;
      }
      continue;
    }
    if (bytes[0] != SCS_START || message_end || !decode_byte(runs, next, &next, &message_end, &bytes[1]) ||
        message_end) {
      continue;
    }

    const uint8_t frame_size = is_extended(bytes[1]) ? SCS_EXTENDED_SIZE : SCS_STANDARD_SIZE;
    bool complete = true;
    for (uint8_t byte_index = 2; byte_index < frame_size; byte_index++) {
      if (!decode_byte(runs, next, &next, &message_end, &bytes[byte_index]) ||
          (byte_index + 1 < frame_size && message_end)) {
        complete = false;
        break;
      }
    }
    if (!complete)
      continue;

    ScsBticinoData candidate;
    if (ScsBticinoData::from_bytes(candidate, bytes.data(), frame_size)) {
      *data = candidate;
      return true;
    }
  }
  return false;
}

}  // namespace esphome::scs_bticino
