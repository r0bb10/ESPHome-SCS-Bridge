#include "scsbticino_tx.h"

namespace esphome::scs_bticino {

namespace {

bool is_valid_tx_type(ScsTxType type) {
  switch (type) {
    case ScsTxType::RESPONSE:
    case ScsTxType::SHORT:
    case ScsTxType::EXTENDED:
    case ScsTxType::EXTENDED_ALT:
      return true;
  }
  return false;
}

}  // namespace

bool ScsBticinoTx::enqueue(const ScsBticinoData &frame, ScsTxType type) {
  const bool extended = frame.length == SCS_EXTENDED_SIZE;
  if (!is_valid_tx_type(type) || !frame.is_transmittable() ||
      (type == ScsTxType::EXTENDED || type == ScsTxType::EXTENDED_ALT ? !extended : extended))
    return false;

  const uint8_t next = (this->queue_write_ + 1) % QUEUE_SLOTS;
  if (next == this->queue_read_)
    return false;
  auto &entry = this->queue_[this->queue_write_];
  entry.length = frame.length - 3;
  entry.type = type;
  for (uint8_t index = 0; index < entry.length; index++)
    entry.payload[index] = frame.bytes[index + 1];
  this->queue_write_ = next;
  return true;
}

bool ScsBticinoTx::start_next() {
  if (this->queued_ || !this->pending())
    return false;
  const auto &entry = this->queue_[this->queue_read_];
  if (!ScsBticinoData::from_payload(this->frame_, entry.payload.data(), entry.length))
    return false;
  this->type_ = entry.type;
  this->state_ = ScsTxState::IDLE;
  this->collisions_ = 0;
  this->attempts_ = 0;
  this->release_pending_ = false;
  this->expect_release_ = false;
  this->queued_ = true;
  this->local_ack_ = false;
  return true;
}

bool ScsBticinoTx::start_ack() {
  if (this->queued_)
    return false;
  this->frame_ = ScsBticinoData::acknowledgment();
  this->state_ = ScsTxState::IDLE;
  this->collisions_ = 0;
  this->attempts_ = 0;
  this->release_pending_ = false;
  this->expect_release_ = false;
  this->queued_ = true;
  this->local_ack_ = true;
  return true;
}

void ScsBticinoTx::confirm_started() { this->queue_read_ = (this->queue_read_ + 1) % QUEUE_SLOTS; }

void ScsBticinoTx::cancel() {
  this->state_ = ScsTxState::IDLE;
  this->release_pending_ = false;
  this->expect_release_ = false;
  this->queued_ = false;
  this->local_ack_ = false;
}

bool ScsBticinoTx::complete_response(ScsTxResult *result) {
  if (!this->queued_ || this->state_ != ScsTxState::WAIT_RESPONSE || result == nullptr)
    return false;
  this->queued_ = false;
  this->local_ack_ = false;
  *result = ScsTxResult::SUCCESS;
  return true;
}

uint32_t ScsBticinoTx::access_delay_() {
  // F461 tx_idle (0x1ffe3a20): random access is 150 + 3 * (LCG >> 23) cells.
  this->random_ = (0x41C64E6DU * this->random_ + 0x3039U) & 0x7FFFFFFFU;
  return (150U + 3U * (this->random_ >> 23)) * SCS_CELL_US;
}

bool ScsBticinoTx::collision_(ScsTxResult *result) {
  this->collisions_++;
  // F461 collision_management (0x1ffe38e1) keeps the frame pending through
  // 255 collisions and returns OEM result 3 on collision 256.
  if (this->collisions_ > 0xFF) {
    this->queued_ = false;
    *result = ScsTxResult::COLLISION_LIMIT;
    return false;
  }
  this->attempts_ = 0;
  this->state_ = ScsTxState::IDLE;
  this->release_pending_ = false;
  this->expect_release_ = false;
  return true;
}

bool ScsBticinoTx::advance(bool rx_dominant, ScsTxStep *step, ScsTxResult *result) {
  if (!this->queued_ || step == nullptr || result == nullptr)
    return false;
  *result = ScsTxResult::SUCCESS;
  // F461 collision_management only loses arbitration when RX is dominant at a
  // local released checkpoint; a missing dominant self-echo is not a collision.
  if (this->expect_release_ && rx_dominant) {
    if (!this->collision_(result))
      return false;
  }
  const auto emit = [this, step](uint32_t delay_us, bool drive_dominant, bool check_released) {
    *step = {delay_us, drive_dominant};
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
    // F461 tx_idle: later type 1/2/3 transmissions use an 84-cell gap;
    // type 0 always restarts with random access.
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
      this->state_ = ScsTxState::INTER_BYTE;
    }
    emit(SCS_CELL_US + (this->state_ == ScsTxState::INTER_BYTE ? SCS_INTER_BYTE_GAP_US : 0), false, true);
    return true;
  }
  if (this->state_ == ScsTxState::INTER_BYTE) {
    this->state_ = ScsTxState::START;
    emit(SCS_DOMINANT_US, true, false);
    return true;
  }
  if (this->state_ == ScsTxState::END) {
    if (this->local_ack_) {
      this->queued_ = false;
      this->local_ack_ = false;
      *result = ScsTxResult::SUCCESS;
      return false;
    }
    if (this->type_ == ScsTxType::RESPONSE) {
      this->state_ = ScsTxState::WAIT_RESPONSE;
      // F461 tx_wait_for_ack (0x1ffe3bbe) waits 82 SCS cells for raw A5.
      emit(82 * SCS_CELL_US, false, false);
      return true;
    }
    if (this->attempts_ < 3) {
      this->attempts_++;
      this->byte_index_ = 0;
      this->bit_index_ = 0;
      this->state_ = ScsTxState::WAIT_ACCESS;
      emit(84 * SCS_CELL_US, false, true);
      return true;
    }
    this->queued_ = false;
    *result = ScsTxResult::SUCCESS;
    return false;
  }
  if (this->state_ == ScsTxState::WAIT_RESPONSE) {
    if (this->attempts_ < RETRY_LIMITS[static_cast<uint8_t>(this->type_)]) {
      this->byte_index_ = 0;
      this->bit_index_ = 0;
      this->state_ = ScsTxState::IDLE;
      return this->advance(false, step, result);
    }
    this->queued_ = false;
    *result = ScsTxResult::RESPONSE_TIMEOUT;
  }
  return false;
}

}  // namespace esphome::scs_bticino
