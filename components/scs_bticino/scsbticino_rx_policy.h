#pragma once

#include "scsbticino_codec.h"

namespace esphome::scs_bticino {

struct ScsBticinoIdentity {
  uint8_t bus{0};
  uint16_t address{0};
  uint8_t system{0};
};

enum class ScsBticinoRxAction : uint8_t { NONE, COMPLETE_RESPONSE, QUEUE_LOCAL_ACK };

// Keep RX policy independent of ESP-IDF so the controller's A5 decisions are
// tested on the host. The system-0 OEM boot default never matches a frame.
class ScsBticinoRxPolicy {
 public:
  explicit ScsBticinoRxPolicy(ScsBticinoIdentity identity = {}) : identity_(identity) {}

  bool is_locally_addressed(const ScsBticinoData &frame) const {
    const uint8_t system = this->identity_.system & 0x0F;
    if (!frame.is_valid() || system == 0 || (frame.bytes[3] >> 4) != system)
      return false;
    if (system == 1 || system == 4)
      return this->identity_.address == frame.bytes[1];
    return (frame.bytes[1] & 0xF0) == 0x80 && (this->identity_.address & 0x0FFF) ==
                                                 ((frame.bytes[3] & 0x0F) << 8 | frame.bytes[2]);
  }

  ScsBticinoRxAction action_for(const ScsBticinoData &frame, bool waiting_for_response) const {
    if (frame.is_ack() && waiting_for_response)
      return ScsBticinoRxAction::COMPLETE_RESPONSE;
    if (this->is_locally_addressed(frame))
      return ScsBticinoRxAction::QUEUE_LOCAL_ACK;
    return ScsBticinoRxAction::NONE;
  }

 protected:
  ScsBticinoIdentity identity_;
};

}  // namespace esphome::scs_bticino
