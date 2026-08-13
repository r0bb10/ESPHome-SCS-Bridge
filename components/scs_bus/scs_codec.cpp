#include "scs_codec.h"

namespace esphome {
namespace scs_bus {

namespace {

uint8_t xor_payload(const uint8_t *payload, size_t payload_size) {
  uint8_t checksum = 0;
  for (size_t index = 0; index < payload_size; ++index)
    checksum ^= payload[index];
  return checksum;
}

}  // namespace

size_t ScsFrame::size() const {
  switch (type) {
    case ScsFrameType::ACK:
      return 1;
    case ScsFrameType::STANDARD:
      return SCS_STANDARD_FRAME_SIZE;
    case ScsFrameType::EXTENDED:
      return SCS_EXTENDED_FRAME_SIZE;
    default:
      return 0;
  }
}

size_t ScsFrame::payload_size() const {
  switch (type) {
    case ScsFrameType::STANDARD:
      return 4;
    case ScsFrameType::EXTENDED:
      return 8;
    default:
      return 0;
  }
}

const uint8_t *ScsFrame::payload() const { return payload_size() == 0 ? nullptr : bytes + 1; }

uint8_t ScsFrame::checksum() const {
  const size_t data_size = payload_size();
  return data_size == 0 ? 0 : bytes[1 + data_size];
}

bool ScsFrame::is_ack() const { return type == ScsFrameType::ACK && bytes[0] == SCS_ACK; }

bool ScsFrame::is_valid() const {
  if (is_ack())
    return true;

  const size_t data_size = payload_size();
  return data_size != 0 && bytes[0] == SCS_FRAME_START && bytes[1 + data_size] == xor_payload(bytes + 1, data_size) &&
         bytes[2 + data_size] == SCS_FRAME_END;
}

ScsFrame ScsFrame::acknowledgment() {
  ScsFrame frame;
  frame.bytes[0] = SCS_ACK;
  frame.type = ScsFrameType::ACK;
  return frame;
}

bool ScsFrame::build(ScsFrame &frame, const uint8_t *payload, size_t payload_size) {
  if (payload == nullptr || (payload_size != 4 && payload_size != 8))
    return false;

  frame = ScsFrame{};
  frame.type = payload_size == 4 ? ScsFrameType::STANDARD : ScsFrameType::EXTENDED;
  frame.bytes[0] = SCS_FRAME_START;
  for (size_t index = 0; index < payload_size; ++index)
    frame.bytes[1 + index] = payload[index];
  frame.bytes[1 + payload_size] = xor_payload(payload, payload_size);
  frame.bytes[2 + payload_size] = SCS_FRAME_END;
  return true;
}

ScsParseResult ScsFrameAssembler::push(uint8_t byte, ScsFrame &frame) {
  if (size_ == 0) {
    if (byte == SCS_ACK) {
      frame = ScsFrame::acknowledgment();
      return ScsParseResult::FRAME;
    }
    if (byte == SCS_FRAME_START)
      begin_frame();
    return ScsParseResult::NONE;
  }

  // A delimiter always starts a fresh candidate after noise or a malformed frame.
  if (byte == SCS_FRAME_START) {
    begin_frame();
    return ScsParseResult::NONE;
  }

  buffer_[size_++] = byte;
  if (size_ == SCS_STANDARD_FRAME_SIZE && byte == SCS_FRAME_END) {
    frame.type = ScsFrameType::STANDARD;
    for (size_t index = 0; index < size_; ++index)
      frame.bytes[index] = buffer_[index];
    if (frame.is_valid()) {
      reset();
      return ScsParseResult::FRAME;
    }
    resynchronize();
    return ScsParseResult::INVALID;
  }

  // At seven bytes, any non-A3 byte is payload byte 5 of an extended frame.
  if (size_ < SCS_EXTENDED_FRAME_SIZE)
    return ScsParseResult::NONE;

  frame.type = ScsFrameType::EXTENDED;
  for (size_t index = 0; index < size_; ++index)
    frame.bytes[index] = buffer_[index];
  if (frame.is_valid()) {
    reset();
    return ScsParseResult::FRAME;
  }

  resynchronize();
  return ScsParseResult::INVALID;
}

void ScsFrameAssembler::reset() { size_ = 0; }

void ScsFrameAssembler::begin_frame() {
  buffer_[0] = SCS_FRAME_START;
  size_ = 1;
}

void ScsFrameAssembler::resynchronize() {
  size_t start = size_;
  for (size_t index = 1; index < size_; ++index) {
    if (buffer_[index] == SCS_FRAME_START)
      start = index;
  }

  if (start == size_) {
    reset();
    return;
  }

  const size_t remaining = size_ - start;
  for (size_t index = 0; index < remaining; ++index)
    buffer_[index] = buffer_[start + index];
  size_ = remaining;
}

}  // namespace scs_bus
}  // namespace esphome
