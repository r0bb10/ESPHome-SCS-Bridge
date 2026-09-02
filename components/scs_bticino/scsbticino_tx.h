#pragma once

#include "scsbticino_codec.h"

#include <array>
#include <cstdint>

namespace esphome::scs_bticino {

enum class ScsTxType : uint8_t { RESPONSE = 0, SHORT = 1, EXTENDED = 2, EXTENDED_ALT = 3 };
enum class ScsTxResult : uint8_t { SUCCESS = 0, RESPONSE_TIMEOUT = 2, COLLISION_LIMIT = 3 };
enum class ScsTxState : uint8_t { IDLE, WAIT_ACCESS, START, BYTE, STOP, END, WAIT_RESPONSE };

struct ScsTxStep {
  uint32_t delay_us;
  bool drive_dominant;
  bool check_released;
};

// The timer backend calls advance() at every returned delay. A collision is
// reported only when a scheduled released checkpoint observes dominant RX.
class ScsBticinoTx {
 public:
  bool enqueue(const ScsBticinoData &frame, ScsTxType type);
  bool advance(bool rx_dominant, ScsTxStep *step, ScsTxResult *result);
  bool complete_response(ScsTxResult *result);
  bool active() const { return this->queued_; }
  ScsTxState state() const { return this->state_; }

 protected:
  bool collision_(ScsTxResult *result);
  uint32_t access_delay_();

  ScsBticinoData frame_{};
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
};

}  // namespace esphome::scs_bticino
