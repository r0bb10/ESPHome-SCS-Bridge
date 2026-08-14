#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::scs_bus {

class ScsSerialReceiver {
 public:
  // ESP32-C3 GPTimer requires an APB-clock divider of at least two, making
  // 40 MHz its maximum valid shared timing base. These are the nearest ticks
  // for the 300EOS M4's 34.67 us / 104.01 us timing.
  static constexpr uint32_t CLOCK_HZ = 40000000;
  static constexpr uint16_t ACTIVE_TICKS = 1387;
  static constexpr uint16_t CELL_TICKS = 4160;
  static constexpr uint16_t SAMPLE_TICKS = ACTIVE_TICKS / 2;
  static constexpr uint16_t SYNC_TICKS = 914;

  enum class State : uint8_t { IDLE, SYNC, DATA, STOP };
  enum class SampleResult : uint8_t { NONE, BYTE, INVALID };

  bool start() {
    if (state_ != State::IDLE)
      return false;
    state_ = State::SYNC;
    bit_index_ = 0;
    byte_ = 0;
    return true;
  }

  SampleResult sample(uint8_t level, uint8_t *byte) {
    switch (state_) {
      case State::SYNC:
        if (level != 0) {
          state_ = State::IDLE;
          return SampleResult::INVALID;
        }
        state_ = State::DATA;
        return SampleResult::NONE;
      case State::DATA:
        byte_ |= level << bit_index_;
        bit_index_++;
        if (bit_index_ == 8)
          state_ = State::STOP;
        return SampleResult::NONE;
      case State::STOP:
        state_ = State::IDLE;
        if (level == 0)
          return SampleResult::INVALID;
        *byte = byte_;
        return SampleResult::BYTE;
      case State::IDLE:
        return SampleResult::INVALID;
    }
    return SampleResult::INVALID;
  }

  State state() const { return state_; }
  bool receiving() const { return state_ != State::IDLE; }
  uint8_t bit_index() const { return bit_index_; }
  uint16_t next_sample_delay_ticks() const {
    return state_ == State::SYNC ? SYNC_TICKS :
           state_ == State::DATA ? CELL_TICKS + SAMPLE_TICKS - SYNC_TICKS : CELL_TICKS;
  }

 private:
  State state_{State::IDLE};
  uint8_t bit_index_{0};
  uint8_t byte_{0};
};

struct ScsSerialRun {
  bool level;
  uint16_t duration_ticks;
};

constexpr size_t SCS_SERIAL_MAX_RUNS = 19;
size_t scs_encode_serial_byte(uint8_t byte, ScsSerialRun *runs, size_t capacity);

}  // namespace esphome::scs_bus
