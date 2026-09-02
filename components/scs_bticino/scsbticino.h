#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#include "scsbticino_rx.h"
#include "scsbticino_tx.h"

#ifdef USE_ESP32
#include "driver/gptimer.h"
#endif

namespace esphome::scs_bticino {

class ScsBticinoController : public Component {
 public:
  void set_rx_pin(InternalGPIOPin *pin) { this->rx_pin_ = pin; }
  void set_tx_pin(InternalGPIOPin *pin) { this->tx_pin_ = pin; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  bool send(const ScsBticinoData &frame, ScsTxType type);
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
#ifdef USE_ESP32
  static bool IRAM_ATTR on_tx_timer_(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *arg);
  static void IRAM_ATTR on_rx_edge_(void *arg);
  bool arm_tx_timer_(uint64_t alarm_us);

  gptimer_handle_t tx_timer_{nullptr};
  volatile bool access_contended_{false};
  volatile bool tx_result_ready_{false};
  volatile ScsTxResult tx_result_{ScsTxResult::SUCCESS};
  uint64_t next_alarm_us_{0};
#endif

  ScsBticinoReceiver receiver_;
  ScsBticinoTx transmitter_;
  InternalGPIOPin *rx_pin_{nullptr};
  InternalGPIOPin *tx_pin_{nullptr};
};

}  // namespace esphome::scs_bticino
