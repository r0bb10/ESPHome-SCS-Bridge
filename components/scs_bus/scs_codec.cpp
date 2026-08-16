#include "scs_codec.h"

#include <cstring>

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

size_t ScsTelegram::size() const {
  switch (type) {
    case ScsTelegramType::ACK:
      return 1;
    case ScsTelegramType::STANDARD:
      return SCS_STANDARD_TELEGRAM_SIZE;
    case ScsTelegramType::EXTENDED:
      return SCS_EXTENDED_TELEGRAM_SIZE;
    default:
      return 0;
  }
}

size_t ScsTelegram::payload_size() const {
  switch (type) {
    case ScsTelegramType::STANDARD:
      return 4;
    case ScsTelegramType::EXTENDED:
      return 8;
    default:
      return 0;
  }
}

const uint8_t *ScsTelegram::payload() const { return payload_size() == 0 ? nullptr : bytes + 1; }

uint8_t ScsTelegram::checksum() const {
  const size_t data_size = payload_size();
  return data_size == 0 ? 0 : bytes[1 + data_size];
}

bool ScsTelegram::is_ack() const { return type == ScsTelegramType::ACK && bytes[0] == SCS_ACK; }

bool ScsTelegram::is_valid() const {
  if (is_ack())
    return true;

  const size_t data_size = payload_size();
  // The 300EOS RX path checks the start marker and XOR only. Its transmit
  // path always emits A3, but rx_end does not reject a different final byte.
  return data_size != 0 && bytes[0] == SCS_TELEGRAM_START && bytes[1 + data_size] == xor_payload(bytes + 1, data_size);
}

ScsTelegram ScsTelegram::acknowledgment() {
  ScsTelegram telegram;
  telegram.bytes[0] = SCS_ACK;
  telegram.type = ScsTelegramType::ACK;
  return telegram;
}

bool ScsTelegram::build(ScsTelegram &telegram, const uint8_t *payload, size_t payload_size) {
  if (payload == nullptr || (payload_size != 4 && payload_size != 8))
    return false;

  telegram = ScsTelegram{};
  telegram.type = payload_size == 4 ? ScsTelegramType::STANDARD : ScsTelegramType::EXTENDED;
  telegram.bytes[0] = SCS_TELEGRAM_START;
  for (size_t index = 0; index < payload_size; ++index)
    telegram.bytes[1 + index] = payload[index];
  telegram.bytes[1 + payload_size] = xor_payload(payload, payload_size);
  telegram.bytes[2 + payload_size] = SCS_TELEGRAM_END;
  return true;
}

ScsTelegramParseResult ScsTelegramAssembler::push(uint8_t byte, ScsTelegram &telegram) {
  if (size_ == 0) {
    if (byte == SCS_ACK) {
      telegram = ScsTelegram::acknowledgment();
      return ScsTelegramParseResult::TELEGRAM;
    }
    if (byte == SCS_TELEGRAM_START)
      begin_telegram();
    return ScsTelegramParseResult::NONE;
  }

  // A delimiter always starts a fresh candidate after noise or a malformed telegram.
  if (byte == SCS_TELEGRAM_START) {
    begin_telegram();
    return ScsTelegramParseResult::NONE;
  }

  buffer_[size_++] = byte;
  if (size_ == 2)
    expected_size_ = (buffer_[1] & 0xF0U) == 0xD0U || (buffer_[1] & 0xF0U) == 0xE0U ?
                          SCS_EXTENDED_TELEGRAM_SIZE : SCS_STANDARD_TELEGRAM_SIZE;
  if (size_ < expected_size_)
    return ScsTelegramParseResult::NONE;

  telegram.type = expected_size_ == SCS_EXTENDED_TELEGRAM_SIZE ? ScsTelegramType::EXTENDED : ScsTelegramType::STANDARD;
  for (size_t index = 0; index < size_; ++index)
    telegram.bytes[index] = buffer_[index];
  if (telegram.is_valid()) {
    reset();
    return ScsTelegramParseResult::TELEGRAM;
  }

  resynchronize();
  return ScsTelegramParseResult::INVALID;
}

void ScsTelegramAssembler::reset() {
  size_ = 0;
  expected_size_ = 0;
}

void ScsTelegramAssembler::begin_telegram() {
  buffer_[0] = SCS_TELEGRAM_START;
  size_ = 1;
  expected_size_ = 0;
}

void ScsTelegramAssembler::resynchronize() {
  size_t start = size_;
  for (size_t index = 1; index < size_; ++index) {
    if (buffer_[index] == SCS_TELEGRAM_START)
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
  expected_size_ = size_ >= 2 && ((buffer_[1] & 0xF0U) == 0xD0U || (buffer_[1] & 0xF0U) == 0xE0U) ?
                        SCS_EXTENDED_TELEGRAM_SIZE : size_ >= 2 ? SCS_STANDARD_TELEGRAM_SIZE : 0;
}

bool ScsTelegramDeduplicator::is_duplicate(const ScsTelegram &telegram, uint32_t now_us) {
  if (telegram.is_ack())
    return false;

  if (has_last_ && last_.type == telegram.type &&
      memcmp(last_.bytes, telegram.bytes, telegram.size()) == 0) {
    const int32_t age_us = static_cast<int32_t>(now_us - last_seen_us_);
    if (age_us >= 0 && static_cast<uint32_t>(age_us) < window_us_) {
      last_seen_us_ = now_us;
      suppressed_++;
      return true;
    }
  }

  has_last_ = true;
  last_ = telegram;
  last_seen_us_ = now_us;
  return false;
}

}  // namespace scs_bus
}  // namespace esphome
