#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#ifdef USE_ESP32
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
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
  static bool on_rx_done_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *arg);
  bool start_receive_();
  void handle_received_();

  static constexpr size_t RX_SYMBOL_CAPACITY = 128;
  rmt_channel_handle_t rx_channel_{nullptr};
  rmt_channel_handle_t tx_channel_{nullptr};
  rmt_encoder_handle_t tx_encoder_{nullptr};
  rmt_symbol_word_t rx_symbols_[RX_SYMBOL_CAPACITY]{};
  volatile size_t rx_symbol_count_{0};
  volatile bool rx_done_{false};
#endif

  InternalGPIOPin *rx_pin_{nullptr};
  InternalGPIOPin *tx_pin_{nullptr};
};

}  // namespace esphome::scs_bticino
