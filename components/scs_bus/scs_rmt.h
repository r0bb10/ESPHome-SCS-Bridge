#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"

namespace esphome::scs_bus {

// RMT transport for the normalized SCS serial waveform. RMT callback work is
// isolated internally; registered callbacks are invoked by loop().
class ScsRmt {
 public:
  static constexpr size_t MAX_TX_BYTES = 16;

  using ReceiveCallback = void (*)(void *context, uint8_t byte);
  using TransmitDoneCallback = void (*)(void *context);

  ScsRmt() = default;
  ~ScsRmt();

  ScsRmt(const ScsRmt &) = delete;
  ScsRmt &operator=(const ScsRmt &) = delete;

  esp_err_t setup(int rx_pin, int tx_pin, bool rx_inverted, bool tx_inverted);
  esp_err_t start_receive();
  esp_err_t transmit(const uint8_t *data, size_t length);
  void loop();
  void teardown();

  void set_receive_callback(ReceiveCallback callback, void *context) {
    this->receive_callback_ = callback;
    this->receive_context_ = context;
  }
  void set_transmit_done_callback(TransmitDoneCallback callback, void *context) {
    this->transmit_done_callback_ = callback;
    this->transmit_done_context_ = context;
  }

  bool transmitting() const { return this->transmitting_; }

 protected:
  static bool on_receive_done_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *context);
  static bool on_transmit_done_(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t *event, void *context);

  void decode_(const rmt_symbol_word_t *symbols, size_t symbol_count);
  bool sample_level_(const rmt_symbol_word_t *symbols, size_t symbol_count, uint32_t time_us, uint8_t *level) const;
  size_t encode_byte_(uint8_t byte, rmt_symbol_word_t *symbols, size_t symbol_capacity) const;

  static constexpr uint32_t RESOLUTION_HZ = 1000000;
  static constexpr uint16_t CELL_US = 105;
  static constexpr uint16_t HALF_CELL_US = 35;
  static constexpr uint16_t SAMPLE_US = HALF_CELL_US / 2;
  static constexpr size_t RX_SYMBOLS = 256;
  static constexpr size_t MAX_SYMBOLS_PER_BYTE = 10;
  static constexpr uint8_t RX_QUEUE_SIZE = 64;

  rmt_channel_handle_t rx_channel_{nullptr};
  rmt_channel_handle_t tx_channel_{nullptr};
  rmt_encoder_handle_t copy_encoder_{nullptr};
  rmt_symbol_word_t rx_symbols_[RX_SYMBOLS]{};
  rmt_symbol_word_t tx_symbols_[MAX_TX_BYTES * MAX_SYMBOLS_PER_BYTE]{};
  uint8_t received_bytes_[RX_QUEUE_SIZE]{};

  ReceiveCallback receive_callback_{nullptr};
  void *receive_context_{nullptr};
  TransmitDoneCallback transmit_done_callback_{nullptr};
  void *transmit_done_context_{nullptr};
  volatile uint8_t received_head_{0};
  volatile uint8_t received_tail_{0};
  volatile size_t received_symbol_count_{0};
  volatile bool receive_complete_{false};
  volatile bool transmit_complete_{false};
  volatile bool transmitting_{false};
  bool configured_{false};
};

}  // namespace esphome::scs_bus
