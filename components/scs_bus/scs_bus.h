#pragma once

#include <functional>
#include <vector>

#include "esphome/core/component.h"
#include "scs_codec.h"
#include "scs_own.h"
#include "scs_rmt.h"

namespace esphome {
namespace scs_bus {

class ScsBus : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_rx_pin(int pin) { rx_pin_ = pin; }
  void set_tx_pin(int pin) { tx_pin_ = pin; }
  void set_rx_inverted(bool inverted) { rx_inverted_ = inverted; }
  void set_tx_inverted(bool inverted) { tx_inverted_ = inverted; }
  void set_ack_timeout(uint32_t timeout_ms) { ack_timeout_ms_ = timeout_ms; }
  void set_max_retries(uint8_t retries) { max_retries_ = retries; }

  void add_on_frame_callback(std::function<void(std::vector<uint8_t>)> &&callback) {
    on_frame_callbacks_.add(std::move(callback));
  }
  void add_doorbell_listener(uint8_t address, std::function<void()> &&callback);
  bool send_door_unlock(uint8_t address);

 protected:
  struct PendingTx {
    ScsFrame frame;
    uint8_t attempts{0};
  };

  static void on_byte_(void *context, uint8_t byte);
  static void on_transmit_done_(void *context);
  void handle_byte_(uint8_t byte);
  void handle_frame_(const ScsFrame &frame);
  void start_next_transmission_();
  void fail_or_retry_();
  void publish_frame_(const ScsFrame &frame);

  int rx_pin_{-1};
  int tx_pin_{-1};
  bool rx_inverted_{false};
  bool tx_inverted_{false};
  uint32_t ack_timeout_ms_{100};
  uint8_t max_retries_{3};
  ScsRmt rmt_;
  ScsFrameAssembler assembler_;
  std::vector<PendingTx> transmit_queue_;
  PendingTx active_tx_{};
  bool active_tx_valid_{false};
  bool awaiting_ack_{false};
  uint32_t ack_deadline_{0};
  uint32_t last_bus_activity_ms_{0};
  CallbackManager<void(std::vector<uint8_t>)> on_frame_callbacks_;
  CallbackManager<void(uint8_t)> on_doorbell_callbacks_;
  uint32_t received_frames_{0};
  uint32_t invalid_frames_{0};
  uint32_t ack_timeouts_{0};
  uint32_t retries_{0};
  uint32_t queue_overflows_{0};
};

}  // namespace scs_bus
}  // namespace esphome
