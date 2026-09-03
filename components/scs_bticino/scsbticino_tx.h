#pragma once

#include "scsbticino_codec.h"

#include <array>
#include <cstdint>

#ifdef USE_ESP32
#include "esp_attr.h"
#define SCS_BTICINO_IRAM_ATTR IRAM_ATTR
#else
#define SCS_BTICINO_IRAM_ATTR
#endif

namespace esphome::scs_bticino {

enum class ScsTxType : uint8_t { RESPONSE = 0, SHORT = 1, EXTENDED = 2, EXTENDED_ALT = 3 };
enum class ScsTxResult : uint8_t { SUCCESS = 0, RESPONSE_TIMEOUT = 2, COLLISION_LIMIT = 3 };
enum class ScsTxState : uint8_t { IDLE, WAIT_ACCESS, START, BYTE, STOP, INTER_BYTE, END, WAIT_RESPONSE };

struct ScsTxStep {
  uint32_t delay_us;
  bool drive_dominant;
};

// The timer backend calls advance() at every returned delay. A collision is
// reported only when a scheduled released checkpoint observes dominant RX.
class ScsBticinoTx {
 public:
  bool enqueue(const ScsBticinoData &frame, ScsTxType type);
  bool start_next();
  bool start_ack();
  void confirm_started();
  bool SCS_BTICINO_IRAM_ATTR advance(bool rx_dominant, ScsTxStep *step, ScsTxResult *result);
  void SCS_BTICINO_IRAM_ATTR cancel();
  bool complete_response(ScsTxResult *result);
  bool active() const { return this->queued_; }
  bool pending() const { return this->queue_read_ != this->queue_write_; }
  bool local_ack() const { return this->local_ack_; }
  bool ready(bool bus_busy) const { return !this->queued_ && this->pending() && !bus_busy; }
  uint8_t queue_depth() const { return (this->queue_write_ + QUEUE_SLOTS - this->queue_read_) % QUEUE_SLOTS; }
  const ScsBticinoData &frame() const { return this->frame_; }
  ScsTxType type() const { return this->type_; }
  uint8_t attempts() const { return this->attempts_; }
  uint16_t collisions() const { return this->collisions_; }
  bool checking_released() const { return this->expect_release_; }
  bool awaiting_access() const { return this->state_ == ScsTxState::WAIT_ACCESS; }
  ScsTxState state() const { return this->state_; }

 protected:
  bool SCS_BTICINO_IRAM_ATTR collision_(ScsTxResult *result);
  uint32_t SCS_BTICINO_IRAM_ATTR access_delay_();

  struct QueueEntry {
    std::array<uint8_t, 8> payload{};
    uint8_t length{0};
    ScsTxType type{ScsTxType::SHORT};
    uint8_t bus{0};
  };

  static constexpr uint8_t QUEUE_SLOTS = 32;
  static constexpr uint8_t RETRY_LIMITS[] = {8, 3, 3, 3};

  ScsBticinoData frame_{};
  std::array<QueueEntry, QUEUE_SLOTS> queue_{};
  ScsTxType type_{ScsTxType::SHORT};
  ScsTxState state_{ScsTxState::IDLE};
  uint32_t random_{1};
  uint16_t collisions_{0};
  uint8_t attempts_{0};
  uint8_t byte_index_{0};
  uint8_t bit_index_{0};
  bool release_pending_{false};
  bool expect_release_{false};
  bool queued_{false};
  bool local_ack_{false};
  uint8_t queue_read_{0};
  uint8_t queue_write_{0};
};

}  // namespace esphome::scs_bticino

#undef SCS_BTICINO_IRAM_ATTR
