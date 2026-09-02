#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/automation.h"

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

template<typename... Ts> class ScsBticinoSendAction : public Action<Ts...> {
 public:
  void set_parent(ScsBticinoController *parent) { this->parent_ = parent; }
  void set_payload(const uint8_t *payload, uint8_t length) {
    this->payload_ = payload;
    this->payload_length_ = length;
  }
  void set_type(uint8_t type) { this->type_ = static_cast<ScsTxType>(type); }
  void play(Ts... x) override {
    ScsBticinoData frame;
    if (this->parent_ != nullptr && ScsBticinoData::from_payload(frame, this->payload_, this->payload_length_))
      this->parent_->send(frame, this->type_);
  }

 protected:
  ScsBticinoController *parent_{nullptr};
  const uint8_t *payload_{nullptr};
  uint8_t payload_length_{0};
  ScsTxType type_{ScsTxType::SHORT};
};

}  // namespace esphome::scs_bticino
