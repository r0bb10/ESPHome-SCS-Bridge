#include "scsbticino_tx.h"

namespace esphome::scs_bticino {

bool ScsBticinoTx::enqueue(const ScsBticinoData &frame, ScsTxType type) {
  const bool extended = frame.length == SCS_EXTENDED_SIZE;
  if (this->queued_ || !frame.is_transmittable() ||
      (type == ScsTxType::EXTENDED || type == ScsTxType::EXTENDED_ALT ? !extended : extended))
    return false;
  this->frame_ = frame;
  this->type_ = type;
  this->state_ = ScsTxState::IDLE;
  this->collisions_ = 0;
  this->attempts_ = 0;
  this->release_pending_ = false;
  this->expect_release_ = false;
  this->queued_ = true;
  return true;
}

bool ScsBticinoTx::complete_response(ScsTxResult *result) {
  if (!this->queued_ || this->state_ != ScsTxState::WAIT_RESPONSE || result == nullptr)
    return false;
  this->queued_ = false;
  *result = ScsTxResult::SUCCESS;
  return true;
}

uint32_t ScsBticinoTx::access_delay_() {
  this->random_ = (0x41C64E6DU * this->random_ + 0x3039U) & 0x7FFFFFFFU;
  return (150U + 3U * (this->random_ >> 23)) * SCS_CELL_US;
}

bool ScsBticinoTx::collision_(ScsTxResult *result) {
  this->collisions_++;
  if (this->collisions_ > 0xFF) {
    this->queued_ = false;
    *result = ScsTxResult::COLLISION_LIMIT;
    return false;
  }
  this->attempts_ = 0;
  this->state_ = ScsTxState::IDLE;
  return true;
}

bool ScsBticinoTx::advance(bool rx_dominant, ScsTxStep *step, ScsTxResult *result) {
  if (!this->queued_ || step == nullptr || result == nullptr)
    return false;
  *result = ScsTxResult::SUCCESS;
  if (this->expect_release_ && rx_dominant) {
    return this->collision_(result);
  }
  const auto emit = [this, step](uint32_t delay_us, bool drive_dominant, bool check_released) {
    *step = {delay_us, drive_dominant, check_released};
    this->expect_release_ = check_released;
  };
  if (this->release_pending_) {
    this->release_pending_ = false;
    emit(SCS_RELEASE_US, false, true);
    return true;
  }
  if (this->state_ == ScsTxState::IDLE) {
    this->attempts_++;
    this->byte_index_ = 0;
    this->bit_index_ = 0;
    this->state_ = ScsTxState::WAIT_ACCESS;
    emit(this->attempts_ == 1 || this->type_ == ScsTxType::RESPONSE ? this->access_delay_() : 84 * SCS_CELL_US,
         false, true);
    return true;
  }
  if (this->state_ == ScsTxState::WAIT_ACCESS) {
    this->state_ = ScsTxState::START;
    emit(SCS_DOMINANT_US, true, false);
    return true;
  }
  if (this->state_ == ScsTxState::START) {
    this->state_ = ScsTxState::BYTE;
    emit(SCS_RELEASE_US, false, true);
    return true;
  }
  if (this->state_ == ScsTxState::BYTE) {
    const bool one = this->frame_.bytes[this->byte_index_] & (1U << this->bit_index_);
    this->bit_index_++;
    if (this->bit_index_ == 8) {
      this->bit_index_ = 0;
      this->state_ = ScsTxState::STOP;
    }
    this->release_pending_ = !one;
    emit(one ? SCS_CELL_US : SCS_DOMINANT_US, !one, one);
    return true;
  }
  if (this->state_ == ScsTxState::STOP) {
    this->byte_index_++;
    if (this->byte_index_ == this->frame_.length) {
      this->state_ = ScsTxState::END;
    } else {
      this->state_ = ScsTxState::BYTE;
    }
    emit(SCS_CELL_US + (this->state_ == ScsTxState::BYTE ? SCS_INTER_BYTE_GAP_US : 0), false, true);
    return true;
  }
  if (this->state_ == ScsTxState::END) {
    if (this->type_ == ScsTxType::RESPONSE) {
      this->state_ = ScsTxState::WAIT_RESPONSE;
      emit(82 * SCS_CELL_US, false, true);
      return true;
    }
    if (this->attempts_ < 3) {
      this->state_ = ScsTxState::IDLE;
      emit(84 * SCS_CELL_US, false, true);
      return true;
    }
    this->queued_ = false;
    *result = ScsTxResult::SUCCESS;
    return false;
  }
  if (this->state_ == ScsTxState::WAIT_RESPONSE) {
    this->queued_ = false;
    *result = ScsTxResult::RESPONSE_TIMEOUT;
  }
  return false;
}

}  // namespace esphome::scs_bticino
