#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#include "scsbticino_rx.h"

#ifdef USE_ESP32
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#endif

namespace esphome::scs_bticino {

class ScsBticinoController : public Component {
 public:
  void set_rx_pin(InternalGPIOPin *pin) { this->rx_pin_ = pin; }
  void set_tx_pin(InternalGPIOPin *pin) { this->tx_pin_ = pin; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
#ifdef USE_ESP32
  rmt_channel_handle_t tx_channel_{nullptr};
  rmt_encoder_handle_t tx_encoder_{nullptr};
#endif

  ScsBticinoReceiver receiver_;
  InternalGPIOPin *rx_pin_{nullptr};
  InternalGPIOPin *tx_pin_{nullptr};
};

}  // namespace esphome::scs_bticino
