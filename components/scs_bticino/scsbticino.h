#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/automation.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "scsbticino_rx.h"
#include "scsbticino_rx_policy.h"
#include "scsbticino_tx.h"

#include <array>

#ifdef USE_ESP32
#include "driver/gptimer.h"
#endif

namespace esphome::scs_bticino {

class ScsBticinoController : public Component {
 public:
  void set_rx_pin(InternalGPIOPin *pin) { this->rx_pin_ = pin; }
  void set_tx_pin(InternalGPIOPin *pin) { this->tx_pin_ = pin; }
  void set_telegram_sensor(text_sensor::TextSensor *sensor) { this->telegram_sensor_ = sensor; }

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
  bool start_queued_tx_();
  bool start_tx_(bool local_ack);
  void IRAM_ATTR push_tx_trace_(uint8_t kind);
  void drain_tx_traces_();
  void set_tx_defer_reason_(uint8_t reason);

  static constexpr uint8_t TX_TRACE_ATTEMPT = 0;
  static constexpr uint8_t TX_TRACE_COLLISION = 1;
  static constexpr uint8_t TX_TRACE_WAIT_RESPONSE = 2;
  static constexpr uint8_t TX_TRACE_CAPACITY = 8;
  struct TxTrace {
    std::array<uint8_t, SCS_EXTENDED_SIZE> bytes{};
    uint8_t length{0};
    uint8_t kind{0};
    uint8_t type{0};
    uint8_t attempts{0};
    uint16_t collisions{0};
    bool local_ack{false};
  };

  gptimer_handle_t tx_timer_{nullptr};
  // GPIO ISR producer, GPTimer ISR consumer.
  volatile bool access_contended_{false};
  // GPTimer completion is handed to the ESPHome loop; raw-A5 completion is
  // produced by the loop after it stops the timer.
  volatile bool tx_result_ready_{false};
  volatile bool tx_result_local_ack_{false};
  volatile bool tx_timer_fault_{false};
  volatile ScsTxResult tx_result_{ScsTxResult::SUCCESS};
  volatile esp_err_t tx_timer_error_{ESP_OK};
  // GPTimer ISR producer, ESPHome loop consumer. This follows the same C3
  // single-core ownership assumption as the RX capture ring.
  std::array<TxTrace, TX_TRACE_CAPACITY> tx_traces_{};
  volatile uint8_t tx_trace_read_{0};
  volatile uint8_t tx_trace_write_{0};
  volatile bool tx_trace_overflow_{false};
  volatile uint16_t tx_trace_dropped_{0};
  uint8_t tx_defer_reason_{0};
#endif

  ScsBticinoReceiver receiver_;
  ScsBticinoTx transmitter_;
  ScsBticinoRxPolicy rx_policy_;
  bool pending_local_ack_{false};
  InternalGPIOPin *rx_pin_{nullptr};
  InternalGPIOPin *tx_pin_{nullptr};
  text_sensor::TextSensor *telegram_sensor_{nullptr};
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
