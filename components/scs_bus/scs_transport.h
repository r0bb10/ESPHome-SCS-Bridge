#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#include "scs_serial.h"

namespace esphome::scs_bus {

// GPIO/GPTimer receiver and RMT transmitter for the normalized SCS waveform.
// ISR-produced RX events have a timestamp and monotonic sequence number.
class ScsTransport {
 public:
  static constexpr size_t MAX_TX_BYTES = 16;

  struct RxEvent {
    uint8_t byte;
    uint32_t timestamp_us;
    uint32_t sequence;
  };
  ScsTransport() = default;
  ~ScsTransport();

  ScsTransport(const ScsTransport &) = delete;
  ScsTransport &operator=(const ScsTransport &) = delete;

  esp_err_t setup(int rx_pin, int tx_pin, bool rx_inverted, bool tx_inverted);
  // These methods are owned exclusively by the high-priority TX coordinator.
  esp_err_t transmit(const uint8_t *data, size_t length, uint32_t transaction_id);
  esp_err_t cancel_transmit();
  bool take_rx_event(RxEvent *event);
  bool take_transmit_done(uint32_t *transaction_id, uint32_t *completed_at_us);
  void arm_event_task_in(uint32_t delay_us);
  void teardown();

  void set_event_task(TaskHandle_t task) { event_task_ = task; }

  bool transmitting() const { return this->transmitting_; }
  bool receiving() const { return this->receiver_.receiving(); }
  bool can_transmit() const { return !this->transmitting_ && !this->receiver_.receiving(); }
  uint32_t last_bus_activity_us() const { return this->last_bus_activity_us_; }
  bool take_collision();

 protected:
  static bool on_transmit_done_(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t *event, void *context);
  static void on_rx_edge_(void *context);
  static bool on_rx_timer_(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *context);
  static bool on_task_wake_timer_(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *context);

  void schedule_rx_sample_(uint64_t alarm_count);
  void queue_rx_byte_(uint8_t byte);
  bool queue_rx_event_(const RxEvent &event);
  void flush_echo_prefix_();
  void abort_transmit_from_isr_();

  static constexpr uint32_t RESOLUTION_HZ = ScsSerialReceiver::CLOCK_HZ;
  static constexpr uint32_t TICKS_PER_US = RESOLUTION_HZ / 1000000;
  // RMT channel memory must not consume neighbouring channels.
  static constexpr size_t RMT_CHANNEL_SYMBOLS = SOC_RMT_MEM_WORDS_PER_CHANNEL;
  static constexpr size_t MAX_SYMBOLS_PER_BYTE = (SCS_SERIAL_MAX_RUNS + 1) / 2;
  static constexpr uint8_t RX_QUEUE_SIZE = 64;

  rmt_channel_handle_t tx_channel_{nullptr};
  rmt_encoder_handle_t copy_encoder_{nullptr};
  rmt_symbol_word_t tx_symbols_[MAX_TX_BYTES * MAX_SYMBOLS_PER_BYTE]{};
  RxEvent received_events_[RX_QUEUE_SIZE]{};
  RxEvent echo_prefix_[MAX_TX_BYTES]{};
  volatile uint8_t expected_tx_[MAX_TX_BYTES]{};

  TaskHandle_t event_task_{nullptr};
  std::atomic<uint8_t> received_head_{0};
  std::atomic<uint8_t> received_tail_{0};
  std::atomic<uint32_t> rx_sequence_{0};
  volatile uint32_t last_bus_activity_us_{0};
  volatile bool transmit_complete_{false};
  volatile uint32_t completed_transaction_id_{0};
  volatile uint32_t completed_at_us_{0};
  volatile uint32_t transmitting_transaction_id_{0};
  volatile bool transmitting_{false};
  volatile bool checking_echo_{false};
  volatile bool echo_prefix_pending_{false};
  volatile bool collision_detected_{false};
  volatile uint8_t expected_tx_size_{0};
  volatile uint8_t expected_tx_index_{0};
  int rx_pin_{-1};
  int tx_pin_{-1};
  bool rx_inverted_{false};
  bool tx_inverted_{false};
  volatile bool tx_disconnected_{false};
  gptimer_handle_t rx_timer_{nullptr};
  gptimer_handle_t task_wake_timer_{nullptr};
  ScsSerialReceiver receiver_;
  bool configured_{false};
};

}  // namespace esphome::scs_bus
